#include "FidelityFX.h"

#include <directx/d3dx12.h>

#include "Utils/FileSystem.h"

#include "State.h"
#include "Upscaling.h"

#include "DX12SwapChain.h"

ffxFunctions ffxModule;

// Define the static member
std::vector<std::pair<std::string, std::string>> FidelityFX::dllVersions = {};

void FidelityFX::LoadFFX()
{
	std::wstring dllPath = std::wstring(FidelityFX::PluginDir) + L"\\amd_fidelityfx_dx12.dll";
	module = LoadLibrary(dllPath.c_str());

	// Cache all DLL versions in the FidelityFX directory
	std::filesystem::path pluginDir = std::filesystem::path(FidelityFX::PluginDir);
	FidelityFX::dllVersions = Util::EnumerateDllVersions(pluginDir);

	if (module) {
		ffxLoadFunctions(&ffxModule, module);

		featureFSR3FG = true;
		featureFSR3 = true;

		if (featureFSR3) {
			logger::info("[FidelityFX] FSR 3 API loaded successfully");
		} else {
			logger::warn("[FidelityFX] FSR 3 API functions not found, falling back to legacy implementation");
		}
	} else {
		featureFSR3FG = false;
		featureFSR3 = false;
	}
}

void FidelityFX::SetupFrameGeneration()
{
	auto swapChain = globals::dx12SwapChain;
	auto upscaling = globals::upscaling;

	ffx::CreateContextDescFrameGeneration createFg{};
	createFg.displaySize = { swapChain->swapChainDesc.Width, swapChain->swapChainDesc.Height };
	createFg.maxRenderSize = createFg.displaySize;
	createFg.flags = FFX_FRAMEGENERATION_ENABLE_ASYNC_WORKLOAD_SUPPORT;
	createFg.backBufferFormat = ffxApiGetSurfaceFormatDX12(swapChain->swapChainDesc.Format);

	ffx::CreateBackendDX12Desc createBackend{};
	createBackend.device = upscaling->sharedD3D12Device.get();

	if (ffx::CreateContext(frameGenContext, nullptr, createFg, createBackend) != ffx::ReturnCode::Ok)
		logger::critical("[FidelityFX] Failed to create frame generation context!");
}

/**
 * @brief Presents the current frame, optionally performing frame generation using FidelityFX.
 *
 * Configures and dispatches FidelityFX frame generation for the current swap chain frame if enabled. Sets up frame pacing, prepares resources, and issues dispatches for both frame generation parameters and camera information. Increments the internal frame ID after each call.
 *
 * @param a_useFrameGeneration If true, enables frame generation and dispatches the necessary workloads; otherwise, presents without frame generation.
 */
void FidelityFX::Present(bool a_useFrameGeneration)
{
	auto upscaling = globals::upscaling;
	auto swapChain = globals::dx12SwapChain;
	auto commandList = swapChain->commandLists[swapChain->frameIndex].get();

	auto HUDLessColor = upscaling->HUDLessBufferShared12->resource.get();
	auto depth = upscaling->depthBufferShared12->resource.get();
	auto motionVectors = upscaling->motionVectorBufferShared12->resource.get();

	FfxApiSwapchainFramePacingTuning framePacingTuning{ 0.1f, 0.1f, true, 2, false };

	ffx::ConfigureDescFrameGenerationSwapChainKeyValueDX12 framePacingTuningParameters{};
	framePacingTuningParameters.key = FFX_API_CONFIGURE_FG_SWAPCHAIN_KEY_FRAMEPACINGTUNING;
	framePacingTuningParameters.ptr = &framePacingTuning;

	if (ffx::Configure(swapChainContext, framePacingTuningParameters) != ffx::ReturnCode::Ok) {
		logger::critical("[FidelityFX] Failed to configure frame pacing tuning!");
	}

	ffx::ConfigureDescFrameGeneration configParameters{};

	if (a_useFrameGeneration) {
		configParameters.frameGenerationEnabled = true;

		configParameters.frameGenerationCallback = [](ffxDispatchDescFrameGeneration* params, void* pUserCtx) -> ffxReturnCode_t {
			return ffxModule.Dispatch(reinterpret_cast<ffxContext*>(pUserCtx), &params->header);
		};
		configParameters.frameGenerationCallbackUserContext = &frameGenContext;

		configParameters.HUDLessColor = ffxApiGetResourceDX12(HUDLessColor);

	} else {
		configParameters.frameGenerationEnabled = false;

		configParameters.frameGenerationCallbackUserContext = nullptr;
		configParameters.frameGenerationCallback = nullptr;

		configParameters.HUDLessColor = FfxApiResource({});
	}

	static uint64_t frameID = 0;
	configParameters.frameID = frameID;
	configParameters.swapChain = swapChain->swapChain;
	configParameters.onlyPresentGenerated = false;
	configParameters.allowAsyncWorkloads = true;
	configParameters.flags = 0;

	auto state = globals::state;

	auto wasUpscaled = upscaling->wasUpscaled;

	auto screenSize = state->screenSize;
	auto renderSize = wasUpscaled ? Util::ConvertToDynamic(state->screenSize, true) : screenSize;

	configParameters.generationRect.left = (swapChain->swapChainDesc.Width - swapChain->swapChainDesc.Width) / 2;
	configParameters.generationRect.top = (swapChain->swapChainDesc.Height - swapChain->swapChainDesc.Height) / 2;
	configParameters.generationRect.width = swapChain->swapChainDesc.Width;
	configParameters.generationRect.height = swapChain->swapChainDesc.Height;

	if (ffx::Configure(frameGenContext, configParameters) != ffx::ReturnCode::Ok) {
		logger::critical("[FidelityFX] Failed to configure frame generation!");
	}

	ffx::ConfigureDescFrameGenerationSwapChainRegisterUiResourceDX12 uiConfig{};
	uiConfig.uiResource = ffxApiGetResourceDX12(swapChain->uiBuffersWrapped[swapChain->frameIndex]->resource.get());
	uiConfig.flags = FFX_FRAMEGENERATION_UI_COMPOSITION_FLAG_USE_PREMUL_ALPHA;

	if (ffx::Configure(swapChainContext, uiConfig) != ffx::ReturnCode::Ok) {
		logger::critical("[FidelityFX] Failed to configure UI composition!");
	}

	if (a_useFrameGeneration) {
		ffx::DispatchDescFrameGenerationPrepare dispatchParameters{};

		dispatchParameters.commandList = commandList;

		dispatchParameters.motionVectorScale.x = renderSize.x;
		dispatchParameters.motionVectorScale.y = renderSize.y;
		dispatchParameters.renderSize.width = static_cast<uint32_t>(renderSize.x);
		dispatchParameters.renderSize.height = static_cast<uint32_t>(renderSize.y);

		auto gameViewport = globals::game::graphicsState;

		if (wasUpscaled) {
			float2 jitter;

			if (globals::game::isVR)
				jitter.x = -gameViewport->projectionPosScaleX * renderSize.x;
			else
				jitter.x = -gameViewport->projectionPosScaleX * renderSize.x / 2.0f;

			jitter.y = gameViewport->projectionPosScaleY * renderSize.y / 2.0f;

			dispatchParameters.jitterOffset.x = -jitter.x;
			dispatchParameters.jitterOffset.y = -jitter.y;
		} else {
			dispatchParameters.jitterOffset.x = 0.0f;
			dispatchParameters.jitterOffset.y = 0.0f;
		}

		dispatchParameters.frameTimeDelta = *globals::game::deltaTime * 1000.f;

		dispatchParameters.cameraFar = *globals::game::cameraFar;
		dispatchParameters.cameraNear = *globals::game::cameraNear;

		dispatchParameters.cameraFovAngleVertical = Util::GetVerticalFOVRad();
		dispatchParameters.viewSpaceToMetersFactor = 0.01428222656f;

		dispatchParameters.frameID = frameID;

		dispatchParameters.depth = ffxApiGetResourceDX12(depth);
		dispatchParameters.motionVectors = ffxApiGetResourceDX12(motionVectors);

		if (ffx::Dispatch(frameGenContext, dispatchParameters) != ffx::ReturnCode::Ok) {
			logger::critical("[FidelityFX] Failed to dispatch frame generation!");
		}

		ffx::DispatchDescFrameGenerationPrepareCameraInfo cameraConfig{};

		auto viewMatrix = globals::game::frameBufferCached.GetCameraViewInverse().Transpose();
		auto cameraViewToClip = globals::game::frameBufferCached.GetCameraProjUnjittered().Transpose();

		cameraConfig.cameraRight[0] = viewMatrix._11;
		cameraConfig.cameraRight[1] = viewMatrix._12;
		cameraConfig.cameraRight[2] = viewMatrix._13;

		cameraConfig.cameraUp[0] = viewMatrix._21;
		cameraConfig.cameraUp[1] = viewMatrix._22;
		cameraConfig.cameraUp[2] = viewMatrix._23;

		cameraConfig.cameraForward[0] = viewMatrix._31;
		cameraConfig.cameraForward[1] = viewMatrix._32;
		cameraConfig.cameraForward[2] = viewMatrix._33;

		cameraConfig.cameraPosition[0] = globals::game::frameBufferCached.GetCameraPosAdjust().x;
		cameraConfig.cameraPosition[1] = globals::game::frameBufferCached.GetCameraPosAdjust().y;
		cameraConfig.cameraPosition[2] = globals::game::frameBufferCached.GetCameraPosAdjust().z;

		if (ffx::Dispatch(frameGenContext, dispatchParameters, cameraConfig) != ffx::ReturnCode::Ok) {
			logger::critical("[FidelityFX] Failed to dispatch frame generation camera info!");
		}
	}

	frameID++;

	// Set isFrameGenActive based on whether FSR3 frame generation is enabled
	isFrameGenActive = a_useFrameGeneration;
}


void FidelityFX::CreateFSRResources()
{
	auto state = globals::state;
	auto upscaling = globals::upscaling;

	ffx::CreateContextDescUpscale createUpscaling;
	createUpscaling.maxRenderSize.width = (uint)state->screenSize.x;
	createUpscaling.maxRenderSize.height = (uint)state->screenSize.y;
	createUpscaling.maxUpscaleSize.width = (uint)state->screenSize.x;
	createUpscaling.maxUpscaleSize.height = (uint)state->screenSize.y;
	createUpscaling.flags = FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE | FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION;

	createUpscaling.fpMessage = [](uint32_t type, const wchar_t* wideMessage) {
		auto message = stl::utf16_to_utf8(wideMessage);
		if (message.has_value()) {
			if (type == FFX_API_MESSAGE_TYPE_ERROR)
				logger::error("[FidelityFX] {}", message.value());
			else
				logger::warn("[FidelityFX] {}", message.value());
		}
	};

	ffx::CreateBackendDX12Desc backendDesc{};
	backendDesc.device = upscaling->sharedD3D12Device.get();

	if (ffx::CreateContext(upscalingContext, nullptr, createUpscaling, backendDesc) != ffx::ReturnCode::Ok)
		logger::critical("[FidelityFX] Failed to create FSR3 API context");
}

void FidelityFX::DestroyFSRResources()
{
	if (ffx::DestroyContext(upscalingContext) != ffx::ReturnCode::Ok)
		logger::critical("[FidelityFX] Failed to destroy FSR3 API context");
}

float FidelityFX::GetInputResolutionScale([[maybe_unused]] uint32_t outputWidth, [[maybe_unused]] uint32_t outputHeight, uint32_t qualityMode)
{
	float upscaleRatio = 1.0f;

	ffx::QueryDescUpscaleGetUpscaleRatioFromQualityMode query{};
	query.qualityMode = qualityMode;
	query.pOutUpscaleRatio = &upscaleRatio;

	if (ffx::Query(upscalingContext, query) != ffx::ReturnCode::Ok) {
		logger::critical("[FidelityFX] Failed to query upscale ratio for quality preset {}", qualityMode);
		return 1.0f;
	}

	// Convert upscale ratio to resolution scale (input resolution / output resolution)
	float resolutionScale = 1.0f / upscaleRatio;

	return resolutionScale;
}

void FidelityFX::Upscale(
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
	auto state = globals::state;

	ffx::DispatchDescUpscale dispatchUpscale{};

	dispatchUpscale.commandList = a_commandList;
	dispatchUpscale.color = ffxApiGetResourceDX12(a_inputColorTexture, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
	dispatchUpscale.depth = ffxApiGetResourceDX12(a_depthTexture, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
	dispatchUpscale.motionVectors = ffxApiGetResourceDX12(a_motionVectorTexture, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
	dispatchUpscale.output = ffxApiGetResourceDX12(a_outputTexture, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
	dispatchUpscale.exposure = ffxApiGetResourceDX12(nullptr, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
	dispatchUpscale.reactive = ffxApiGetResourceDX12(a_reactiveMask, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
	dispatchUpscale.transparencyAndComposition = ffxApiGetResourceDX12(nullptr, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);

	dispatchUpscale.jitterOffset.x = -a_jitter.x;
	dispatchUpscale.jitterOffset.y = -a_jitter.y;
	dispatchUpscale.motionVectorScale.x = (float)a_renderWidth;
	dispatchUpscale.motionVectorScale.y = (float)a_renderHeight;
	dispatchUpscale.reset = false;
	dispatchUpscale.enableSharpening = true;
	dispatchUpscale.sharpness = 0.0f;

	dispatchUpscale.frameTimeDelta = static_cast<float>(*globals::game::deltaTime * 1000.f);

	dispatchUpscale.preExposure = 1.0f;
	dispatchUpscale.renderSize.width = a_renderWidth;
	dispatchUpscale.renderSize.height = a_renderHeight;
	dispatchUpscale.upscaleSize.width = (uint32_t)state->screenSize.x;
	dispatchUpscale.upscaleSize.height = (uint32_t)state->screenSize.y;

	dispatchUpscale.cameraFovAngleVertical = Util::GetVerticalFOVRad();

	dispatchUpscale.cameraFar = *globals::game::cameraFar;
	dispatchUpscale.cameraNear = *globals::game::cameraNear;

	dispatchUpscale.viewSpaceToMetersFactor = 0.01428222656f;

	dispatchUpscale.flags = FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB;

	if (ffx::Dispatch(upscalingContext, dispatchUpscale) != ffx::ReturnCode::Ok)
		logger::critical("[FidelityFX] Failed to upscale");	
}