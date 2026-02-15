#include "OpenVRDetection.h"
#include <format>
#include <openvr.h>
#include <vector>
#include <windows.h>
#include <winver.h>
#pragma comment(lib, "version.lib")

namespace VRDetection
{
	const char* RuntimeTypeToString(RuntimeType type)
	{
		switch (type) {
		case RuntimeType::SteamVR:
			return "SteamVR";
		case RuntimeType::OpenComposite:
			return "OpenComposite";
		default:
			return "Unknown";
		}
	}

	bool ProbeRuntimeInterfaces(OpenVRDetectionResult& result)
	{
		HMODULE hModule = GetModuleHandleA("openvr_api.dll");
		if (!hModule)
			return false;

		using pfnIsValid = bool(__cdecl*)(const char*);
		auto IsValid = reinterpret_cast<pfnIsValid>(GetProcAddress(hModule, "VR_IsInterfaceVersionValid"));
		if (!IsValid)
			return false;

		result.hasOverlayInterface = IsValid(vr::IVROverlay_Version);
		result.hasSystemInterface = IsValid(vr::IVRSystem_Version);
		result.hasCompositorInterface = IsValid(vr::IVRCompositor_Version);

		result.probingSucceeded = result.hasOverlayInterface && result.hasSystemInterface && result.hasCompositorInterface;
		return result.probingSucceeded;
	}

	void GatherDLLInfo(OpenVRDetectionResult& result)
	{
		HMODULE hModule = GetModuleHandleA("openvr_api.dll");
		if (!hModule) {
			result.isAvailable = false;
			return;
		}

		result.isAvailable = true;

		char dllPath[MAX_PATH];
		DWORD fileLength = GetModuleFileNameA(hModule, dllPath, MAX_PATH);
		if (fileLength == 0 || (fileLength == MAX_PATH && GetLastError() == ERROR_INSUFFICIENT_BUFFER)) {
			result.isAvailable = false;
			return;
		}

		result.dllPath = dllPath;

		DWORD dwSize = GetFileVersionInfoSizeA(dllPath, nullptr);
		if (dwSize > 0) {
			std::vector<BYTE> buffer(dwSize);
			if (GetFileVersionInfoA(dllPath, 0, dwSize, buffer.data())) {
				VS_FIXEDFILEINFO* pFileInfo = nullptr;
				UINT len = 0;
				if (VerQueryValueA(buffer.data(), "\\", reinterpret_cast<LPVOID*>(&pFileInfo), &len)) {
					DWORD major = HIWORD(pFileInfo->dwFileVersionMS);
					DWORD minor = LOWORD(pFileInfo->dwFileVersionMS);
					DWORD build = HIWORD(pFileInfo->dwFileVersionLS);
					DWORD revision = LOWORD(pFileInfo->dwFileVersionLS);
					result.version = std::format("{}.{}.{}.{}", major, minor, build, revision);
				}
			}
		}

		if (result.version.empty())
			result.version = "Unknown";

		WIN32_FIND_DATAA findData;
		HANDLE hFind = FindFirstFileA(dllPath, &findData);
		if (hFind != INVALID_HANDLE_VALUE) {
			FindClose(hFind);
			ULARGE_INTEGER fileSize;
			fileSize.LowPart = findData.nFileSizeLow;
			fileSize.HighPart = findData.nFileSizeHigh;
			result.fileSize = fileSize.QuadPart;

			SYSTEMTIME st;
			FileTimeToSystemTime(&findData.ftLastWriteTime, &st);
			result.modificationTime = std::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}",
				st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
		}
	}

	RuntimeType DetectRuntimeType(const std::string& dllPath, const std::string& version, uint64_t fileSize)
	{
		// OpenComposite DLLs are typically small (~600KB) with version 1.0.10.0
		if (version == "1.0.10.0" && fileSize < 700000)
			return RuntimeType::OpenComposite;

		// Check path for OpenComposite indicators
		std::string lowerPath = dllPath;
		for (auto& c : lowerPath)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

		if (lowerPath.find("opencomposite") != std::string::npos)
			return RuntimeType::OpenComposite;

		// SteamVR DLLs are typically larger and have higher version numbers
		if (lowerPath.find("steamvr") != std::string::npos || lowerPath.find("steam") != std::string::npos)
			return RuntimeType::SteamVR;

		// Higher version numbers suggest SteamVR
		if (!version.empty() && version != "Unknown" && version != "1.0.10.0")
			return RuntimeType::SteamVR;

		return RuntimeType::Unknown;
	}

	OpenVRDetectionResult Detect()
	{
		OpenVRDetectionResult result;

		GatherDLLInfo(result);
		if (!result.isAvailable)
			return result;

		result.runtimeType = DetectRuntimeType(result.dllPath, result.version, result.fileSize);

		// Detect compatibility via runtime interface probing
		result.isCompatible = ProbeRuntimeInterfaces(result);

		return result;
	}
}
