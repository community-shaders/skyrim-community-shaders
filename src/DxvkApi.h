#pragma once

#include <Windows.h>
#include <cs_dxvk_api.h>
#include <d3d11.h>
#include <dxgi.h>

namespace DxvkLoader::Detail
{
	struct Api
	{
		HMODULE d3d11Module = nullptr;
		HMODULE dxgiModule = nullptr;
		decltype(&D3D11CreateDeviceAndSwapChain) d3d11Create = nullptr;
		decltype(&CreateDXGIFactory) createFactory = nullptr;
		PFN_csDxvkGetApiVersion getApiVersion = nullptr;
		PFN_csDxvkSetTearingPreference setTearingPreference = nullptr;
		PFN_csDxvkGetPresenterSurfaceState getPresenterSurfaceState = nullptr;
		PFN_csDxvkSetFrameGenOwnershipQuery setFrameGenOwnershipQuery = nullptr;
		PFN_csDxvkSetPresentCallback setPresentBeginCallback = nullptr;
		PFN_csDxvkSetPresentCallback setPresentCompletedCallback = nullptr;
		PFN_csDxvkRequestSwapchainRecreate requestSwapchainRecreate = nullptr;
		PFN_csDxvkSetSwapchainTornDownCallback setSwapchainTornDownCallback = nullptr;
		PFN_csDxvkSetTargetFrameRate setTargetFrameRate = nullptr;
		PFN_csDxvkSetSyncPresent setSyncPresent = nullptr;
		PFN_csDxvkSetPresentQueueDepth setPresentQueueDepth = nullptr;
		PFN_csDxvkEnqueueInteropCommandBuffer enqueueInteropCommandBuffer = nullptr;
		PFN_csDxvkGetPresentWaitSemaphoreState getPresentWaitSemaphoreState = nullptr;
		PFN_csDxvkClearPresentWaitSemaphore clearPresentWaitSemaphore = nullptr;
		PFN_csDxvkCancelPresentWaitSemaphore cancelPresentWaitSemaphore = nullptr;
		PFN_csDxvkReleaseQueuedPresentWaitSemaphoresAfterIdle releaseQueuedPresentWaitSemaphoresAfterIdle = nullptr;

		[[nodiscard]] bool HasCoreRenderer() const
		{
			return d3d11Create && createFactory && getApiVersion && getApiVersion() == CS_DXVK_API_VERSION;
		}
		[[nodiscard]] bool HasFrameGenerationControl() const
		{
			return requestSwapchainRecreate && setSyncPresent && getPresenterSurfaceState &&
			       setSwapchainTornDownCallback && setFrameGenOwnershipQuery;
		}
		[[nodiscard]] bool HasPresentCallbacks() const
		{
			return setPresentBeginCallback && setPresentCompletedCallback;
		}
		[[nodiscard]] bool HasPresentWaitInterop() const
		{
			return enqueueInteropCommandBuffer && getPresentWaitSemaphoreState && clearPresentWaitSemaphore &&
			       cancelPresentWaitSemaphore && releaseQueuedPresentWaitSemaphoresAfterIdle;
		}
	};
}
