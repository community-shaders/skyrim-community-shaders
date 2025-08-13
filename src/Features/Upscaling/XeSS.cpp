#include "XeSS.h"
#include "../../Utils/FileSystem.h"

#include "../../State.h"
#include "../Upscaling.h"

#include <directx/d3dx12.h>
#include <magic_enum.hpp>

// Define the static member
std::vector<std::pair<std::string, std::string>> XeSS::dllVersions = {};

// XeSS Frame Generation logging callback
static void XeSSFGLoggingCallback(const char* message, xefg_swapchain_logging_level_t loggingLevel, void*)
{
	switch (loggingLevel) {
	case XEFG_SWAPCHAIN_LOG_LEVEL_INFO:
		logger::info("[XeSS FG] {}", message);
		break;
	case XEFG_SWAPCHAIN_LOG_LEVEL_WARNING:
		logger::warn("[XeSS FG] {}", message);
		break;
	case XEFG_SWAPCHAIN_LOG_LEVEL_ERROR:
		logger::error("[XeSS FG] {}", message);
		break;
	default:
		logger::info("[XeSS FG] {}", message);
		break;
	}
}

void XeSS::LoadXeSS()
{
	std::wstring dllPath = std::wstring(XeSS::PluginDir) + L"\\libxess.dll";
	module = LoadLibrary(dllPath.c_str());

	std::wstring dllPathFG = std::wstring(XeSS::PluginDir) + L"\\libxess_fg.dll";
	moduleFG = LoadLibrary(dllPathFG.c_str());

	std::wstring dllPathLL = std::wstring(XeSS::PluginDir) + L"\\libxell.dll";
	moduleLL = LoadLibrary(dllPathLL.c_str());

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

			// Try to load XeSS Frame Generation functions - corrected function names
			xefgSwapChainD3D12CreateContext = (xefgSwapChainD3D12CreateContextPtr)GetProcAddress(moduleFG, "xefgSwapChainD3D12CreateContext");
			xefgSwapChainD3D12InitFromSwapChain = (xefgSwapChainD3D12InitFromSwapChainPtr)GetProcAddress(moduleFG, "xefgSwapChainD3D12InitFromSwapChain");
			xefgSwapChainD3D12InitFromSwapChainDesc = (xefgSwapChainD3D12InitFromSwapChainDescPtr)GetProcAddress(moduleFG, "xefgSwapChainD3D12InitFromSwapChainDesc");
			xefgSwapChainSetLatencyReduction = (xefgSwapChainSetLatencyReductionPtr)GetProcAddress(moduleFG, "xefgSwapChainSetLatencyReduction");
			xellD3D12CreateContext = (xellD3D12CreateContextPtr)GetProcAddress(moduleLL, "xellD3D12CreateContext");
			xellDestroyContext = (xellDestroyContextPtr)GetProcAddress(moduleLL, "xellDestroyContext");
			xellAddMarkerData = (xellAddMarkerDataPtr)GetProcAddress(moduleLL, "xellAddMarkerData");
			xefgSwapChainSetLoggingCallback = (xefgSwapChainSetLoggingCallbackPtr)GetProcAddress(moduleFG, "xefgSwapChainSetLoggingCallback");
			xefgSwapChainD3D12TagFrameResource = (xefgSwapChainD3D12TagFrameResourcePtr)GetProcAddress(moduleFG, "xefgSwapChainD3D12TagFrameResource");
			xefgSwapChainTagFrameConstants = (xefgSwapChainTagFrameConstantsPtr)GetProcAddress(moduleFG, "xefgSwapChainTagFrameConstants");
			xefgSwapChainSetEnabled = (xefgSwapChainSetEnabledPtr)GetProcAddress(moduleFG, "xefgSwapChainSetEnabled");
			xefgSwapChainD3D12GetSwapChainPtr = (xefgSwapChainD3D12GetSwapChainPtrPtr)GetProcAddress(moduleFG, "xefgSwapChainD3D12GetSwapChainPtr");
			xefgSwapChainDestroyContext = (xefgSwapChainDestroyContextPtr)GetProcAddress(moduleFG, "xefgSwapChainDestroyContext");
			xefgSwapChainSetPresentId = (xefgSwapChainSetPresentIdPtr)GetProcAddress(moduleFG, "xefgSwapChainSetPresentId");

			if (xefgSwapChainD3D12CreateContext && (xefgSwapChainD3D12InitFromSwapChain || xefgSwapChainD3D12InitFromSwapChainDesc) && 
				xefgSwapChainD3D12TagFrameResource && xefgSwapChainTagFrameConstants && xefgSwapChainSetEnabled && 
				xefgSwapChainD3D12GetSwapChainPtr && xefgSwapChainDestroyContext && xefgSwapChainSetPresentId) {
				featureXeSSFG = true;
				logger::info("[XeSS] Successfully loaded XeSS Frame Generation functions");
			} else {
				featureXeSSFG = false;
				logger::warn("[XeSS] XeSS Frame Generation functions not available in this SDK version");
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
	auto& upscaling = globals::features::upscaling;
	if (!featureXeSS || !upscaling.sharedD3D12Device) {
		logger::error("[XeSS] XeSS not available or shared D3D12 device not available, cannot create resources");
		return;
	}

	auto state = globals::state;

	xess_result_t createResult = xessD3D12CreateContext(upscaling.sharedD3D12Device.get(), &xessContext);
	if (createResult != XESS_RESULT_SUCCESS) {
		logger::critical("[XeSS] Failed to create XeSS context, error: {} ({})", magic_enum::enum_name(createResult), (int)createResult);
		return;
	}

	xess_d3d12_init_params_t initParams{};
	initParams.outputResolution.x = (uint32_t)state->screenSize.x;
	initParams.outputResolution.y = (uint32_t)state->screenSize.y;
	initParams.qualitySetting = XESS_QUALITY_SETTING_AA;
	initParams.initFlags = XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE | XESS_INIT_FLAG_USE_NDC_VELOCITY | XESS_INIT_FLAG_RESPONSIVE_PIXEL_MASK;

	initParams.creationNodeMask = 1;
	initParams.visibleNodeMask = 1;
	initParams.pTempBufferHeap = nullptr;
	initParams.bufferHeapOffset = 0;
	initParams.pTempTextureHeap = nullptr;
	initParams.textureHeapOffset = 0;
	initParams.pPipelineLibrary = nullptr;

	xess_result_t initResult = xessD3D12Init(xessContext, &initParams);
	if (initResult != XESS_RESULT_SUCCESS) {
		logger::critical("[XeSS] Failed to initialize XeSS context, error: {} ({})", magic_enum::enum_name(initResult), (int)initResult);
		return;
	}
}

void XeSS::DestroyXeSSResources()
{
	// Destroy frame generation resources first
	DestroyFrameGenerationResources();

	if (xessContext && xessDestroyContext) {
		xess_result_t result = xessDestroyContext(xessContext);
		if (result != XESS_RESULT_SUCCESS) {
			logger::error("[XeSS] Failed to destroy XeSS context, error: {} ({})", magic_enum::enum_name(result), (int)result);
		}
		xessContext = nullptr;
	}
}

float XeSS::GetInputResolutionScale(uint32_t outputWidth, uint32_t outputHeight, uint32_t qualityMode)
{
	// Check if XeSS context is valid
	if (!xessContext) {
		logger::error("[XeSS] GetInputResolutionScale called with null context");
		return 1.0f;
	}

	// Check if function pointer is valid
	if (!xessGetInputResolution) {
		logger::error("[XeSS] GetInputResolutionScale called with null function pointer");
		return 1.0f;
	}

	// Validate input parameters
	if (outputWidth == 0 || outputHeight == 0) {
		logger::error("[XeSS] GetInputResolutionScale called with invalid resolution: {}x{}", outputWidth, outputHeight);
		return 1.0f;
	}

	xess_quality_settings_t xessQuality;
	switch (qualityMode) {
	case 1:
		xessQuality = XESS_QUALITY_SETTING_QUALITY;
		break;
	case 2:
		xessQuality = XESS_QUALITY_SETTING_BALANCED;
		break;
	case 3:
		xessQuality = XESS_QUALITY_SETTING_PERFORMANCE;
		break;
	case 4:
		xessQuality = XESS_QUALITY_SETTING_ULTRA_PERFORMANCE;
		break;
	default:
		xessQuality = XESS_QUALITY_SETTING_AA;
		break;
	}

	xess_2d_t outputResolution = { outputWidth, outputHeight };
	xess_2d_t inputResolution = { 0, 0 };

	xess_result_t result = xessGetInputResolution(xessContext, &outputResolution, xessQuality, &inputResolution);
	if (result != XESS_RESULT_SUCCESS) {
		logger::critical("[XeSS] Failed to get input resolution, error: {} ({})", magic_enum::enum_name(result), (int)result);
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
	ID3D12Resource* a_reactiveMask,
	ID3D12Resource* a_outputTexture,
	ID3D12GraphicsCommandList* a_commandList,
	uint32_t a_renderWidth,
	uint32_t a_renderHeight,
	float2 a_jitter)
{
	// Set velocity and jitter scales
	xess_result_t velocityResult = xessSetVelocityScale(xessContext, 2.0f, -2.0f);
	if (velocityResult != XESS_RESULT_SUCCESS) {
		logger::warn("[XeSS] Failed to set velocity scale, error: {} ({})", magic_enum::enum_name(velocityResult), (int)velocityResult);
	}

	xess_result_t jitterResult = xessSetJitterScale(xessContext, 1.0f, 1.0f);
	if (jitterResult != XESS_RESULT_SUCCESS) {
		logger::warn("[XeSS] Failed to set jitter scale, error: {} ({})", magic_enum::enum_name(jitterResult), (int)jitterResult);
	}

	// XeSS execution parameters
	xess_d3d12_execute_params_t execParams{};
	execParams.pColorTexture = a_inputColorTexture;
	execParams.pVelocityTexture = a_motionVectorTexture;
	execParams.pDepthTexture = a_depthTexture;
	execParams.pExposureScaleTexture = nullptr;
	execParams.pResponsivePixelMaskTexture = a_reactiveMask;
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
		logger::error("[XeSS] Failed to execute XeSS upscaling, error: {} ({})", magic_enum::enum_name(result), (int)result);
		return;
	}
}

IDXGISwapChain* XeSS::SetupFrameGeneration(HWND a_hwnd, DXGI_SWAP_CHAIN_DESC1 a_swapChainDesc, ID3D12CommandQueue* a_commandQueue, IDXGIFactory* a_factory)
{
	auto& upscaling = globals::features::upscaling;

	xell_result_t xellResult = xellD3D12CreateContext(upscaling.sharedD3D12Device.get(), &xellContext);
	if (xellResult == XELL_RESULT_SUCCESS) {
		logger::info("[XeLL] Successfully created XeLL context for latency reduction");
	} else {
		logger::warn("[XeLL] Failed to create XeLL context, error: {}", (int)xellResult);
	}	

	xefg_swapchain_result_t result = xefgSwapChainD3D12CreateContext(upscaling.sharedD3D12Device.get(), &xefgContext);
	if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS) {
		logger::critical("[XeSS FG] Failed to create frame generation context, error: {}", (int)result);
		return nullptr;
	}

	// Set up logging callback if available
	xefg_swapchain_logging_level_t log_level = xefg_swapchain_logging_level_t::XEFG_SWAPCHAIN_LOGGING_LEVEL_DEBUG;

	result = xefgSwapChainSetLoggingCallback(xefgContext, log_level, (xefg_swapchain_logging_callback_t)XeSSFGLoggingCallback, nullptr);
	if (result == XEFG_SWAPCHAIN_RESULT_SUCCESS) {
		logger::info("[XeSS FG] Logging callback configured");
	} else {
		logger::warn("[XeSS FG] Failed to set logging callback, error: {}", (int)result);
	}
	

	result = xefgSwapChainSetLatencyReduction(xefgContext, xellContext);
	if (result == XEFG_SWAPCHAIN_RESULT_SUCCESS) {
		logger::info("[XeSS FG] Latency reduction configured");
	} else {
		logger::warn("[XeSS FG] Failed to set latency reduction, error: {}", (int)result);
	}
	xefg_swapchain_d3d12_init_params_t initParams{};
	initParams.pApplicationSwapChain = nullptr;
	initParams.maxInterpolatedFrames = 1;
	initParams.uiMode = XEFG_SWAPCHAIN_UI_MODE_AUTO;
	
	a_swapChainDesc.BufferCount = 2;
	a_swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	a_swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	a_swapChainDesc.SampleDesc.Count = 1;
	a_swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

	result = xefgSwapChainD3D12InitFromSwapChainDesc(xefgContext, a_hwnd, &a_swapChainDesc, nullptr, a_commandQueue, a_factory, &initParams);

	if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS) {
		logger::critical("[XeSS FG] Failed to initialize from swap chain desc, error: {}", (int)result);
		return nullptr;
	}

	// Get the created swap chain from XeSS
	IDXGISwapChain* createdSwapChain = nullptr;
	result = xefgSwapChainD3D12GetSwapChainPtr(xefgContext, IID_PPV_ARGS(&createdSwapChain));
	if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS || !createdSwapChain) {
		logger::critical("[XeSS FG] Failed to get swap chain pointer, error: {}", (int)result);
		return nullptr;
	}

	currentPresentId = 0;
	logger::info("[XeSS FG] Successfully initialized frame generation and created swap chain");
	return createdSwapChain;
}


void XeSS::Present(bool a_useFrameGeneration, ID3D12CommandList* a_commandList)
{
	if (!featureXeSSFG || !xefgContext) {
		return;
	}

	// Get frame ID from global state
	uint32_t frameId = globals::state ? globals::state->frameCount : 0;

	// Set present ID
	if (xefgSwapChainSetPresentId && xefgSwapChainSetPresentId(xefgContext, frameId) != XEFG_SWAPCHAIN_RESULT_SUCCESS) {
		logger::warn("[XeSS FG] Failed to set present ID");
	}

	// Tag frame resources if frame generation is enabled
	if (a_useFrameGeneration && a_commandList) {
		TagFrameResources(a_commandList, frameId);
	}

	// Enable/disable frame generation
	uint32_t enableFlag = a_useFrameGeneration ? 1 : 0;
	if (xefgSwapChainSetEnabled(xefgContext, enableFlag) != XEFG_SWAPCHAIN_RESULT_SUCCESS) {
		logger::warn("[XeSS FG] Failed to set frame generation enabled state");
	}

	// Update frame generation active state
	isFrameGenActive = a_useFrameGeneration;
}

void XeSS::DestroyFrameGenerationResources()
{
	// Destroy XeLL context first
	if (xellContext && xellDestroyContext) {
		xell_result_t xellResult = xellDestroyContext(xellContext);
		if (xellResult != XELL_RESULT_SUCCESS) {
			logger::error("[XeLL] Failed to destroy XeLL context, error: {}", (int)xellResult);
		}
		xellContext = nullptr;
	}

	// Destroy frame generation context
	if (xefgContext && xefgSwapChainDestroyContext) {
		xefg_swapchain_result_t result = xefgSwapChainDestroyContext(xefgContext);
		if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS) {
			logger::error("[XeSS FG] Failed to destroy frame generation context, error: {}", (int)result);
		}
		xefgContext = nullptr;
	}
	currentPresentId = 0;
	isFrameGenActive = false;
}

void XeSS::TagFrameResources(ID3D12CommandList* a_commandList, uint32_t presentId)
{
	auto& upscaling = globals::features::upscaling;
	auto screenSize = globals::state->screenSize;
	auto renderSize = Util::ConvertToDynamic(screenSize);

	// Motion vectors resource data
	xefg_swapchain_d3d12_resource_data_t motionVectorData{};
	motionVectorData.type = XEFG_SWAPCHAIN_RES_MOTION_VECTOR;
	motionVectorData.validity = XEFG_SWAPCHAIN_RV_UNTIL_NEXT_PRESENT;
	motionVectorData.pResource = upscaling.motionVectorBufferShared12->resource.get();
	motionVectorData.incomingState = D3D12_RESOURCE_STATE_COMMON;
	motionVectorData.resourceBase = { 0, 0 };
	motionVectorData.resourceSize = { (uint)renderSize.x, (uint)renderSize.y };

	if (xefgSwapChainD3D12TagFrameResource(xefgContext, a_commandList, presentId, &motionVectorData) != XEFG_SWAPCHAIN_RESULT_SUCCESS) {
		logger::warn("[XeSS FG] Failed to tag motion vectors");
	}

	// Depth buffer resource data
	xefg_swapchain_d3d12_resource_data_t depthData{};
	depthData.type = XEFG_SWAPCHAIN_RES_DEPTH;
	depthData.validity = XEFG_SWAPCHAIN_RV_UNTIL_NEXT_PRESENT;
	depthData.pResource = upscaling.depthBufferShared12->resource.get();
	depthData.incomingState = D3D12_RESOURCE_STATE_COMMON;
	depthData.resourceBase = { 0, 0 };
	depthData.resourceSize = { (uint)renderSize.x, (uint)renderSize.y };

	if (xefgSwapChainD3D12TagFrameResource(xefgContext, a_commandList, presentId, &depthData) != XEFG_SWAPCHAIN_RESULT_SUCCESS) {
		logger::warn("[XeSS FG] Failed to tag depth buffer");
	}

	// HUD-less color buffer resource data
	xefg_swapchain_d3d12_resource_data_t colorData{};
	colorData.type = XEFG_SWAPCHAIN_RES_HUDLESS_COLOR;
	colorData.validity = XEFG_SWAPCHAIN_RV_UNTIL_NEXT_PRESENT;
	colorData.pResource = upscaling.HUDLessBufferShared12->resource.get();
	colorData.incomingState = D3D12_RESOURCE_STATE_COMMON;
	colorData.resourceBase = { 0, 0 };
	colorData.resourceSize = { (uint)renderSize.x, (uint)renderSize.y };

	if (xefgSwapChainD3D12TagFrameResource(xefgContext, a_commandList, presentId, &colorData) != XEFG_SWAPCHAIN_RESULT_SUCCESS) {
		logger::warn("[XeSS FG] Failed to tag HUD-less color buffer");
	}

	// UI buffer resource data
	xefg_swapchain_d3d12_resource_data_t uiData{};
	uiData.type = XEFG_SWAPCHAIN_RES_UI;
	uiData.validity = XEFG_SWAPCHAIN_RV_UNTIL_NEXT_PRESENT;
	uiData.pResource = upscaling.dx12SwapChain.uiBufferWrapped->resource.get();
	uiData.incomingState = D3D12_RESOURCE_STATE_COMMON;
	uiData.resourceBase = { 0, 0 };
	uiData.resourceSize = { (uint)renderSize.x, (uint)renderSize.y };

	if (xefgSwapChainD3D12TagFrameResource(xefgContext, a_commandList, presentId, &uiData) != XEFG_SWAPCHAIN_RESULT_SUCCESS) {
		logger::warn("[XeSS FG] Failed to tag UI buffer");
	}

	// constants
	{
		xefg_swapchain_frame_constant_data_t constData = {};

		auto viewMatrix = globals::game::frameBufferCached.GetCameraViewInverse().Transpose();
		auto projectionMatrix = globals::game::frameBufferCached.GetCameraProjUnjittered().Transpose();
		
		memcpy(constData.viewMatrix, &viewMatrix, sizeof(float) * 16);
		memcpy(constData.projectionMatrix, &projectionMatrix, sizeof(float) * 16);
		
		constData.jitterOffsetX = constData.jitterOffsetY = 0.0f;
		constData.motionVectorScaleX = constData.motionVectorScaleY = 1.0f;
		constData.frameRenderTime = *globals::game::deltaTime * 1000.f;

		DX::ThrowIfFailed(xefgSwapChainTagFrameConstants(xefgContext, presentId, &constData));
	}
}

void XeSS::SetFrameConstants(uint32_t presentId, const xefg_swapchain_frame_constant_data_t* pConstants)
{
	if (!xefgContext || !pConstants) {
		return;
	}

	if (xefgSwapChainTagFrameConstants(xefgContext, presentId, pConstants) != XEFG_SWAPCHAIN_RESULT_SUCCESS) {
		logger::warn("[XeSS FG] Failed to tag frame constants");
	}
}

// XeLL low latency marker functions
void XeSS::AddMarker(_xell_latency_marker_type_t markerType)
{
	if (!xellContext || !xellAddMarkerData) {
		return;
	}

	uint32_t frameId = globals::state ? globals::state->frameCount : 0;
	xell_result_t result = xellAddMarkerData(xellContext, frameId, markerType);
	if (result != XELL_RESULT_SUCCESS) {
		logger::warn("[XeLL] Failed to add marker data, type: {}, error: {}", (int)markerType, (int)result);
	}
}

void XeSS::AddSimulationStartMarker()
{
	AddMarker(XELL_SIMULATION_START);
}

void XeSS::AddSimulationEndMarker()
{
	AddMarker(XELL_SIMULATION_END);
	AddMarker(XELL_RENDERSUBMIT_START);
}

void XeSS::AddPresentStartMarker()
{
	AddMarker(XELL_RENDERSUBMIT_END);
	AddMarker(XELL_PRESENT_START);
}

void XeSS::AddPresentEndMarker()
{
	AddMarker(XELL_PRESENT_END);
}