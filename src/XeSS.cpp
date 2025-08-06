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

	// Create shared D3D11/D3D12 fences for synchronization
	winrt::com_ptr<ID3D11Device5> d3d11Device5;
	DX::ThrowIfFailed(globals::d3d::device->QueryInterface(IID_PPV_ARGS(&d3d11Device5)));

	HANDLE sharedFenceHandle;
	DX::ThrowIfFailed(upscaling->sharedD3D12Device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&d3d12Fence)));
	DX::ThrowIfFailed(upscaling->sharedD3D12Device->CreateSharedHandle(d3d12Fence.get(), nullptr, GENERIC_ALL, nullptr, &sharedFenceHandle));
	DX::ThrowIfFailed(d3d11Device5->OpenSharedFence(sharedFenceHandle, IID_PPV_ARGS(&d3d11Fence)));
	CloseHandle(sharedFenceHandle);

	logger::info("[XeSS] XeSS context initialized successfully");
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
}

void XeSS::Upscale(ID3D11Resource* a_inputTexture, ID3D11Resource* a_outputTexture, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_motionVectors, ID3D11Resource* a_depth, float2 a_jitter)
{
	auto upscaling = globals::upscaling;
	auto state = globals::state;
	auto renderSize = Util::ConvertToDynamic(state->screenSize);

	// Copy input textures to shared resources
	globals::d3d::context->CopyResource(upscaling->inputColorBufferShared12->resource11, a_inputTexture);
	globals::d3d::context->CopyResource(upscaling->motionVectorBufferShared12->resource11, a_motionVectors);
	globals::d3d::context->CopyResource(upscaling->depthBufferShared12->resource11, a_depth);
	globals::d3d::context->CopyResource(upscaling->reactiveMaskBufferShared12->resource11, a_reactiveMask);
	
	// Wait for D3D11 to finish
	winrt::com_ptr<ID3D11DeviceContext4> d3d11Context4;
	DX::ThrowIfFailed(globals::d3d::context->QueryInterface(IID_PPV_ARGS(&d3d11Context4)));
	DX::ThrowIfFailed(d3d11Context4->Signal(d3d11Fence.get(), fenceValue));
	DX::ThrowIfFailed(upscaling->sharedD3D12CommandQueue->Wait(d3d12Fence.get(), fenceValue));
	fenceValue++;

	// Reset command allocator and list from shared resources
	DX::ThrowIfFailed(upscaling->sharedD3D12CommandAllocator->Reset());
	DX::ThrowIfFailed(upscaling->sharedD3D12CommandList->Reset(upscaling->sharedD3D12CommandAllocator.get(), nullptr));

	// Transition input resources to NON_PIXEL_SHADER_RESOURCE state and output to UNORDERED_ACCESS state
	{
		std::vector<D3D12_RESOURCE_BARRIER> barriers;
		barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(upscaling->inputColorBufferShared12->resource.get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
		barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(upscaling->motionVectorBufferShared12->resource.get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
		barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(upscaling->depthBufferShared12->resource.get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
		barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(upscaling->reactiveMaskBufferShared12->resource.get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
		barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(upscaling->outputColorBufferShared12->resource.get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
		upscaling->sharedD3D12CommandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
	}

	// Execute XeSS upscaling on D3D12 using shared resources
	xess_d3d12_execute_params_t execParams{};
	execParams.pColorTexture = upscaling->inputColorBufferShared12->resource.get();
	execParams.pVelocityTexture = upscaling->motionVectorBufferShared12->resource.get();
	execParams.pDepthTexture = upscaling->depthBufferShared12->resource.get();
	execParams.pExposureScaleTexture = nullptr;
	execParams.pResponsivePixelMaskTexture = upscaling->reactiveMaskBufferShared12->resource.get();
	execParams.pOutputTexture = upscaling->outputColorBufferShared12->resource.get();
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

	// Transition resources back to COMMON state
	{
		std::vector<D3D12_RESOURCE_BARRIER> barriers;
		barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(upscaling->inputColorBufferShared12->resource.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON));
		barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(upscaling->motionVectorBufferShared12->resource.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON));
		barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(upscaling->depthBufferShared12->resource.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON));
		barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(upscaling->reactiveMaskBufferShared12->resource.get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON));
		barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(upscaling->outputColorBufferShared12->resource.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON));
		upscaling->sharedD3D12CommandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
	}

	// Close and execute command list using shared resources
	DX::ThrowIfFailed(upscaling->sharedD3D12CommandList->Close());

	ID3D12CommandList* commandLists[] = { upscaling->sharedD3D12CommandList.get() };
	upscaling->sharedD3D12CommandQueue->ExecuteCommandLists(1, commandLists);

	// Wait for D3D12 to finish
	DX::ThrowIfFailed(upscaling->sharedD3D12CommandQueue->Signal(d3d12Fence.get(), fenceValue));
	DX::ThrowIfFailed(d3d11Context4->Wait(d3d11Fence.get(), fenceValue));
	fenceValue++;

	// Copy output texture from D3D12 back to D3D11
	globals::d3d::context->CopyResource(a_outputTexture, upscaling->outputColorBufferShared12->resource11);
}