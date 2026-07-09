#include "D3DX9MathUpgrade.h"

#include <DirectXMath.h>

#include <delayimp.h>
#include <tlhelp32.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace D3DX9MathUpgrade
{
	using namespace DirectX;

	namespace
	{
		// D3DX9 types are binary-compatible with the DirectXMath storage types used here:
		// D3DXMATRIX = row-major float[4][4] (XMFLOAT4X4), D3DXVECTOR3 = XMFLOAT3,
		// D3DXPLANE (a,b,c,d) = XMFLOAT4. All are 4-byte aligned, and the XMLoad*/XMStore*
		// helpers use unaligned loads/stores. Every wrapper loads its inputs into registers
		// before storing the result, so in-place calls (pOut aliasing an input, which D3DX
		// permits) are safe. All functions return pOut, in RAX, per the D3DX9 contract.

		XMFLOAT4X4* WINAPI MatrixTranspose(XMFLOAT4X4* pOut, const XMFLOAT4X4* pM)
		{
			XMStoreFloat4x4(pOut, XMMatrixTranspose(XMLoadFloat4x4(pM)));
			return pOut;
		}

		XMFLOAT4X4* WINAPI MatrixMultiply(XMFLOAT4X4* pOut, const XMFLOAT4X4* pM1, const XMFLOAT4X4* pM2)
		{
			XMStoreFloat4x4(pOut, XMMatrixMultiply(XMLoadFloat4x4(pM1), XMLoadFloat4x4(pM2)));
			return pOut;
		}

		XMFLOAT4X4* WINAPI MatrixMultiplyTranspose(XMFLOAT4X4* pOut, const XMFLOAT4X4* pM1, const XMFLOAT4X4* pM2)
		{
			XMStoreFloat4x4(pOut, XMMatrixMultiplyTranspose(XMLoadFloat4x4(pM1), XMLoadFloat4x4(pM2)));
			return pOut;
		}

		XMFLOAT4X4* WINAPI MatrixInverse(XMFLOAT4X4* pOut, float* pDeterminant, const XMFLOAT4X4* pM)
		{
			XMVECTOR determinant;
			const XMMATRIX inverse = XMMatrixInverse(&determinant, XMLoadFloat4x4(pM));
			const float det = XMVectorGetX(determinant);

			// D3DX9 semantics: a singular matrix fails the call — return NULL and leave the
			// output and determinant untouched (XMMatrixInverse would have produced INFs).
			if (det == 0.0f)
				return nullptr;

			if (pDeterminant)
				*pDeterminant = det;

			XMStoreFloat4x4(pOut, inverse);
			return pOut;
		}

		XMFLOAT3* WINAPI Vec3Normalize(XMFLOAT3* pOut, const XMFLOAT3* pV)
		{
			// XMVector3Normalize returns a zero vector for zero-length input — same as D3DX9.
			XMStoreFloat3(pOut, XMVector3Normalize(XMLoadFloat3(pV)));
			return pOut;
		}

		XMFLOAT3* WINAPI Vec3TransformCoord(XMFLOAT3* pOut, const XMFLOAT3* pV, const XMFLOAT4X4* pM)
		{
			XMStoreFloat3(pOut, XMVector3TransformCoord(XMLoadFloat3(pV), XMLoadFloat4x4(pM)));
			return pOut;
		}

		XMFLOAT3* WINAPI Vec3TransformNormal(XMFLOAT3* pOut, const XMFLOAT3* pV, const XMFLOAT4X4* pM)
		{
			XMStoreFloat3(pOut, XMVector3TransformNormal(XMLoadFloat3(pV), XMLoadFloat4x4(pM)));
			return pOut;
		}

		XMFLOAT4* WINAPI PlaneNormalize(XMFLOAT4* pOut, const XMFLOAT4* pP)
		{
			// Not XMPlaneNormalize: that returns INFs for a zero-length normal, while D3DX9
			// returns a zero plane. Replicate the D3DX behavior with an explicit zero mask
			// (XMVector3LengthSq/XMVectorSqrt broadcast across all four lanes, so the mask
			// covers d as well).
			const XMVECTOR plane = XMLoadFloat4(pP);
			const XMVECTOR length = XMVectorSqrt(XMVector3LengthSq(plane));
			const XMVECTOR zeroMask = XMVectorNotEqual(length, XMVectorZero());
			XMStoreFloat4(pOut, XMVectorAndInt(XMVectorDivide(plane, length), zeroMask));
			return pOut;
		}

		XMFLOAT4* WINAPI PlaneTransform(XMFLOAT4* pOut, const XMFLOAT4* pP, const XMFLOAT4X4* pM)
		{
			// D3DXPlaneTransform is a plain vec4 * matrix transform (the caller is expected
			// to pass the inverse-transpose), which is exactly XMPlaneTransform.
			XMStoreFloat4(pOut, XMPlaneTransform(XMLoadFloat4(pP), XMLoadFloat4x4(pM)));
			return pOut;
		}

		struct Replacement
		{
			const char* name;
			void* replacement;
		};

		const Replacement kReplacements[] = {
			{ "D3DXMatrixTranspose", reinterpret_cast<void*>(&MatrixTranspose) },
			{ "D3DXMatrixMultiply", reinterpret_cast<void*>(&MatrixMultiply) },
			{ "D3DXMatrixMultiplyTranspose", reinterpret_cast<void*>(&MatrixMultiplyTranspose) },
			{ "D3DXMatrixInverse", reinterpret_cast<void*>(&MatrixInverse) },
			{ "D3DXVec3Normalize", reinterpret_cast<void*>(&Vec3Normalize) },
			{ "D3DXVec3TransformCoord", reinterpret_cast<void*>(&Vec3TransformCoord) },
			{ "D3DXVec3TransformNormal", reinterpret_cast<void*>(&Vec3TransformNormal) },
			{ "D3DXPlaneNormalize", reinterpret_cast<void*>(&PlaneNormalize) },
			{ "D3DXPlaneTransform", reinterpret_cast<void*>(&PlaneTransform) },
		};

		/// @brief Any D3DX9 flavor: the math entry points are identical across d3dx9_24..d3dx9_43.
		bool IsD3DX9Module(const char* name)
		{
			return _strnicmp(name, "d3dx9", 5) == 0;
		}

		void* FindReplacement(const char* importName)
		{
			for (const auto& replacement : kReplacements) {
				if (strcmp(importName, replacement.name) == 0)
					return replacement.replacement;
			}
			return nullptr;
		}

		bool PatchIATEntry(ULONGLONG* slot, void* replacement)
		{
			if (*slot == reinterpret_cast<ULONGLONG>(replacement))
				return false;  // already ours (idempotent re-sweep)

			DWORD oldProtect = 0;
			if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtect))
				return false;

			*slot = reinterpret_cast<ULONGLONG>(replacement);
			VirtualProtect(slot, sizeof(*slot), oldProtect, &oldProtect);
			return true;
		}

		/// @brief Patches one module's regular import table. Returns entries rewritten.
		uint32_t PatchModuleImports(uint8_t* base)
		{
			const auto* dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
			if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE)
				return 0;
			const auto* ntHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dosHeader->e_lfanew);
			if (ntHeaders->Signature != IMAGE_NT_SIGNATURE)
				return 0;

			uint32_t patched = 0;

			const auto& importDir = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
			if (importDir.VirtualAddress) {
				for (auto* desc = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(base + importDir.VirtualAddress); desc->Name; ++desc) {
					if (!IsD3DX9Module(reinterpret_cast<const char*>(base + desc->Name)))
						continue;
					// Some linkers omit OriginalFirstThunk; without the name table the
					// entries can't be identified once bound, so skip those.
					if (!desc->OriginalFirstThunk)
						continue;

					auto* nameThunk = reinterpret_cast<PIMAGE_THUNK_DATA>(base + desc->OriginalFirstThunk);
					auto* iatThunk = reinterpret_cast<PIMAGE_THUNK_DATA>(base + desc->FirstThunk);
					for (; nameThunk->u1.AddressOfData; ++nameThunk, ++iatThunk) {
						if (IMAGE_SNAP_BY_ORDINAL(nameThunk->u1.Ordinal))
							continue;
						const auto* import = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(base + nameThunk->u1.AddressOfData);
						if (void* replacement = FindReplacement(reinterpret_cast<const char*>(import->Name))) {
							if (PatchIATEntry(&iatThunk->u1.Function, replacement))
								patched++;
						}
					}
				}
			}

			// Delay-load imports keep their own IAT; entries start as resolver thunks and are
			// rewritten on first call. Overwriting them directly is valid either way (the
			// resolver is simply never invoked for a pre-filled slot).
			const auto& delayDir = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
			if (delayDir.VirtualAddress) {
				for (auto* desc = reinterpret_cast<PCImgDelayDescr>(base + delayDir.VirtualAddress); desc->rvaDLLName; ++desc) {
					if (!IsD3DX9Module(reinterpret_cast<const char*>(base + desc->rvaDLLName)))
						continue;

					auto* nameThunk = reinterpret_cast<PIMAGE_THUNK_DATA>(base + desc->rvaINT);
					auto* iatThunk = reinterpret_cast<PIMAGE_THUNK_DATA>(base + desc->rvaIAT);
					for (; nameThunk->u1.AddressOfData; ++nameThunk, ++iatThunk) {
						if (IMAGE_SNAP_BY_ORDINAL(nameThunk->u1.Ordinal))
							continue;
						const auto* import = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(base + nameThunk->u1.AddressOfData);
						if (void* replacement = FindReplacement(reinterpret_cast<const char*>(import->Name))) {
							if (PatchIATEntry(&iatThunk->u1.Function, replacement))
								patched++;
						}
					}
				}
			}

			return patched;
		}
	}

	void Sweep(const char* reason)
	{
		char disable[2] = {};
		if (GetEnvironmentVariableA("CS_NO_D3DX_UPGRADE", disable, sizeof(disable)) && disable[0] == '1') {
			logger::info("[D3DX9MathUpgrade] disabled via CS_NO_D3DX_UPGRADE");
			return;
		}

		const HMODULE self = reinterpret_cast<HMODULE>(&__ImageBase);

		HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
		if (snapshot == INVALID_HANDLE_VALUE) {
			logger::warn("[D3DX9MathUpgrade] module snapshot failed (error {})", GetLastError());
			return;
		}

		uint32_t totalPatched = 0;
		uint32_t modulesTouched = 0;

		MODULEENTRY32W entry{};
		entry.dwSize = sizeof(entry);
		if (Module32FirstW(snapshot, &entry)) {
			do {
				// Never rewrite our own imports (we intentionally have none to d3dx9) and
				// leave d3dx9 itself alone — its internal calls are direct, not IAT-routed.
				if (entry.hModule == self || _wcsnicmp(entry.szModule, L"d3dx9", 5) == 0)
					continue;

				if (uint32_t patched = PatchModuleImports(reinterpret_cast<uint8_t*>(entry.modBaseAddr))) {
					logger::info("[D3DX9MathUpgrade] {}: patched {} d3dx9 math import(s)",
						std::filesystem::path(entry.szModule).string(), patched);
					totalPatched += patched;
					modulesTouched++;
				}
			} while (Module32NextW(snapshot, &entry));
		}
		CloseHandle(snapshot);

		logger::info("[D3DX9MathUpgrade] sweep ({}): {} import entr{} across {} module(s) now point at DirectXMath",
			reason, totalPatched, totalPatched == 1 ? "y" : "ies", modulesTouched);
	}

	void Install()
	{
		Sweep("plugin load");
	}
}
