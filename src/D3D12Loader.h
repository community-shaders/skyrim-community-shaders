#pragma once

#include <d3d11.h>
#include <d3d11on12.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

/**
 * @brief Runs the game on D3D12 by translating its D3D11 through D3D11On12.
 *
 * This is the D3D12 counterpart to DxvkLoader. Where DXVK substitutes a
 * D3D11-on-Vulkan runtime, this substitutes a D3D11-on-D3D12 one, so every draw
 * the engine issues ends up as D3D12 work on a D3D12 command queue, and the
 * swap chain is created on that queue rather than on a D3D11 device.
 *
 * The point of doing it this way is the upscaling path. Community Shaders owns
 * the D3D12 device and the direct command queue here, so DLSS, FSR and XeSS can
 * be driven through their native D3D12 entry points with the real interfaces,
 * instead of reaching across an API boundary. Resource wrapping
 * (ID3D11On12Device::CreateWrappedResource) is confined to that upscaling work
 * and to the swap-chain back buffer the translation layer has to hand back to
 * the engine as an ID3D11Texture2D -- the engine's own rendering never round
 * trips through it.
 *
 * Enabled with CS_D3D12=1. Off by default while it is being brought up.
 */
namespace D3D12Loader
{
	/** @brief Returns whether CS_D3D12=1 requests the D3D12 translation path. */
	bool Requested();

	/** @brief Creates the D3D12 device and queue. Call before the engine makes its device. */
	bool Load();

	/** @brief Returns whether the D3D12 path initialised successfully. */
	bool IsLoaded();

	/**
	 * @brief D3D11CreateDeviceAndSwapChain implemented on D3D12.
	 *
	 * Signature-compatible with the real export so it can be dropped into the
	 * same hook DXVK uses. Creates the D3D11On12 device over our D3D12 device
	 * and, when a swap-chain description is supplied, creates the swap chain on
	 * the D3D12 command queue.
	 */
	HRESULT WINAPI CreateDeviceAndSwapChain(
		IDXGIAdapter* pAdapter,
		D3D_DRIVER_TYPE DriverType,
		HMODULE Software,
		UINT Flags,
		const D3D_FEATURE_LEVEL* pFeatureLevels,
		UINT FeatureLevels,
		UINT SDKVersion,
		const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
		IDXGISwapChain** ppSwapChain,
		ID3D11Device** ppDevice,
		D3D_FEATURE_LEVEL* pFeatureLevel,
		ID3D11DeviceContext** ppImmediateContext);

	/** @brief CreateDXGIFactory implemented against the system DXGI. */
	HRESULT WINAPI CreateFactory(REFIID riid, void** ppFactory);

	/** @brief The D3D12 device backing the translation layer, for upscaling. */
	ID3D12Device* GetDevice();

	/** @brief The direct queue the swap chain and all translated work run on. */
	ID3D12CommandQueue* GetCommandQueue();

	/** @brief The D3D11On12 device, for wrapping resources the upscalers need. */
	ID3D11On12Device* GetD3D11On12Device();
}
