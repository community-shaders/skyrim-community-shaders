#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include <filesystem>

// Loads the prefixed DXVK DLLs from CommunityShaders/bin so they do not alias
// Skyrim's process-wide System32 d3d11.dll and dxgi.dll modules.
namespace DxvkLoader
{
	/** @brief Loads DXVK before the game creates its D3D11 device. */
	bool Load();

	/** @brief Returns whether DXVK loaded successfully. */
	bool IsLoaded();

	/** @brief Returns whether CS_NATIVE_D3D11 requests the native runtime. */
	bool NativeModeRequested();

	/** @brief Returns the module-relative renderer runtime directory. */
	std::filesystem::path GetRuntimeDir();

	/** @brief Returns DXVK's D3D11CreateDeviceAndSwapChain export. */
	decltype(&D3D11CreateDeviceAndSwapChain) GetD3D11CreateDeviceAndSwapChain();

	/** @brief Returns DXVK's CreateDXGIFactory export. */
	decltype(&CreateDXGIFactory) GetCreateDXGIFactory();
}
