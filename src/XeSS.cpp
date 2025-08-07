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
		xessGetInputResolution = (xessGetInputResolutionPtr)GetProcAddress(module, "xessGetInputResolution");

		if (xessGetVersion && xessD3D12CreateContext && xessD3D12Init && xessD3D12Execute && xessDestroyContext && xessSetJitterScale && xessSetVelocityScale && xessGetInputResolution) {
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

float XeSS::GetInputResolutionScale(uint32_t outputWidth, uint32_t outputHeight, uint32_t qualityPreset)
{
	xess_quality_settings_t xessQuality;
	switch (qualityPreset) {
	case 1: xessQuality = XESS_QUALITY_SETTING_QUALITY; break; 
	case 2: xessQuality = XESS_QUALITY_SETTING_BALANCED; break;
	case 3: xessQuality = XESS_QUALITY_SETTING_PERFORMANCE; break;
	default: 
		xessQuality = XESS_QUALITY_SETTING_AA;
		break;
	}

	xess_2d_t outputResolution = { outputWidth, outputHeight };
	xess_2d_t inputResolution = { 0, 0 };

	xess_result_t result = xessGetInputResolution(xessContext, &outputResolution, xessQuality, &inputResolution);
	if (result != XESS_RESULT_SUCCESS) {
		logger::critical("[XeSS] Failed to get input resolution, error code: {}", (int)result);
		return 1.0f;
	}

	// Calculate scale as ratio of input to output resolution
	float scaleX = (float)inputResolution.x / (float)outputResolution.x;
	float scaleY = (float)inputResolution.y / (float)outputResolution.y;
	
	// Use the average scale (both should be the same for uniform scaling)
	return (scaleX + scaleY) * 0.5f;
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