#include "XeSS.h"
#include "Utils/FileSystem.h"

#include "State.h"
#include "Upscaling.h"

// Define the static member
std::vector<std::pair<std::string, std::string>> XeSS::dllVersions = {};

void XeSS::LoadXeSS()
{
	std::wstring dllPath = std::wstring(XeSS::PluginDir) + L"\\libxess.dll";
	module = LoadLibrary(dllPath.c_str());

	// Cache all DLL versions in the XeSS directory
	std::filesystem::path pluginDir = std::filesystem::path(XeSS::PluginDir);
	XeSS::dllVersions = Util::EnumerateDllVersions(pluginDir);

	if (module) {
		xessGetVersion = (xessGetVersionPtr)GetProcAddress(module, "xessGetVersion");
		xessD3D12CreateContext = (xessD3D12CreateContextPtr)GetProcAddress(module, "xessD3D12CreateContext");
		xessD3D12Init = (xessD3D12InitPtr)GetProcAddress(module, "xessD3D12Init");
		xessD3D12Execute = (xessD3D12ExecutePtr)GetProcAddress(module, "xessD3D12Execute");
		xessDestroyContext = (xessDestroyContextPtr)GetProcAddress(module, "xessDestroyContext");

		if (xessGetVersion && xessD3D12CreateContext && xessD3D12Init && xessD3D12Execute && xessDestroyContext) {
			featureXeSS = true;
			xess_version_t version;
			if (xessGetVersion(&version) == XESS_RESULT_SUCCESS) {
				logger::info("[XeSS] Successfully loaded XeSS SDK version: {}.{}.{}", version.major, version.minor, version.patch);
			} else {
				logger::info("[XeSS] Successfully loaded XeSS SDK");
			}
		} else {
			featureXeSS = false;
			logger::error("[XeSS] Failed to load XeSS function pointers");
		}
	} else {
		featureXeSS = false;
		logger::error("[XeSS] Failed to load libxess.dll");
	}
}



void XeSS::CreateXeSSResources()
{
	auto upscaling = globals::upscaling;
	if (!featureXeSS || !upscaling->sharedD3D12Device) {
		logger::error("[XeSS] XeSS not available or shared D3D12 device not available, cannot create resources");
		return;
	}

	auto state = globals::state;

	if (xessD3D12CreateContext(upscaling->sharedD3D12Device.get(), &xessContext) != XESS_RESULT_SUCCESS) {
		logger::critical("[XeSS] Failed to create XeSS context!");
		return;
	}

	// Map upscale preset to XeSS quality settings
	// 0=Performance, 1=Balanced, 2=Quality, 3=Native AA
	xess_quality_settings_t xessQuality;
	switch (upscaling->settings.upscalePreset) {
		case 0: xessQuality = XESS_QUALITY_SETTING_PERFORMANCE; break;
		case 1: xessQuality = XESS_QUALITY_SETTING_BALANCED; break;
		case 2: xessQuality = XESS_QUALITY_SETTING_QUALITY; break;
		case 3: xessQuality = XESS_QUALITY_SETTING_AA; break;
		default: xessQuality = XESS_QUALITY_SETTING_QUALITY; break;
	}

	xess_d3d12_init_params_t initParams{};
	initParams.outputResolution.x = (uint32_t)state->screenSize.x;
	initParams.outputResolution.y = (uint32_t)state->screenSize.y;
	initParams.qualitySetting = xessQuality;
	initParams.initFlags = XESS_INIT_FLAG_RESPONSIVE_PIXEL_MASK;
	initParams.creationNodeMask = 1;
	initParams.visibleNodeMask = 1;
	initParams.pTempBufferHeap = nullptr;
	initParams.bufferHeapOffset = 0;
	initParams.pTempTextureHeap = nullptr;
	initParams.textureHeapOffset = 0;
	initParams.pPipelineLibrary = nullptr;

	if (xessD3D12Init(xessContext, &initParams) != XESS_RESULT_SUCCESS) {
		logger::critical("[XeSS] Failed to initialize XeSS context!");
		return;
	}

	logger::info("[XeSS] XeSS context initialized successfully");
	logger::info("[XeSS] Shared textures will be created dynamically during first Upscale() call");
}

void XeSS::DestroyXeSSResources()
{
	if (xessContext && xessDestroyContext) {
		xess_result_t result = xessDestroyContext(xessContext);
		if (result != XESS_RESULT_SUCCESS) {
			logger::error("[XeSS] Failed to destroy XeSS context, error code: {}", (int)result);
		}
		xessContext = nullptr;
	}

	// Clean up XeSS-specific shared textures
	if (inputColorTexture) {
		delete inputColorTexture;
		inputColorTexture = nullptr;
	}
	if (outputColorTexture) {
		delete outputColorTexture;
		outputColorTexture = nullptr;
	}

	// Note: motion vector, depth, and reactive mask textures are now managed by Upscaling system
}

void XeSS::Upscale(ID3D11Resource* a_inputTexture, ID3D11Resource* a_outputTexture, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_motionVectors, ID3D11Resource* a_depth, float2 a_jitter)
{
	auto upscaling = globals::upscaling;
	if (!featureXeSS || !xessContext || !upscaling->sharedD3D12Device) {
		logger::error("[XeSS] XeSS not initialized, cannot upscale");
		return;
	}

	auto state = globals::state;
	auto renderSize = Util::ConvertToDynamic(state->screenSize);

	// Create shared textures dynamically from source textures if not already created
	if (!inputColorTexture) {
		// Get D3D11 device5 interface for WrappedResource creation
		winrt::com_ptr<ID3D11Device5> d3d11Device5;
		if (FAILED(globals::d3d::device->QueryInterface(IID_PPV_ARGS(&d3d11Device5)))) {
			logger::error("[XeSS] Failed to get ID3D11Device5 interface");
			return;
		}

		// Get texture description from input texture
		winrt::com_ptr<ID3D11Texture2D> inputTexture2D;
		if (FAILED(a_inputTexture->QueryInterface(IID_PPV_ARGS(&inputTexture2D)))) {
			logger::error("[XeSS] Input texture is not a 2D texture");
			return;
		}
		
		D3D11_TEXTURE2D_DESC inputDesc;
		inputTexture2D->GetDesc(&inputDesc);
		inputDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
		inputColorTexture = new WrappedResource(inputDesc, d3d11Device5.get(), upscaling->sharedD3D12Device.get());
		logger::debug("[XeSS] Created input color shared texture from source");
	}
	
	if (!outputColorTexture) {
		// Get D3D11 device5 interface for WrappedResource creation
		winrt::com_ptr<ID3D11Device5> d3d11Device5;
		if (FAILED(globals::d3d::device->QueryInterface(IID_PPV_ARGS(&d3d11Device5)))) {
			logger::error("[XeSS] Failed to get ID3D11Device5 interface");
			return;
		}

		// Get texture description from output texture
		winrt::com_ptr<ID3D11Texture2D> outputTexture2D;
		if (FAILED(a_outputTexture->QueryInterface(IID_PPV_ARGS(&outputTexture2D)))) {
			logger::error("[XeSS] Output texture is not a 2D texture");
			return;
		}
		
		D3D11_TEXTURE2D_DESC outputDesc;
		outputTexture2D->GetDesc(&outputDesc);
		outputDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
		outputColorTexture = new WrappedResource(outputDesc, d3d11Device5.get(), upscaling->sharedD3D12Device.get());
		logger::debug("[XeSS] Created output color shared texture from source");
	}

	// Copy input textures to shared resources
	globals::d3d::context->CopyResource(inputColorTexture->resource11, a_inputTexture);
	globals::d3d::context->CopyResource(upscaling->motionVectorBufferShared12->resource11, a_motionVectors);
	globals::d3d::context->CopyResource(upscaling->depthBufferShared12->resource11, a_depth);
	globals::d3d::context->CopyResource(upscaling->reactiveMaskBufferShared12->resource11, a_reactiveMask);
	
	// Flush D3D11 context to ensure copies are complete before D3D12 access
	globals::d3d::context->Flush();

	// Reset command allocator and list from shared resources
	HRESULT hr = upscaling->sharedD3D12CommandAllocator->Reset();
	if (FAILED(hr)) {
		logger::error("[XeSS] Failed to reset shared command allocator: 0x{:X}", hr);
		return;
	}

	hr = upscaling->sharedD3D12CommandList->Reset(upscaling->sharedD3D12CommandAllocator.get(), nullptr);
	if (FAILED(hr)) {
		logger::error("[XeSS] Failed to reset shared command list: 0x{:X}", hr);
		return;
	}

	// Execute XeSS upscaling on D3D12 using shared resources
	xess_d3d12_execute_params_t execParams{};
	execParams.pColorTexture = inputColorTexture->resource.get();
	execParams.pVelocityTexture = upscaling->motionVectorBufferShared12->resource.get();
	execParams.pDepthTexture = upscaling->depthBufferShared12->resource.get();
	execParams.pExposureScaleTexture = nullptr;
	execParams.pResponsivePixelMaskTexture = upscaling->reactiveMaskBufferShared12->resource.get();
	execParams.pOutputTexture = outputColorTexture->resource.get();
	execParams.jitterOffsetX = -a_jitter.x;
	execParams.jitterOffsetY = -a_jitter.y;
	execParams.exposureScale = 1.0f;
	execParams.resetHistory = 0;
	execParams.inputWidth = (uint32_t)renderSize.x;
	execParams.inputHeight = (uint32_t)renderSize.y;
	execParams.inputColorBase = { 0, 0 };
	execParams.inputMotionVectorBase = { 0, 0 };
	execParams.inputDepthBase = { 0, 0 };
	execParams.inputResponsiveMaskBase = { 0, 0 };
	execParams.reserved0 = { 0, 0 };
	execParams.outputColorBase = { 0, 0 };
	execParams.pDescriptorHeap = nullptr;
	execParams.descriptorHeapOffset = 0;

	xess_result_t result = xessD3D12Execute(xessContext, upscaling->sharedD3D12CommandList.get(), &execParams);
	if (result != XESS_RESULT_SUCCESS) {
		logger::error("[XeSS] Failed to execute XeSS upscaling, error code: {}", (int)result);
		return;
	}

	// Close and execute command list using shared resources
	hr = upscaling->sharedD3D12CommandList->Close();
	if (FAILED(hr)) {
		logger::error("[XeSS] Failed to close shared command list: 0x{:X}", hr);
		return;
	}

	ID3D12CommandList* commandLists[] = { upscaling->sharedD3D12CommandList.get() };
	upscaling->sharedD3D12CommandQueue->ExecuteCommandLists(1, commandLists);

	// Signal fence and wait for completion using shared resources
	upscaling->sharedFenceValue++;
	hr = upscaling->sharedD3D12CommandQueue->Signal(upscaling->sharedD3D12Fence.get(), upscaling->sharedFenceValue);
	if (FAILED(hr)) {
		logger::error("[XeSS] Failed to signal shared fence: 0x{:X}", hr);
		return;
	}

	if (upscaling->sharedD3D12Fence->GetCompletedValue() < upscaling->sharedFenceValue) {
		hr = upscaling->sharedD3D12Fence->SetEventOnCompletion(upscaling->sharedFenceValue, upscaling->sharedFenceEvent);
		if (FAILED(hr)) {
			logger::error("[XeSS] Failed to set shared fence event: 0x{:X}", hr);
			return;
		}
		WaitForSingleObject(upscaling->sharedFenceEvent, INFINITE);
	}

	// Copy output texture from D3D12 back to D3D11
	globals::d3d::context->CopyResource(a_outputTexture, outputColorTexture->resource11);
}