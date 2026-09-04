#include "DX12SwapChain.h"

#include <FidelityFX/api/include/dx12/ffx_api_dx12.hpp>
#include <algorithm>
#include <cmath>
#include <dxgi1_6.h>

#include "../HDRDisplay.h"
#include "../Upscaling.h"
#include "FidelityFX.h"
#include "Streamline.h"

namespace
{
	/**
	 * @brief XeLL sleep interval that keeps the *presented* frame rate at the refresh rate.
	 *
	 * XeLL throttles the rendered frame rate, and frame generation multiplies that into the
	 * presented rate, so the interval has to scale with the multiplier or the display would
	 * receive multiplier times refresh rate frames.
	 */
	uint32_t ComputeXeLLMinimumIntervalUs()
	{
		auto& upscaling = globals::features::upscaling;
		if (!upscaling.settings.frameLimitMode || upscaling.refreshRate <= 0.0)
			return 0;

		const uint32_t interpolatedFrames = upscaling.intelXeSSFrameGeneration.GetNumInterpolatedFrames();
		const uint32_t multiplier = interpolatedFrames ?
		                                interpolatedFrames + 1 :
		                                std::clamp<uint32_t>(
											upscaling.settings.frameGenerationMultiplier,
											Upscaling::kMinFrameGenerationMultiplier,
											Upscaling::kMaxFrameGenerationMultiplier);
		return static_cast<uint32_t>(std::lround(1000000.0 * multiplier / upscaling.refreshRate));
	}

	/**
	 * @brief Unbinds the D3D11 output-merger targets for the duration of a scope.
	 *
	 * The present chain re-binds kFRAMEBUFFER's RTV right before handing over, and with frame
	 * generation that RTV is the UI texture. D3D11 forces a shader-resource bind to NULL while
	 * the same resource is bound as an output, so any compute pass here that reads the UI
	 * texture (UICompositeCS) would silently read zeros. Restores the previous targets on exit
	 * so whatever the game expects bound after Present still is.
	 */
	class ScopedRenderTargetUnbind
	{
	public:
		explicit ScopedRenderTargetUnbind(ID3D11DeviceContext* context) :
			context_(context)
		{
			context_->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRTVs_, &savedDSV_);
			ID3D11RenderTargetView* nullRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
			context_->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, nullRTVs, nullptr);
		}

		~ScopedRenderTargetUnbind()
		{
			context_->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRTVs_, savedDSV_);
			for (auto* rtv : savedRTVs_) {
				if (rtv)
					rtv->Release();
			}
			if (savedDSV_)
				savedDSV_->Release();
		}

		ScopedRenderTargetUnbind(const ScopedRenderTargetUnbind&) = delete;
		ScopedRenderTargetUnbind& operator=(const ScopedRenderTargetUnbind&) = delete;

	private:
		ID3D11DeviceContext* context_;
		ID3D11RenderTargetView* savedRTVs_[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
		ID3D11DepthStencilView* savedDSV_ = nullptr;
	};
}

void DX12SwapChain::CreateD3D12Device(IDXGIAdapter* a_adapter)
{
	DX::ThrowIfFailed(D3D12CreateDevice(a_adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&d3d12Device)));

	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	queueDesc.NodeMask = 0;

	DX::ThrowIfFailed(d3d12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)));

	for (int i = 0; i < 2; i++) {
		DX::ThrowIfFailed(d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocators[i])));
		DX::ThrowIfFailed(d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocators[i].get(), nullptr, IID_PPV_ARGS(&commandLists[i])));
		commandLists[i]->Close();
	}

	DX::ThrowIfFailed(d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(xessCommandAllocator.put())));
	DX::ThrowIfFailed(d3d12Device->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		xessCommandAllocator.get(),
		nullptr,
		IID_PPV_ARGS(xessCommandList.put())));
	DX::ThrowIfFailed(xessCommandList->Close());
}

bool DX12SwapChain::CreateSwapChain(IDXGIAdapter* adapter, DXGI_SWAP_CHAIN_DESC a_swapChainDesc, bool a_enableFrameGenerationProvider)
{
	CreateD3D12Device(adapter);

	winrt::com_ptr<IDXGIFactory4> dxgiFactory;
	DX::ThrowIfFailed(adapter->GetParent(IID_PPV_ARGS(dxgiFactory.put())));

	// Runtime format negotiation for swap chain
	DXGI_FORMAT attemptedFormat = a_enableFrameGenerationProvider ?
	                                  DXGI_FORMAT_R10G10B10A2_UNORM :
	                                  a_swapChainDesc.BufferDesc.Format;
	DXGI_FORMAT negotiatedFormat = attemptedFormat;
	bool fallbackUsed = false;

	// Validate the selected swap-chain format as a D3D12 render target.
	D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport = { attemptedFormat, D3D12_FORMAT_SUPPORT1_RENDER_TARGET, D3D12_FORMAT_SUPPORT2_NONE };
	if (FAILED(d3d12Device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &formatSupport, sizeof(formatSupport))) ||
		(formatSupport.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) == 0) {
		logger::warn("[DX12SwapChain] Format {} is not supported as a render target, falling back to R8G8B8A8_UNORM", static_cast<uint32_t>(attemptedFormat));
		negotiatedFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		fallbackUsed = true;
	}

	logger::info("[DX12SwapChain] Swap chain format negotiation: attempted={}, negotiated={}, fallback={}",
		static_cast<uint32_t>(attemptedFormat),
		static_cast<uint32_t>(negotiatedFormat),
		fallbackUsed ? "true" : "false");

	swapChainDesc = {};
	swapChainDesc.Width = a_swapChainDesc.BufferDesc.Width;
	swapChainDesc.Height = a_swapChainDesc.BufferDesc.Height;
	swapChainDesc.Format = negotiatedFormat;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 2;
	swapChainDesc.SwapEffect = a_swapChainDesc.SwapEffect;
	swapChainDesc.Flags = a_swapChainDesc.Flags;

	auto& upscaling = globals::features::upscaling;
	if (a_enableFrameGenerationProvider && upscaling.settings.frameGenerationMode == static_cast<uint>(Upscaling::FrameGenerationMethod::kXESS)) {
		swapChainBackend = SwapChainBackend::XeSS;

		IntelXeSSFrameGeneration::CreateInfo createInfo{};
		createInfo.device = d3d12Device.get();
		createInfo.commandQueue = commandQueue.get();
		createInfo.factory = dxgiFactory.get();
		createInfo.window = a_swapChainDesc.OutputWindow;
		createInfo.swapChainDesc = swapChainDesc;
		createInfo.hudlessFormat = swapChainDesc.Format;
		createInfo.uiFormat = swapChainDesc.Format;
		createInfo.initFlags = XEFG_SWAPCHAIN_INIT_FLAG_NONE;
		createInfo.uiMode = static_cast<xefg_swapchain_ui_mode_t>(std::min<uint>(
			upscaling.settings.frameGenerationXeSSUIMode,
			static_cast<uint>(XEFG_SWAPCHAIN_UI_MODE_BACKBUFFER_HUDLESS_UITEXTURE)));
		// Initialize with the adapter maximum so the multiplier can be changed without a restart.
		createInfo.maxInterpolatedFrames = XEFG_SWAPCHAIN_USE_MAX_SUPPORTED_INTERPOLATED_FRAMES;
		if (!upscaling.intelXeSSFrameGeneration.CreateContextAndSwapChain(createInfo)) {
			logger::error("[XeSS-FG] Failed to create the XeSS-FG/XeLL proxy swap chain");
			return false;
		}

		swapChain = upscaling.intelXeSSFrameGeneration.GetSwapChain();
		if (!swapChain) {
			logger::error("[XeSS-FG] SDK returned no proxy swap chain");
			upscaling.intelXeSSFrameGeneration.Shutdown();
			return false;
		}
		// GetSwapChain returns a borrowed pointer. CreateInterop transfers this
		// owned reference into the outer DXGI proxy.
		swapChain->AddRef();
		// The SDK defaults to its initialization maximum, so apply the configured multiplier
		// before the first present.
		const uint32_t initialMultiplier = std::clamp<uint32_t>(
			upscaling.settings.frameGenerationMultiplier,
			Upscaling::kMinFrameGenerationMultiplier,
			Upscaling::kMaxFrameGenerationMultiplier);
		upscaling.intelXeSSFrameGeneration.SetNumInterpolatedFrames(initialMultiplier - 1);
		const uint32_t minimumIntervalUs = ComputeXeLLMinimumIntervalUs();
		if (!upscaling.intelXeSSFrameGeneration.SetEnabled(true, minimumIntervalUs)) {
			logger::error("[XeSS-FG] Failed to enable XeLL before the first frame");
			swapChain->Release();
			swapChain = nullptr;
			upscaling.intelXeSSFrameGeneration.Shutdown();
			return false;
		}
	} else if (a_enableFrameGenerationProvider && upscaling.settings.frameGenerationMode == static_cast<uint>(Upscaling::FrameGenerationMethod::kFSR)) {
		swapChainBackend = SwapChainBackend::FidelityFX;

		ffx::CreateContextDescFrameGenerationSwapChainForHwndDX12 ffxSwapChainDesc{};
		ffxSwapChainDesc.desc = &swapChainDesc;
		ffxSwapChainDesc.dxgiFactory = dxgiFactory.get();
		ffxSwapChainDesc.fullscreenDesc = nullptr;
		ffxSwapChainDesc.gameQueue = commandQueue.get();
		ffxSwapChainDesc.hwnd = a_swapChainDesc.OutputWindow;
		ffxSwapChainDesc.swapchain = &swapChain;

		auto& fidelityFX = upscaling.fidelityFX;
		if (ffx::CreateContext(fidelityFX.swapChainContext, nullptr, ffxSwapChainDesc) != ffx::ReturnCode::Ok) {
			logger::critical("[FidelityFX] Failed to create swap chain context!");
			return false;
		}
	} else {
		swapChainBackend = SwapChainBackend::Native;
		winrt::com_ptr<IDXGISwapChain1> nativeSwapChain;
		const HRESULT createResult = dxgiFactory->CreateSwapChainForHwnd(
			commandQueue.get(),
			a_swapChainDesc.OutputWindow,
			&swapChainDesc,
			nullptr,
			nullptr,
			nativeSwapChain.put());
		if (FAILED(createResult)) {
			logger::error("[DX12SwapChain] Failed to create native D3D12 interop swap chain (0x{:08X})", static_cast<uint32_t>(createResult));
			return false;
		}
		auto nativeSwapChain4 = nativeSwapChain.as<IDXGISwapChain4>();
		swapChain = nativeSwapChain4.detach();
		logger::info("[DX12SwapChain] Created native D3D12 swap chain for cross-vendor XeSS-SR interop");
	}

	DX::ThrowIfFailed(swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainBuffers[0])));
	DX::ThrowIfFailed(swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainBuffers[1])));

	frameIndex = swapChain->GetCurrentBackBufferIndex();

	// Set color space based on HDR Display feature state and negotiated format
	auto* hdr = globals::features::hdrDisplay.loaded ? &globals::features::hdrDisplay : nullptr;
	bool enableHDR = hdr && hdr->settings.enableHDR;
	// Only set HDR color space if not falling back to SDR format
	SetColorSpace(enableHDR && !fallbackUsed);

	if (swapChainBackend == SwapChainBackend::FidelityFX)
		upscaling.fidelityFX.SetupFrameGeneration();

	return true;
}

void DX12SwapChain::CreateInterop()
{
	HANDLE sharedFenceHandle;
	DX::ThrowIfFailed(d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&d3d12Fence)));
	DX::ThrowIfFailed(d3d12Device->CreateSharedHandle(d3d12Fence.get(), nullptr, GENERIC_ALL, nullptr, &sharedFenceHandle));
	DX::ThrowIfFailed(d3d11Device->OpenSharedFence(sharedFenceHandle, IID_PPV_ARGS(&d3d11Fence)));
	CloseHandle(sharedFenceHandle);

	swapChainProxy.attach(new DXGISwapChainProxy(swapChain));

	RecreateWrappedResources(swapChainDesc);
}

void DX12SwapChain::RecreateWrappedResources(const DXGI_SWAP_CHAIN_DESC1& desc)
{
	D3D11_TEXTURE2D_DESC texDesc11{};
	texDesc11.Width = desc.Width;
	texDesc11.Height = desc.Height;
	texDesc11.MipLevels = 1;
	texDesc11.ArraySize = 1;
	texDesc11.Format = desc.Format;
	texDesc11.SampleDesc.Count = 1;
	texDesc11.SampleDesc.Quality = 0;
	texDesc11.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;

	// Build both replacements before releasing the active resources so a failed
	// allocation cannot leave the proxy with only half of its interop textures.
	auto newSwapChainBuffer = std::make_unique<WrappedResource>(texDesc11, d3d11Device.get(), d3d12Device.get());
	Util::SetResourceName(newSwapChainBuffer->resource11, "FrameGeneration::HUDLessColor");
	newSwapChainBuffer->resource->SetName(L"FrameGeneration::HUDLessColor D3D12");

	// FidelityFX accepts the existing SDR UI surface. XeSS-FG requires UI,
	// HUD-less color, and the backbuffer to use the exact same format/color space.
	if (swapChainBackend == SwapChainBackend::FidelityFX)
		texDesc11.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	auto newUiBuffer = std::make_unique<WrappedResource>(texDesc11, d3d11Device.get(), d3d12Device.get());
	Util::SetResourceName(newUiBuffer->resource11, "FrameGeneration::UI");
	newUiBuffer->resource->SetName(L"FrameGeneration::UI D3D12");

	// Only the XeSS backend presents a separately composited frame; see CompositeFrameGenerationUI.
	std::unique_ptr<WrappedResource> newPresentBuffer;
	if (swapChainBackend == SwapChainBackend::XeSS) {
		texDesc11.Format = desc.Format;
		newPresentBuffer = std::make_unique<WrappedResource>(texDesc11, d3d11Device.get(), d3d12Device.get());
		Util::SetResourceName(newPresentBuffer->resource11, "FrameGeneration::PresentComposite");
		newPresentBuffer->resource->SetName(L"FrameGeneration::PresentComposite D3D12");
	}

	delete swapChainBufferWrapped;
	delete uiBufferWrapped;
	delete presentBufferWrapped;
	swapChainBufferWrapped = newSwapChainBuffer.release();
	uiBufferWrapped = newUiBuffer.release();
	presentBufferWrapped = newPresentBuffer.release();

	const float clearColor[4]{};
	d3d11Context->ClearRenderTargetView(swapChainBufferWrapped->rtv, clearColor);
	d3d11Context->ClearRenderTargetView(uiBufferWrapped->rtv, clearColor);
	if (presentBufferWrapped)
		d3d11Context->ClearRenderTargetView(presentBufferWrapped->rtv, clearColor);
}

DXGISwapChainProxy* DX12SwapChain::GetSwapChainProxy()
{
	auto* proxy = swapChainProxy.get();
	if (proxy)
		proxy->AddRef();
	return proxy;
}

void DX12SwapChain::SetD3D11Device(ID3D11Device* a_d3d11Device)
{
	DX::ThrowIfFailed(a_d3d11Device->QueryInterface(IID_PPV_ARGS(&d3d11Device)));
}

void DX12SwapChain::SetD3D11DeviceContext(ID3D11DeviceContext* a_d3d11Context)
{
	DX::ThrowIfFailed(a_d3d11Context->QueryInterface(IID_PPV_ARGS(&d3d11Context)));
}

HRESULT DX12SwapChain::GetBuffer(UINT buffer, REFIID riid, void** ppSurface)
{
	if (!ppSurface)
		return E_POINTER;

	*ppSurface = nullptr;
	if (buffer != 0 || !swapChainBufferWrapped || !swapChainBufferWrapped->resource11)
		return DXGI_ERROR_INVALID_CALL;

	// IDXGISwapChain::GetBuffer returns an owned COM reference. Returning the raw
	// pointer here let the caller's Release destroy the shared texture while the
	// D3D12 side still retained and submitted its corresponding resource.
	return swapChainBufferWrapped->resource11->QueryInterface(riid, ppSurface);
}

HRESULT DX12SwapChain::ResizeBuffers(UINT bufferCount, UINT width, UINT height, DXGI_FORMAT format, UINT flags)
{
	return ResizeBuffersInternal(bufferCount, width, height, format, flags, nullptr, nullptr, false);
}

HRESULT DX12SwapChain::ResizeBuffers1(UINT bufferCount, UINT width, UINT height, DXGI_FORMAT format, UINT flags, const UINT* creationNodeMask, IUnknown* const* presentQueue)
{
	return ResizeBuffersInternal(bufferCount, width, height, format, flags, creationNodeMask, presentQueue, true);
}

HRESULT DX12SwapChain::ResizeBuffersInternal(
	UINT bufferCount,
	UINT width,
	UINT height,
	DXGI_FORMAT format,
	UINT flags,
	const UINT* creationNodeMask,
	IUnknown* const* presentQueue,
	bool useResizeBuffers1)
{
	if (!swapChain)
		return DXGI_ERROR_INVALID_CALL;

	// DXGI defines zero as "preserve the current buffer count". FidelityFX's
	// frame-generation swap-chain stores the supplied value verbatim and uses it
	// as its replacement-buffer count, so forwarding zero leaves it with no valid
	// source resource at the next Present.
	const UINT effectiveBufferCount = bufferCount ? bufferCount : swapChainDesc.BufferCount;
	if (!bufferCount)
		logger::warn("[FidelityFX] Normalized ResizeBuffers count from 0 to {} to preserve replacement buffers", effectiveBufferCount);
	if (effectiveBufferCount != 2) {
		logger::error("[DX12SwapChain] Rejected unsupported resize buffer count {} (CS requires 2)", effectiveBufferCount);
		return DXGI_ERROR_UNSUPPORTED;
	}

	// XeSS-FG guide: before ResizeBuffers the application must wait for the command lists that
	// reference proxy-owned resources; our per-frame copy targets the proxy back buffers.
	if (!WaitForIdle())
		logger::warn("[DX12SwapChain] Could not drain the GPU before ResizeBuffers");

	// These references are to provider replacement buffers. They must not keep
	// the old generation alive across the provider's resize, and must be refreshed
	// before CS records another copy.
	swapChainBuffers[0] = nullptr;
	swapChainBuffers[1] = nullptr;
	HRESULT result;
	// ResizeBuffers1 describes its queue arrays using the caller's BufferCount.
	// When that count is zero, preserve the existing queues through the legacy
	// resize entry point instead of making the provider read zero-length arrays
	// as if they contained effectiveBufferCount entries.
	if (useResizeBuffers1 && bufferCount)
		result = swapChain->ResizeBuffers1(effectiveBufferCount, width, height, format, flags, creationNodeMask, presentQueue);
	else
		result = swapChain->ResizeBuffers(effectiveBufferCount, width, height, format, flags);
	if (FAILED(result))
		return result;

	DXGI_SWAP_CHAIN_DESC1 resizedDesc{};
	const HRESULT descResult = swapChain->GetDesc1(&resizedDesc);
	if (FAILED(descResult))
		return descResult;

	const bool wrappedResourcesChanged = resizedDesc.Width != swapChainDesc.Width ||
	                                     resizedDesc.Height != swapChainDesc.Height ||
	                                     resizedDesc.Format != swapChainDesc.Format;
	if (wrappedResourcesChanged)
		RecreateWrappedResources(resizedDesc);
	swapChainDesc = resizedDesc;
	if (swapChainBackend == SwapChainBackend::XeSS &&
		!globals::features::upscaling.intelXeSSFrameGeneration.OnResize(resizedDesc.Width, resizedDesc.Height, resizedDesc.Format)) {
		logger::warn("[XeSS-FG] Resize completed, but the SDK rejected its resize-state update; interpolation will reset or remain disabled");
	}

	DX::ThrowIfFailed(swapChain->GetBuffer(0, IID_PPV_ARGS(swapChainBuffers[0].put())));
	DX::ThrowIfFailed(swapChain->GetBuffer(1, IID_PPV_ARGS(swapChainBuffers[1].put())));
	frameIndex = swapChain->GetCurrentBackBufferIndex();
	return S_OK;
}

HRESULT DX12SwapChain::Present(UINT SyncInterval, UINT Flags)
{
	return PresentInternal(SyncInterval, Flags, nullptr);
}

HRESULT DX12SwapChain::Present1(UINT syncInterval, UINT flags, const DXGI_PRESENT_PARAMETERS* presentParameters)
{
	if (!presentParameters)
		return E_INVALIDARG;
	return PresentInternal(syncInterval, flags, presentParameters);
}

HRESULT DX12SwapChain::PresentInternal(UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* presentParameters)
{
	auto& upscaling = globals::features::upscaling;

	// Scale UI brightness BEFORE fence sync so the D3D11 UIBrightnessCS dispatch
	// is covered by the D3D11→D3D12 fence. Without this, FidelityFX may read
	// uiBufferWrapped on D3D12 before the PQ encoding completes on D3D11.
	// Only runs when HDR Display feature is loaded (UIBrightnessCS may not exist otherwise)
	auto* hdr = globals::features::hdrDisplay.loaded ? &globals::features::hdrDisplay : nullptr;

	// Must precede every D3D11 compute pass in this function; see ScopedRenderTargetUnbind.
	ScopedRenderTargetUnbind renderTargetUnbind(d3d11Context.get());

	if (hdr)
		hdr->ScaleUIBrightnessForFG();

	bool isHDR = hdr && hdr->settings.enableHDR;

	const bool frameGenerationThisFrame = upscaling.ShouldUseFrameGenerationThisFrame();

	// See CompositeFrameGenerationUI: the XeSS backend must present a UI-composited frame.
	const bool compositedPresent = swapChainBackend == SwapChainBackend::XeSS &&
	                               frameGenerationThisFrame &&
	                               (presentBufferValid || upscaling.CompositeFrameGenerationUI());
	presentBufferValid = false;

	// Wait for D3D11 to finish (includes ApplyHDR scene encoding, UIBrightnessCS and the UI composite)
	fenceValue++;
	DX::ThrowIfFailed(d3d11Context->Signal(d3d11Fence.get(), fenceValue));
	DX::ThrowIfFailed(commandQueue->Wait(d3d12Fence.get(), fenceValue));

	// New frame, reset
	if (frameFenceValues[frameIndex])
		DX::ThrowIfFailed(d3d12Fence->SetEventOnCompletion(frameFenceValues[frameIndex], nullptr));
	DX::ThrowIfFailed(commandAllocators[frameIndex]->Reset());
	DX::ThrowIfFailed(commandLists[frameIndex]->Reset(commandAllocators[frameIndex].get(), nullptr));

	// Copy shared texture to swap chain buffer
	{
		auto fakeSwapChain = (compositedPresent ? presentBufferWrapped : swapChainBufferWrapped)->resource.get();
		auto realSwapChain = swapChainBuffers[frameIndex].get();
		{
			std::vector<D3D12_RESOURCE_BARRIER> barriers;
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(fakeSwapChain, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE));
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(realSwapChain, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST));
			commandLists[frameIndex]->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
		}

		commandLists[frameIndex]->CopyResource(realSwapChain, fakeSwapChain);

		{
			std::vector<D3D12_RESOURCE_BARRIER> barriers;
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(fakeSwapChain, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON));
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(realSwapChain, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT));
			commandLists[frameIndex]->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
		}
	}

	if (swapChainBackend == SwapChainBackend::XeSS) {
		const uint32_t requestedMultiplier = std::clamp<uint32_t>(
			upscaling.settings.frameGenerationMultiplier,
			Upscaling::kMinFrameGenerationMultiplier,
			Upscaling::kMaxFrameGenerationMultiplier);
		// No-ops when unchanged; the backend only reconfigures the SDK on an actual change.
		upscaling.intelXeSSFrameGeneration.SetNumInterpolatedFrames(requestedMultiplier - 1);

		// XeLL integration checklist: "application must ensure any GPU activity is finished before
		// xellSetSleepMode is called". That covers pause, resume and frame-limit changes alike;
		// each happens once per settings or menu transition, never per frame. Calling it mid-frame
		// with the GPU busy coincided with device removal when XeSS-SR shared the adapter.
		const uint32_t minimumIntervalUs = ComputeXeLLMinimumIntervalUs();
		if (upscaling.intelXeSSFrameGeneration.SleepModeWouldChange(frameGenerationThisFrame, minimumIntervalUs)) {
			if (!WaitForIdle())
				logger::warn("[XeSS-FG] Could not drain the GPU before changing the XeLL sleep mode");
		}
		upscaling.intelXeSSFrameGeneration.SetEnabled(frameGenerationThisFrame, minimumIntervalUs);

		auto viewMatrix = globals::game::frameBufferCached.GetCameraView().Transpose();
		auto projectionMatrix = globals::game::frameBufferCached.GetCameraProjUnjittered().Transpose();
		const auto renderSize = float2{
			static_cast<float>(swapChainDesc.Width) * upscaling.resolutionScale.x,
			static_cast<float>(swapChainDesc.Height) * upscaling.resolutionScale.y
		};

		IntelXeSSFrameGeneration::FrameData frameData{};
		// UNTIL_NEXT_PRESENT is the SDK's recommended mode: it keeps references and the SDK
		// guide explicitly permits overwriting the resources once the matching Present has
		// returned, which is when the clears at the end of this function run. ONLY_NOW made the
		// SDK record copies into this command list instead; those copies were still in flight
		// on the GPU when a menu pause called xefgSwapChainSetEnabled(0), which coincided with
		// device removal whenever XeSS-SR was also active.
		frameData.taggingCommandList = nullptr;
		frameData.hudlessColor = swapChainBufferWrapped ? swapChainBufferWrapped->resource.get() : nullptr;
		frameData.uiTexture = uiBufferWrapped ? uiBufferWrapped->resource.get() : nullptr;
		frameData.depth = depthBufferShared12 ? depthBufferShared12->resource.get() : nullptr;
		frameData.motionVectors = motionVectorBufferShared12 ? motionVectorBufferShared12->resource.get() : nullptr;
		frameData.renderWidth = static_cast<uint32_t>(renderSize.x);
		frameData.renderHeight = static_cast<uint32_t>(renderSize.y);
		frameData.displayWidth = swapChainDesc.Width;
		frameData.displayHeight = swapChainDesc.Height;
		frameData.viewMatrix = &viewMatrix._11;
		frameData.projectionMatrix = &projectionMatrix._11;
		frameData.jitterOffsetX = -upscaling.jitter.x;
		frameData.jitterOffsetY = -upscaling.jitter.y;
		frameData.motionVectorScaleX = renderSize.x;
		frameData.motionVectorScaleY = renderSize.y;
		frameData.frameRenderTimeMs = RE::GetSecondsSinceLastFrame() * 1000.0f;
		frameData.resetHistory = upscaling.pendingXeSSFrameGenerationReset.exchange(false, std::memory_order_acq_rel);
		frameData.resourceValidity = XEFG_SWAPCHAIN_RV_UNTIL_NEXT_PRESENT;
		frameData.hudlessState = D3D12_RESOURCE_STATE_COMMON;
		frameData.uiState = D3D12_RESOURCE_STATE_COMMON;
		frameData.depthState = D3D12_RESOURCE_STATE_COMMON;
		frameData.motionVectorState = D3D12_RESOURCE_STATE_COMMON;
		upscaling.intelXeSSFrameGeneration.TagFrame(frameData);
	} else if (swapChainBackend == SwapChainBackend::FidelityFX) {
		upscaling.fidelityFX.Present(upscaling.ShouldUseFrameGenerationThisFrame(), isHDR);
	}

	DX::ThrowIfFailed(commandLists[frameIndex]->Close());

	ID3D12CommandList* commandListsToExecute[] = { commandLists[frameIndex].get() };
	commandQueue->ExecuteCommandLists(1, commandListsToExecute);

	// Present the frame. Present1 must take the same interception path so callers
	// cannot bypass frame generation after querying a newer swap-chain interface.
	if (swapChainBackend == SwapChainBackend::XeSS)
		upscaling.intelXeSSFrameGeneration.BeforePresent();
	HRESULT presentResult = S_OK;
	if (presentParameters)
		presentResult = swapChain->Present1(SyncInterval, Flags, presentParameters);
	else
		presentResult = swapChain->Present(SyncInterval, Flags);
	if (swapChainBackend == SwapChainBackend::XeSS)
		upscaling.intelXeSSFrameGeneration.AfterPresent(presentResult);
	if (FAILED(presentResult)) {
		// DEVICE_REMOVED on its own says nothing; the removal reason is what separates a GPU
		// hang from a driver fault from an invalid call on our side. Log both devices, since
		// either side of the interop can be the one that died.
		const HRESULT d3d12Reason = d3d12Device ? d3d12Device->GetDeviceRemovedReason() : S_OK;
		const HRESULT d3d11Reason = d3d11Device ? d3d11Device->GetDeviceRemovedReason() : S_OK;
		logger::critical(
			"[DX12SwapChain] Present failed with 0x{:08X}; device removed reason D3D12=0x{:08X} D3D11=0x{:08X} (backend {}, frame generation {}, present source {})",
			static_cast<uint32_t>(presentResult),
			static_cast<uint32_t>(d3d12Reason),
			static_cast<uint32_t>(d3d11Reason),
			static_cast<int>(swapChainBackend),
			frameGenerationThisFrame,
			compositedPresent ? "composited" : "hudless");
	}
	DX::ThrowIfFailed(presentResult);

	// Wait for D3D12 to finish
	fenceValue++;
	DX::ThrowIfFailed(commandQueue->Signal(d3d12Fence.get(), fenceValue));
	frameFenceValues[frameIndex] = fenceValue;
	DX::ThrowIfFailed(d3d11Context->Wait(d3d11Fence.get(), fenceValue));

	// Update the frame index
	frameIndex = swapChain->GetCurrentBackBufferIndex();

	float clearColor[4]{ 0, 0, 0, 0 };
	d3d11Context->ClearRenderTargetView(swapChainBufferWrapped->rtv, clearColor);
	d3d11Context->ClearRenderTargetView(uiBufferWrapped->rtv, clearColor);

	// If VSync is disabled, use frame limiter to prevent tearing and optimise pacing
	if (SyncInterval == 0)
		upscaling.FrameLimiter();

	return S_OK;
}

HRESULT DX12SwapChain::GetDevice(REFIID uuid, void** ppDevice)
{
	if (!ppDevice)
		return E_POINTER;

	*ppDevice = nullptr;
	if (uuid == __uuidof(ID3D11Device) || uuid == __uuidof(ID3D11Device1) || uuid == __uuidof(ID3D11Device2) || uuid == __uuidof(ID3D11Device3) || uuid == __uuidof(ID3D11Device4) || uuid == __uuidof(ID3D11Device5)) {
		return d3d11Device ? d3d11Device->QueryInterface(uuid, ppDevice) : E_NOINTERFACE;
	}

	return swapChain ? swapChain->GetDevice(uuid, ppDevice) : DXGI_ERROR_INVALID_CALL;
}

HANDLE DX12SwapChain::GetFrameLatencyWaitableObject()
{
	return swapChain->GetFrameLatencyWaitableObject();
}

float DX12SwapChain::GetFrameTime() const
{
	// Calculate frame time based on swap chain presentation
	static float lastPresentTime = 0.0f;
	static float frameTime = 1.0f / 60.0f;  // Default to 60 fps
	static LARGE_INTEGER frequency = {};
	static LARGE_INTEGER currentTime = {};

	if (frequency.QuadPart == 0) {
		QueryPerformanceFrequency(&frequency);
	}

	QueryPerformanceCounter(&currentTime);
	float time = static_cast<float>(currentTime.QuadPart) / static_cast<float>(frequency.QuadPart);

	if (lastPresentTime > 0.0f) {
		frameTime = time - lastPresentTime;
	}
	lastPresentTime = time;

	return frameTime;
}

WrappedResource::WrappedResource(D3D11_TEXTURE2D_DESC a_texDesc, ID3D11Device5* a_d3d11Device, ID3D12Device* a_d3d12Device)
{
	// Create D3D11 shared texture directly instead of wrapping D3D12 resource
	a_texDesc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
	DX::ThrowIfFailed(a_d3d11Device->CreateTexture2D(&a_texDesc, nullptr, &resource11));

	// Get shared handle from D3D11 texture to enable D3D12 access
	winrt::com_ptr<IDXGIResource1> dxgiResource;
	DX::ThrowIfFailed(resource11->QueryInterface(IID_PPV_ARGS(dxgiResource.put())));
	HANDLE sharedHandle = nullptr;
	DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &sharedHandle));

	// Open the shared D3D11 texture as D3D12 resource
	DX::ThrowIfFailed(a_d3d12Device->OpenSharedHandle(sharedHandle, IID_PPV_ARGS(resource.put())));
	CloseHandle(sharedHandle);

	if (a_texDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE) {
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = a_texDesc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;

		DX::ThrowIfFailed(a_d3d11Device->CreateShaderResourceView(resource11, &srvDesc, &srv));
	}

	if (a_texDesc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) {
		if (a_texDesc.ArraySize > 1) {
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = a_texDesc.Format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
			uavDesc.Texture2DArray.FirstArraySlice = 0;
			uavDesc.Texture2DArray.ArraySize = a_texDesc.ArraySize;

			DX::ThrowIfFailed(a_d3d11Device->CreateUnorderedAccessView(resource11, &uavDesc, &uav));
		} else {
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = a_texDesc.Format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = 0;

			DX::ThrowIfFailed(a_d3d11Device->CreateUnorderedAccessView(resource11, &uavDesc, &uav));
		}
	}

	if (a_texDesc.BindFlags & D3D11_BIND_RENDER_TARGET) {
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = a_texDesc.Format;
		rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		rtvDesc.Texture2D.MipSlice = 0;
		DX::ThrowIfFailed(a_d3d11Device->CreateRenderTargetView(resource11, &rtvDesc, &rtv));
	}
}

WrappedResource::~WrappedResource()
{
	if (resource11) {
		resource11->Release();
		resource11 = nullptr;
	}
	if (srv) {
		srv->Release();
		srv = nullptr;
	}
	if (uav) {
		uav->Release();
		uav = nullptr;
	}
	if (rtv) {
		rtv->Release();
		rtv = nullptr;
	}
	// resource (winrt::com_ptr) will be automatically released
}

DXGISwapChainProxy::DXGISwapChainProxy(IDXGISwapChain4* a_swapChain)
{
	// The provider returns an owned swap-chain reference. Transfer that reference
	// into the proxy; DX12SwapChain::swapChain remains a non-owning convenience
	// pointer whose lifetime is anchored by this proxy.
	swapChain.attach(a_swapChain);
}

/****IUknown****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::QueryInterface(REFIID riid, void** ppvObj)
{
	if (!ppvObj)
		return E_POINTER;

	*ppvObj = nullptr;
	if (riid == __uuidof(IUnknown) ||
		riid == __uuidof(IDXGIObject) ||
		riid == __uuidof(IDXGIDeviceSubObject) ||
		riid == __uuidof(IDXGISwapChain) ||
		riid == __uuidof(IDXGISwapChain1) ||
		riid == __uuidof(IDXGISwapChain2) ||
		riid == __uuidof(IDXGISwapChain3) ||
		riid == __uuidof(IDXGISwapChain4)) {
		*ppvObj = static_cast<IDXGISwapChain4*>(this);
		AddRef();
		return S_OK;
	}

	return swapChain ? swapChain->QueryInterface(riid, ppvObj) : E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE DXGISwapChainProxy::AddRef()
{
	return referenceCount.fetch_add(1, std::memory_order_relaxed) + 1;
}

ULONG STDMETHODCALLTYPE DXGISwapChainProxy::Release()
{
	const ULONG remaining = referenceCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
	if (!remaining)
		delete this;
	return remaining;
}

/****IDXGIObject****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetPrivateData(_In_ REFGUID Name, UINT DataSize, _In_reads_bytes_(DataSize) const void* pData)
{
	return swapChain->SetPrivateData(Name, DataSize, pData);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetPrivateDataInterface(_In_ REFGUID Name, _In_opt_ const IUnknown* pUnknown)
{
	return swapChain->SetPrivateDataInterface(Name, pUnknown);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetPrivateData(_In_ REFGUID Name, _Inout_ UINT* pDataSize, _Out_writes_bytes_(*pDataSize) void* pData)
{
	return swapChain->GetPrivateData(Name, pDataSize, pData);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetParent(_In_ REFIID riid, _COM_Outptr_ void** ppParent)
{
	return swapChain->GetParent(riid, ppParent);
}

/****IDXGIDeviceSubObject****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetDevice(_In_ REFIID riid, _COM_Outptr_ void** ppDevice)
{
	return globals::features::upscaling.dx12SwapChain.GetDevice(riid, ppDevice);
}

/****IDXGISwapChain****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::Present(UINT SyncInterval, UINT Flags)
{
	return globals::features::upscaling.dx12SwapChain.Present(SyncInterval, Flags);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetBuffer(UINT buffer, _In_ REFIID riid, _COM_Outptr_ void** ppSurface)
{
	return globals::features::upscaling.dx12SwapChain.GetBuffer(buffer, riid, ppSurface);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetFullscreenState(BOOL Fullscreen, _In_opt_ IDXGIOutput* pTarget)
{
	return swapChain->SetFullscreenState(Fullscreen, pTarget);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetFullscreenState(_Out_opt_ BOOL* pFullscreen, _COM_Outptr_opt_result_maybenull_ IDXGIOutput** ppTarget)
{
	return swapChain->GetFullscreenState(pFullscreen, ppTarget);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetDesc(_Out_ DXGI_SWAP_CHAIN_DESC* pDesc)
{
	return swapChain->GetDesc(pDesc);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
	return globals::features::upscaling.dx12SwapChain.ResizeBuffers(BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::ResizeTarget(_In_ const DXGI_MODE_DESC* pNewTargetParameters)
{
	return swapChain->ResizeTarget(pNewTargetParameters);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetContainingOutput(_COM_Outptr_ IDXGIOutput** ppOutput)
{
	return swapChain->GetContainingOutput(ppOutput);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetFrameStatistics(_Out_ DXGI_FRAME_STATISTICS* pStats)
{
	return swapChain->GetFrameStatistics(pStats);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetLastPresentCount(_Out_ UINT* pLastPresentCount)
{
	return swapChain->GetLastPresentCount(pLastPresentCount);
}

/****IDXGISwapChain1****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetDesc1(_Out_ DXGI_SWAP_CHAIN_DESC1* pDesc)
{
	return swapChain->GetDesc1(pDesc);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetFullscreenDesc(_Out_ DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pDesc)
{
	return swapChain->GetFullscreenDesc(pDesc);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetHwnd(_Out_ HWND* pHwnd)
{
	return swapChain->GetHwnd(pHwnd);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetCoreWindow(_In_ REFIID refiid, _COM_Outptr_ void** ppUnk)
{
	return swapChain->GetCoreWindow(refiid, ppUnk);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::Present1(UINT SyncInterval, UINT PresentFlags, _In_ const DXGI_PRESENT_PARAMETERS* pPresentParameters)
{
	return globals::features::upscaling.dx12SwapChain.Present1(SyncInterval, PresentFlags, pPresentParameters);
}

BOOL STDMETHODCALLTYPE DXGISwapChainProxy::IsTemporaryMonoSupported()
{
	return swapChain->IsTemporaryMonoSupported();
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetRestrictToOutput(_Out_ IDXGIOutput** ppRestrictToOutput)
{
	return swapChain->GetRestrictToOutput(ppRestrictToOutput);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetBackgroundColor(_In_ const DXGI_RGBA* pColor)
{
	return swapChain->SetBackgroundColor(pColor);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetBackgroundColor(_Out_ DXGI_RGBA* pColor)
{
	return swapChain->GetBackgroundColor(pColor);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetRotation(_In_ DXGI_MODE_ROTATION Rotation)
{
	return swapChain->SetRotation(Rotation);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetRotation(_Out_ DXGI_MODE_ROTATION* pRotation)
{
	return swapChain->GetRotation(pRotation);
}

/****IDXGISwapChain2****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetSourceSize(UINT Width, UINT Height)
{
	return swapChain->SetSourceSize(Width, Height);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetSourceSize(_Out_ UINT* pWidth, _Out_ UINT* pHeight)
{
	return swapChain->GetSourceSize(pWidth, pHeight);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetMaximumFrameLatency(UINT MaxLatency)
{
	return swapChain->SetMaximumFrameLatency(MaxLatency);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetMaximumFrameLatency(_Out_ UINT* pMaxLatency)
{
	return swapChain->GetMaximumFrameLatency(pMaxLatency);
}

HANDLE STDMETHODCALLTYPE DXGISwapChainProxy::GetFrameLatencyWaitableObject()
{
	return globals::features::upscaling.dx12SwapChain.GetFrameLatencyWaitableObject();
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetMatrixTransform(const DXGI_MATRIX_3X2_F* pMatrix)
{
	return swapChain->SetMatrixTransform(pMatrix);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetMatrixTransform(_Out_ DXGI_MATRIX_3X2_F* pMatrix)
{
	return swapChain->GetMatrixTransform(pMatrix);
}

/****IDXGISwapChain3****/
UINT STDMETHODCALLTYPE DXGISwapChainProxy::GetCurrentBackBufferIndex()
{
	return swapChain->GetCurrentBackBufferIndex();
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::CheckColorSpaceSupport(_In_ DXGI_COLOR_SPACE_TYPE ColorSpace, _Out_ UINT* pColorSpaceSupport)
{
	return swapChain->CheckColorSpaceSupport(ColorSpace, pColorSpaceSupport);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetColorSpace1(_In_ DXGI_COLOR_SPACE_TYPE ColorSpace)
{
	return swapChain->SetColorSpace1(ColorSpace);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::ResizeBuffers1(
	_In_ UINT BufferCount,
	_In_ UINT Width,
	_In_ UINT Height,
	_In_ DXGI_FORMAT Format,
	_In_ UINT SwapChainFlags,
	_In_reads_(BufferCount) const UINT* pCreationNodeMask,
	_In_reads_(BufferCount) IUnknown* const* ppPresentQueue)
{
	return globals::features::upscaling.dx12SwapChain.ResizeBuffers1(BufferCount, Width, Height, Format, SwapChainFlags, pCreationNodeMask, ppPresentQueue);
}

/****IDXGISwapChain4****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetHDRMetaData(_In_ DXGI_HDR_METADATA_TYPE Type, _In_ UINT Size, _In_reads_opt_(Size) void* pMetaData)
{
	return swapChain->SetHDRMetaData(Type, Size, pMetaData);
}

void DX12SwapChain::SetColorSpace(bool enableHDR)
{
	if (!swapChain)
		return;

	if (enableHDR) {
		swapChain->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
		logger::info("[DX12SwapChain] Set color space to HDR10 (PQ/BT.2020)");
	} else {
		swapChain->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
		logger::info("[DX12SwapChain] Set color space to SDR (sRGB)");
	}
}

DX12SwapChain::BlurResources DX12SwapChain::GetBlurResources() const
{
	BlurResources res;
	if (swapChainBufferWrapped) {
		res.backbufferTex = swapChainBufferWrapped->resource11;
		res.backbufferRTV = swapChainBufferWrapped->rtv;
		res.backbufferSRV = swapChainBufferWrapped->srv;
	}
	if (uiBufferWrapped) {
		res.uiBufferSRV = uiBufferWrapped->srv;
		res.uiBufferRTV = uiBufferWrapped->rtv;
	}
	return res;
}

void DX12SwapChain::CreateSharedResources()
{
	auto renderer = globals::game::renderer;
	auto& upscaling = globals::features::upscaling;

	delete depthBufferShared12;
	delete motionVectorBufferShared12;
	delete xessColorBufferShared12;
	delete xessResponsiveMaskShared12;
	delete xessOutputBufferShared12;
	depthBufferShared12 = nullptr;
	motionVectorBufferShared12 = nullptr;
	xessColorBufferShared12 = nullptr;
	xessResponsiveMaskShared12 = nullptr;
	xessOutputBufferShared12 = nullptr;

	// Create depth buffer
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	D3D11_TEXTURE2D_DESC texDesc{};
	main.texture->GetDesc(&texDesc);
	texDesc.Format = DXGI_FORMAT_R32_FLOAT;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;
	depthBufferShared12 = new WrappedResource(texDesc, d3d11Device.get(), d3d12Device.get());
	Util::SetResourceName(depthBufferShared12->resource11, "D3D12Interop::Depth");
	depthBufferShared12->resource->SetName(L"D3D12Interop::Depth D3D12");

	// Create motion vector buffer
	auto& motionVector = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];
	motionVector.texture->GetDesc(&texDesc);
	// UAV so EncodeTexturesCS can write the motion vectors straight into the shared buffer when
	// native XeSS-SR is active, instead of a separate CopyResource per frame.
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	motionVectorBufferShared12 = new WrappedResource(texDesc, d3d11Device.get(), d3d12Device.get());
	Util::SetResourceName(motionVectorBufferShared12->resource11, "D3D12Interop::MotionVectors");
	motionVectorBufferShared12->resource->SetName(L"D3D12Interop::MotionVectors D3D12");

	if (!upscaling.xessD3D12PathActive)
		return;

	main.texture->GetDesc(&texDesc);
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	xessColorBufferShared12 = new WrappedResource(texDesc, d3d11Device.get(), d3d12Device.get());
	Util::SetResourceName(xessColorBufferShared12->resource11, "XeSSD3D12::InputColor");
	xessColorBufferShared12->resource->SetName(L"XeSSD3D12::InputColor D3D12");

	texDesc.Format = DXGI_FORMAT_R8_UNORM;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	xessResponsiveMaskShared12 = new WrappedResource(texDesc, d3d11Device.get(), d3d12Device.get());
	Util::SetResourceName(xessResponsiveMaskShared12->resource11, "XeSSD3D12::ResponsiveMask");
	xessResponsiveMaskShared12->resource->SetName(L"XeSSD3D12::ResponsiveMask D3D12");

	main.texture->GetDesc(&texDesc);
	texDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	xessOutputBufferShared12 = new WrappedResource(texDesc, d3d11Device.get(), d3d12Device.get());
	Util::SetResourceName(xessOutputBufferShared12->resource11, "XeSSD3D12::OutputColor");
	xessOutputBufferShared12->resource->SetName(L"XeSSD3D12::OutputColor D3D12");
}

bool DX12SwapChain::WaitForIdle()
{
	if (!d3d11Context || !d3d11Fence || !commandQueue || !d3d12Fence)
		return false;

	const UINT64 d3d11Done = ++fenceValue;
	if (FAILED(d3d11Context->Signal(d3d11Fence.get(), d3d11Done)) ||
		FAILED(commandQueue->Wait(d3d12Fence.get(), d3d11Done))) {
		return false;
	}
	d3d11Context->Flush();

	const UINT64 d3d12Done = ++fenceValue;
	if (FAILED(commandQueue->Signal(d3d12Fence.get(), d3d12Done)))
		return false;
	if (d3d12Fence->GetCompletedValue() < d3d12Done &&
		FAILED(d3d12Fence->SetEventOnCompletion(d3d12Done, nullptr))) {
		return false;
	}
	return d3d12Fence->GetCompletedValue() >= d3d12Done;
}

bool DX12SwapChain::ExecuteXeSS(
	ID3D11Resource* a_color,
	ID3D11Resource* a_depth,
	ID3D11Resource* a_motionVectors,
	ID3D11Resource* a_responsiveMask,
	ID3D11Resource* a_output,
	uint32_t a_inputWidth,
	uint32_t a_inputHeight,
	float a_jitterOffsetX,
	float a_jitterOffsetY,
	bool a_resetHistory)
{
	auto& xess = globals::features::upscaling.intelXeSSD3D12;
	if (!xess.initialized || !d3d11Context || !d3d11Fence || !d3d12Fence || !commandQueue ||
		!xessCommandAllocator || !xessCommandList || !a_color || !a_depth || !a_motionVectors ||
		!a_responsiveMask || !a_output || !xessColorBufferShared12 || !depthBufferShared12 ||
		!motionVectorBufferShared12 || !xessResponsiveMaskShared12 || !xessOutputBufferShared12) {
		return false;
	}

	d3d11Context->CopyResource(xessColorBufferShared12->resource11, a_color);
	d3d11Context->CopyResource(depthBufferShared12->resource11, a_depth);
	d3d11Context->CopyResource(motionVectorBufferShared12->resource11, a_motionVectors);
	d3d11Context->CopyResource(xessResponsiveMaskShared12->resource11, a_responsiveMask);

	const UINT64 d3d11Done = ++fenceValue;
	if (FAILED(d3d11Context->Signal(d3d11Fence.get(), d3d11Done)) ||
		FAILED(commandQueue->Wait(d3d12Fence.get(), d3d11Done))) {
		return false;
	}
	d3d11Context->Flush();

	if (xessFenceValue && d3d12Fence->GetCompletedValue() < xessFenceValue &&
		FAILED(d3d12Fence->SetEventOnCompletion(xessFenceValue, nullptr))) {
		return false;
	}
	if (FAILED(xessCommandAllocator->Reset()) || FAILED(xessCommandList->Reset(xessCommandAllocator.get(), nullptr)))
		return false;

	ID3D12Resource* inputs[] = {
		xessColorBufferShared12->resource.get(),
		depthBufferShared12->resource.get(),
		motionVectorBufferShared12->resource.get(),
		xessResponsiveMaskShared12->resource.get()
	};
	std::vector<D3D12_RESOURCE_BARRIER> beginBarriers;
	beginBarriers.reserve(5);
	for (auto* input : inputs) {
		beginBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
			input, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
	}
	beginBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
		xessOutputBufferShared12->resource.get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
	xessCommandList->ResourceBarrier(static_cast<UINT>(beginBarriers.size()), beginBarriers.data());

	const bool succeeded = xess.Upscale(
		xessCommandList.get(),
		xessColorBufferShared12->resource.get(),
		depthBufferShared12->resource.get(),
		motionVectorBufferShared12->resource.get(),
		xessResponsiveMaskShared12->resource.get(),
		xessOutputBufferShared12->resource.get(),
		a_inputWidth,
		a_inputHeight,
		a_jitterOffsetX,
		a_jitterOffsetY,
		a_resetHistory);

	std::vector<D3D12_RESOURCE_BARRIER> endBarriers;
	endBarriers.reserve(6);
	endBarriers.push_back(CD3DX12_RESOURCE_BARRIER::UAV(xessOutputBufferShared12->resource.get()));
	for (auto* input : inputs) {
		endBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
			input, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON));
	}
	endBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
		xessOutputBufferShared12->resource.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON));
	xessCommandList->ResourceBarrier(static_cast<UINT>(endBarriers.size()), endBarriers.data());

	if (FAILED(xessCommandList->Close()))
		return false;
	ID3D12CommandList* lists[] = { xessCommandList.get() };
	commandQueue->ExecuteCommandLists(1, lists);

	const UINT64 d3d12Done = ++fenceValue;
	if (FAILED(commandQueue->Signal(d3d12Fence.get(), d3d12Done)) ||
		FAILED(d3d11Context->Wait(d3d11Fence.get(), d3d12Done))) {
		return false;
	}
	xessFenceValue = d3d12Done;
	if (succeeded)
		d3d11Context->CopyResource(a_output, xessOutputBufferShared12->resource11);
	return succeeded;
}
