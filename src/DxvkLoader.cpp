#include "DxvkLoader.h"

#include <filesystem>

namespace DxvkLoader
{
	namespace
	{
		bool g_attempted = false;
		bool g_loaded = false;
		decltype(&D3D11CreateDeviceAndSwapChain) g_d3d11Create = nullptr;
		decltype(&CreateDXGIFactory) g_createFactory = nullptr;
	}

	// Resolve relative to the plugin for mod-manager VFS compatibility.
	std::filesystem::path GetRuntimeDir()
	{
		HMODULE self = nullptr;
		if (!::GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&GetRuntimeDir),
				&self)) {
			return {};
		}
		wchar_t buf[MAX_PATH]{};
		const DWORD n = ::GetModuleFileNameW(self, buf, MAX_PATH);
		if (n == 0 || n >= MAX_PATH) {
			return {};
		}
		return std::filesystem::path(buf).parent_path() / L"CommunityShaders" / L"bin";
	}

	bool NativeModeRequested()
	{
		// Debug override for testing against the native D3D11 runtime.
		static const bool s_native = [] {
			char buf[8] = {};
			return GetEnvironmentVariableA("CS_NATIVE_D3D11", buf, sizeof(buf)) && buf[0] == '1';
		}();
		return s_native;
	}

	bool Load()
	{
		if (g_attempted) {
			return g_loaded;
		}
		g_attempted = true;

		if (NativeModeRequested()) {
			logger::info("[DXVK] CS_NATIVE_D3D11=1 -- skipping DXVK, using the system D3D11 runtime");
			return false;
		}

		if (!::SetEnvironmentVariableW(L"DXVK_HDR", L"1")) {
			logger::warn("[DXVK] Failed to enable HDR color-space support (error {})", ::GetLastError());
		}

		const auto dir = GetRuntimeDir();
		if (dir.empty()) {
			logger::error("[DXVK] Could not resolve plugin directory for DXVK DLLs");
			return false;
		}

		const auto dxgiPath = (dir / L"dxvk_dxgi.dll").wstring();
		const auto d3d11Path = (dir / L"dxvk_d3d11.dll").wstring();

		// dxvk_d3d11.dll imports dxvk_dxgi.dll by base name.
		const HMODULE dxgiMod = ::LoadLibraryExW(dxgiPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
		if (!dxgiMod) {
			const DWORD err = ::GetLastError();
			logger::error("[DXVK] Failed to load dxvk_dxgi.dll from '{}' (error {})", dir.string(), err);
			return false;
		}
		const HMODULE d3d11Mod = ::LoadLibraryExW(d3d11Path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
		if (!d3d11Mod) {
			const DWORD err = ::GetLastError();
			logger::error("[DXVK] Failed to load dxvk_d3d11.dll from '{}' (error {})", dir.string(), err);
			return false;
		}

		g_d3d11Create = reinterpret_cast<decltype(g_d3d11Create)>(::GetProcAddress(d3d11Mod, "D3D11CreateDeviceAndSwapChain"));
		g_createFactory = reinterpret_cast<decltype(g_createFactory)>(::GetProcAddress(dxgiMod, "CreateDXGIFactory"));

		if (!g_d3d11Create || !g_createFactory) {
			logger::error("[DXVK] Resolved DXVK DLLs but missing exports (d3d11create={}, createfactory={})",
				g_d3d11Create != nullptr, g_createFactory != nullptr);
			return false;
		}

		// Frame generation enables synchronous present when it takes ownership.
		if (auto setSync = reinterpret_cast<void (*)(uint32_t)>(::GetProcAddress(d3d11Mod, "dxvkSetSyncPresent")))
			setSync(0u);
		else
			logger::warn("[DXVK] dxvkSetSyncPresent export missing -- present stays at the fork default (sync)");

		logger::info("[DXVK] Loaded DXVK from '{}' (dxvk_d3d11.dll + dxvk_dxgi.dll)", dir.string());
		g_loaded = true;
		return true;
	}

	bool IsLoaded() { return g_loaded; }
	decltype(&D3D11CreateDeviceAndSwapChain) GetD3D11CreateDeviceAndSwapChain() { return g_d3d11Create; }
	decltype(&CreateDXGIFactory) GetCreateDXGIFactory() { return g_createFactory; }
}
