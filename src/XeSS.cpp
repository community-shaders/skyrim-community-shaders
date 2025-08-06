#include "XeSS.h"
#include "Utils/FileSystem.h"

#include "State.h"
#include "Upscaling.h"

#include <FidelityFX/host/backends/dx12/d3dx12.h>

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
		xessSetJitterScale = (xessSetJitterScalePtr)GetProcAddress(module, "xessSetJitterScale");
		xessSetVelocityScale = (xessSetVelocityScalePtr)GetProcAddress(module, "xessSetVelocityScale");

		if (xessGetVersion && xessD3D12CreateContext && xessD3D12Init && xessD3D12Execute && xessDestroyContext && xessSetJitterScale && xessSetVelocityScale) {
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

	xess_d3d12_init_params_t initParams{};
	initParams.outputResolution.x = (uint32_t)state->screenSize.x;
	initParams.outputResolution.y = (uint32_t)state->screenSize.y;
	initParams.qualitySetting = XESS_QUALITY_SETTING_ULTRA_PERFORMANCE;
	initParams.initFlags = XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE;

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

	CreateD3D12IntermediaryTextures();
}

void XeSS::DestroyXeSSResources()
{
	DestroyD3D12IntermediaryTextures();

	if (xessContext && xessDestroyContext) {
		xess_result_t result = xessDestroyContext(xessContext);
		if (result != XESS_RESULT_SUCCESS) {
			logger::error("[XeSS] Failed to destroy XeSS context, error code: {}", (int)result);
		}
		xessContext = nullptr;
	}
}

void XeSS::Upscale(
	ID3D12Resource* a_inputColorTexture,
	ID3D12Resource* a_motionVectorTexture,
	ID3D12Resource* a_depthTexture,
	ID3D12Resource* a_outputTexture,
	ID3D12GraphicsCommandList* a_commandList,
	uint32_t a_renderWidth,
	uint32_t a_renderHeight,
	float2 a_jitter
)
{
	// Set velocity and jitter scales
	if (xessSetVelocityScale(xessContext, (float)a_renderWidth, (float)a_renderHeight) != XESS_RESULT_SUCCESS) {
		logger::warn("[XeSS] Failed to set velocity scale");
	}

	if (xessSetJitterScale(xessContext, 1.0f, 1.0f) != XESS_RESULT_SUCCESS) {
		logger::warn("[XeSS] Failed to set jitter scale");
	}

	// XeSS execution parameters
	xess_d3d12_execute_params_t execParams{};
	execParams.pColorTexture = a_inputColorTexture;
	execParams.pVelocityTexture = a_motionVectorTexture;
	execParams.pDepthTexture = a_depthTexture;
	execParams.pExposureScaleTexture = nullptr;
	execParams.pResponsivePixelMaskTexture = nullptr;
	execParams.pOutputTexture = a_outputTexture;
	execParams.jitterOffsetX = -a_jitter.x;
	execParams.jitterOffsetY = -a_jitter.y;
	execParams.exposureScale = 1.0f;
	execParams.resetHistory = 0;
	execParams.inputWidth = a_renderWidth;
	execParams.inputHeight = a_renderHeight;
	execParams.inputColorBase = { 0, 0 };
	execParams.inputMotionVectorBase = { 0, 0 };
	execParams.inputDepthBase = { 0, 0 };
	execParams.inputResponsiveMaskBase = { 0, 0 };
	execParams.reserved0 = { 0, 0 };
	execParams.outputColorBase = { 0, 0 };
	execParams.pDescriptorHeap = nullptr;
	execParams.descriptorHeapOffset = 0;

	xess_result_t result = xessD3D12Execute(xessContext, a_commandList, &execParams);
	if (result != XESS_RESULT_SUCCESS) {
		logger::error("[XeSS] Failed to execute XeSS upscaling, error code: {}", (int)result);
		return;
	}
}

void XeSS::CreateD3D12IntermediaryTextures()
{
	auto upscaling = globals::upscaling;
	if (!upscaling->sharedD3D12Device) {
		logger::error("[XeSS] Cannot create D3D12 intermediary textures, shared D3D12 device not available");
		return;
	}

	// Clean up any existing textures first
	DestroyD3D12IntermediaryTextures();

	auto state = globals::state;
	auto screenSize = state->screenSize;
//	auto renderSize = Util::ConvertToDynamic(screenSize);

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

	// Input color texture (RGBA16F or RGBA8)
	D3D12_RESOURCE_DESC inputColorDesc = {};
	inputColorDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	inputColorDesc.Width = (UINT)screenSize.x;
	inputColorDesc.Height = (UINT)screenSize.y;
	inputColorDesc.DepthOrArraySize = 1;
	inputColorDesc.MipLevels = 1;
	inputColorDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	inputColorDesc.SampleDesc.Count = 1;
	inputColorDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	inputColorDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	DX::ThrowIfFailed(upscaling->sharedD3D12Device->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&inputColorDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&inputColorTexture)
	));
	inputColorTexture->SetName(L"XeSS_InputColorTexture");

	// Output color texture (same as input)
	DX::ThrowIfFailed(upscaling->sharedD3D12Device->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&inputColorDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&outputColorTexture)
	));
	outputColorTexture->SetName(L"XeSS_OutputColorTexture");

	// Motion vector texture (RG16F)
	D3D12_RESOURCE_DESC motionVectorDesc = inputColorDesc;
	motionVectorDesc.Format = DXGI_FORMAT_R16G16_FLOAT;

	DX::ThrowIfFailed(upscaling->sharedD3D12Device->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&motionVectorDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&motionVectorTexture)
	));
	motionVectorTexture->SetName(L"XeSS_MotionVectorTexture");

	// Depth texture (R16 or R32F)
	D3D12_RESOURCE_DESC depthDesc = inputColorDesc;
	depthDesc.Format = DXGI_FORMAT_R16_UNORM;
	depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	DX::ThrowIfFailed(upscaling->sharedD3D12Device->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&depthDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&depthTexture)
	));
	depthTexture->SetName(L"XeSS_DepthTexture");

	// Store current resolution
	lastRenderWidth = (uint32_t)screenSize.x;
	lastRenderHeight = (uint32_t)screenSize.y;

	logger::info("[XeSS] Created D3D12 intermediary textures ({}x{}) for improved performance", lastRenderWidth, lastRenderHeight);
}

void XeSS::DestroyD3D12IntermediaryTextures()
{
	inputColorTexture = nullptr;
	outputColorTexture = nullptr;
	motionVectorTexture = nullptr;
	depthTexture = nullptr;
	lastRenderWidth = 0;
	lastRenderHeight = 0;
}

void XeSS::CheckAndRecreateIntermediaryTextures()
{
	if (!featureXeSS)
		return;

	auto renderSize = Util::ConvertToDynamic(globals::state->screenSize);
	uint32_t currentWidth = (uint32_t)renderSize.x;
	uint32_t currentHeight = (uint32_t)renderSize.y;

	// Check if resolution has changed or textures don't exist
	if (!inputColorTexture || !outputColorTexture || !motionVectorTexture || !depthTexture ||
		currentWidth != lastRenderWidth || currentHeight != lastRenderHeight) {
		
		logger::info("[XeSS] Resolution changed from {}x{} to {}x{}, recreating intermediary textures", 
			lastRenderWidth, lastRenderHeight, currentWidth, currentHeight);
		CreateD3D12IntermediaryTextures();
	}
}

void XeSS::CopyToIntermediaryTextures(
	ID3D12GraphicsCommandList* a_commandList,
	ID3D12Resource* a_inputColorTexture,
	ID3D12Resource* a_motionVectorTexture,
	ID3D12Resource* a_depthTexture
)
{
	if (!inputColorTexture || !motionVectorTexture || !depthTexture || !outputColorTexture) {
		logger::error("[XeSS] Intermediary textures not created");
		return;
	}

	// Batch all resource barriers for maximum efficiency
	D3D12_RESOURCE_BARRIER barriers[10] = {};
	UINT barrierCount = 0;

	// Transition source textures to copy source state
	barriers[barrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(a_inputColorTexture, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
	barriers[barrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(a_motionVectorTexture, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
	barriers[barrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(a_depthTexture, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);

	// Transition intermediary textures to copy destination state
	barriers[barrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(inputColorTexture.get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
	barriers[barrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(motionVectorTexture.get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
	barriers[barrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(depthTexture.get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);

	a_commandList->ResourceBarrier(barrierCount, barriers);

	// Perform texture copies
	a_commandList->CopyResource(inputColorTexture.get(), a_inputColorTexture);
	a_commandList->CopyResource(motionVectorTexture.get(), a_motionVectorTexture);
	a_commandList->CopyResource(depthTexture.get(), a_depthTexture);

	// Transition textures to final states for XeSS processing
	barrierCount = 0;
	barriers[barrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(inputColorTexture.get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	barriers[barrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(motionVectorTexture.get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	barriers[barrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(depthTexture.get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	barriers[barrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(outputColorTexture.get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	// Transition source textures back to common state
	barriers[barrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(a_inputColorTexture, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
	barriers[barrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(a_motionVectorTexture, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
	barriers[barrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(a_depthTexture, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);

	a_commandList->ResourceBarrier(barrierCount, barriers);
}

void XeSS::CopyFromIntermediaryTexture(
	ID3D12GraphicsCommandList* a_commandList,
	ID3D12Resource* a_outputColorTexture
)
{
	if (!outputColorTexture) {
		logger::error("[XeSS] Output intermediary texture not created");
		return;
	}

	// Batch resource barriers for efficiency
	D3D12_RESOURCE_BARRIER barriers[4] = {};
	UINT barrierCount = 0;

	// Transition output intermediary texture to copy source state
	barriers[barrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(outputColorTexture.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
	
	// Transition destination texture to copy destination state  
	barriers[barrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(a_outputColorTexture, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);

	a_commandList->ResourceBarrier(barrierCount, barriers);

	// Perform texture copy
	a_commandList->CopyResource(a_outputColorTexture, outputColorTexture.get());

	// Transition textures back to common state
	barrierCount = 0;
	barriers[barrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(outputColorTexture.get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
	barriers[barrierCount++] = CD3DX12_RESOURCE_BARRIER::Transition(a_outputColorTexture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);

	a_commandList->ResourceBarrier(barrierCount, barriers);
}

void XeSS::UpscaleWithIntermediaries(
	ID3D12GraphicsCommandList* a_commandList,
	uint32_t a_renderWidth,
	uint32_t a_renderHeight,
	float2 a_jitter
)
{
	if (!inputColorTexture || !motionVectorTexture || !depthTexture || !outputColorTexture) {
		logger::error("[XeSS] Intermediary textures not available for upscaling");
		return;
	}

	// Use the existing Upscale function with our intermediary textures
	Upscale(
		inputColorTexture.get(),
		motionVectorTexture.get(),
		depthTexture.get(),
		outputColorTexture.get(),
		a_commandList,
		a_renderWidth,
		a_renderHeight,
		a_jitter
	);
}