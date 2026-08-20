#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include <filesystem>
#include <cs_dxvk_api.h>

// Loads the prefixed DXVK DLLs from CommunityShaders/bin so they do not alias
// Skyrim's process-wide System32 d3d11.dll and dxgi.dll modules.
namespace DxvkLoader
{
	struct PresentWaitInterop
	{
		PFN_csDxvkGetPresenterSurfaceState getPresenterSurfaceState = nullptr;
		PFN_csDxvkEnqueueInteropCommandBuffer enqueueInteropCommandBuffer = nullptr;
		PFN_csDxvkGetPresentWaitSemaphoreState getPresentWaitSemaphoreState = nullptr;
		PFN_csDxvkClearPresentWaitSemaphore clearPresentWaitSemaphore = nullptr;
		PFN_csDxvkCancelPresentWaitSemaphore cancelPresentWaitSemaphore = nullptr;
		PFN_csDxvkReleaseQueuedPresentWaitSemaphoresAfterIdle releaseQueuedPresentWaitSemaphoresAfterIdle = nullptr;

		[[nodiscard]] bool IsComplete() const;
	};

	/** @brief Loads DXVK before the game creates its D3D11 device. */
	bool Load();

	/** @brief Returns whether DXVK loaded successfully. */
	bool IsLoaded();
	[[nodiscard]] bool HasCoreRenderer();
	[[nodiscard]] bool HasFrameGenerationControl();
	[[nodiscard]] bool HasPresentCallbacks();
	[[nodiscard]] bool HasFrameGenerationOwnershipCallback();
	[[nodiscard]] bool HasSwapchainTeardownCallback();
	[[nodiscard]] bool SupportsSynchronousPresent();
	[[nodiscard]] PresentWaitInterop GetPresentWaitInterop();
	bool SetTearingPreference(uint32_t a_preference);
	bool SetTargetFrameRate(double a_fps);
	bool RegisterFrameGenerationCallbacks(PFN_csDxvkFrameGenOwnershipQuery a_ownership,
		PFN_csDxvkPresentCallback a_begin, PFN_csDxvkPresentCallback a_completed,
		PFN_csDxvkSwapchainTornDownCallback a_tornDown);
	bool RequestSwapchainRecreate();
	bool SetSynchronousPresent(bool a_enabled);
	bool SetPresentQueueDepth(uint32_t a_depth);

	/** @brief Returns whether settings or CS_NATIVE_D3D11 request the native runtime. */
	bool NativeModeRequested();

	/** @brief Returns the module-relative renderer runtime directory. */
	std::filesystem::path GetRuntimeDir();

	/** @brief Returns DXVK's D3D11CreateDeviceAndSwapChain export. */
	decltype(&D3D11CreateDeviceAndSwapChain) GetD3D11CreateDeviceAndSwapChain();

	/** @brief Returns DXVK's CreateDXGIFactory export. */
	decltype(&CreateDXGIFactory) GetCreateDXGIFactory();
}
