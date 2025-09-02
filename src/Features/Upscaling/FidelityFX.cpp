#include "FidelityFX.h"

#include <directx/d3dx12.h>

#include "../../State.h"
#include "../../Utils/FileSystem.h"
#include "../Upscaling.h"
#include "DX12SwapChain.h"

ffxFunctions ffxModule;

// Define the static member
std::vector<std::pair<std::string, std::string>> FidelityFX::dllVersions = {};

void FidelityFX::LoadFFX()
{
	// Load upscaler and frame generation DLLs and their function pointers
	std::wstring upscalerDllName = L"amd_fidelityfx_upscaler_dx12.dll";
	std::wstring framegenDllName = L"amd_fidelityfx_framegeneration_dx12.dll";

	std::wstring upscalerPath = std::wstring(FidelityFX::PluginDir) + L"\\" + upscalerDllName;
	std::wstring framegenPath = std::wstring(FidelityFX::PluginDir) + L"\\" + framegenDllName;

	featureFSR3 = LoadLibrary(upscalerPath.c_str());
	featureFSR3FG = LoadLibrary(framegenPath.c_str());

	// Load loader DLL from plugin directory
	std::wstring loaderDllName = L"amd_fidelityfx_loader_dx12.dll";
	std::wstring pluginLoaderPath = std::wstring(FidelityFX::PluginDir) + L"\\" + loaderDllName;

	module = LoadLibrary(pluginLoaderPath.c_str());

	// Cache all DLL versions in the FidelityFX directory
	std::filesystem::path pluginDir = std::filesystem::path(FidelityFX::PluginDir);
	FidelityFX::dllVersions = Util::EnumerateDllVersions(pluginDir);

	if (module) {
		logger::info("[FidelityFX] Loader DLL loaded successfully from plugin directory");

		ffxLoadFunctions(&ffxModule, module);

		if (featureFSR3) {
			logger::info("[FidelityFX] Upscaler DLL found and available");
		} else {
			logger::warn("[FidelityFX] Upscaler DLL not found - FSR3 upscaling disabled");
		}

		if (featureFSR3FG) {
			logger::info("[FidelityFX] Frame generation DLL found and available");
		} else {
			logger::warn("[FidelityFX] Frame generation DLL not found - FSR3 frame generation disabled");
		}
	} else {
		logger::error("[FidelityFX] Failed to load {} from plugin directory",
			stl::utf16_to_utf8(loaderDllName).value_or("loader DLL"));
	}
}

void FidelityFX::SetupFrameGeneration()
{
	auto& swapChain = globals::features::upscaling.dx12SwapChain;

	ffx::CreateContextDescFrameGeneration createFg{};
	createFg.displaySize = { swapChain.swapChainDesc.Width, swapChain.swapChainDesc.Height };
	createFg.maxRenderSize = createFg.displaySize;
	createFg.flags = 0;
	createFg.backBufferFormat = ffxApiGetSurfaceFormatDX12(swapChain.swapChainDesc.Format);

	ffx::CreateBackendDX12Desc backendDesc{};
	backendDesc.device = swapChain.d3d12Device.get();

	if (ffx::CreateContext(frameGenContext, nullptr, createFg, backendDesc) != ffx::ReturnCode::Ok)
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
	auto& upscaling = globals::features::upscaling;
	auto& swapChain = globals::features::upscaling.dx12SwapChain;

	auto commandList = swapChain.commandLists[swapChain.frameIndex].get();

	auto HUDLessColor = upscaling.HUDLessBufferShared12.get();
	auto depth = upscaling.depthBufferShared12.get();
	auto motionVectors = upscaling.motionVectorBufferShared12.get();

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
	configParameters.swapChain = swapChain.swapChain;
	configParameters.onlyPresentGenerated = false;
	configParameters.allowAsyncWorkloads = true;
	configParameters.flags = FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_VIEW;

	configParameters.generationRect.left = (swapChain.swapChainDesc.Width - swapChain.swapChainDesc.Width) / 2;
	configParameters.generationRect.top = (swapChain.swapChainDesc.Height - swapChain.swapChainDesc.Height) / 2;
	configParameters.generationRect.width = swapChain.swapChainDesc.Width;
	configParameters.generationRect.height = swapChain.swapChainDesc.Height;

	if (ffx::Configure(frameGenContext, configParameters) != ffx::ReturnCode::Ok) {
		logger::critical("[FidelityFX] Failed to configure frame generation!");
	}

	if (a_useFrameGeneration) {
		ffx::DispatchDescFrameGenerationPrepare dispatchParameters{};

		dispatchParameters.commandList = commandList;

		dispatchParameters.motionVectorScale.x = (float)swapChain.swapChainDesc.Width;
		dispatchParameters.motionVectorScale.y = (float)swapChain.swapChainDesc.Height;
		dispatchParameters.renderSize.width = swapChain.swapChainDesc.Width;
		dispatchParameters.renderSize.height = swapChain.swapChainDesc.Height;

		auto gameViewport = globals::game::graphicsState;

		float2 jitter;

		if (globals::game::isVR)
			jitter.x = -gameViewport->projectionPosScaleX * float(swapChain.swapChainDesc.Width);
		else
			jitter.x = -gameViewport->projectionPosScaleX * float(swapChain.swapChainDesc.Width) / 2.0f;

		jitter.y = gameViewport->projectionPosScaleY * (float)swapChain.swapChainDesc.Height / 2.0f;

		dispatchParameters.jitterOffset.x = -jitter.x;
		dispatchParameters.jitterOffset.y = -jitter.y;

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
	}

	frameID++;

	// Set isFrameGenActive based on whether FSR3 frame generation is enabled
	isFrameGenActive = a_useFrameGeneration;
}

void FidelityFX::CreateFSRResources()
{
	auto state = globals::state;
	auto& upscaling = globals::features::upscaling;

	ffx::CreateContextDescUpscale createUpscaling;
	createUpscaling.maxRenderSize.width = (uint)state->screenSize.x;
	createUpscaling.maxRenderSize.height = (uint)state->screenSize.y;
	createUpscaling.maxUpscaleSize.width = (uint)state->screenSize.x;
	createUpscaling.maxUpscaleSize.height = (uint)state->screenSize.y;
	createUpscaling.flags = FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;

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
	backendDesc.device = upscaling.dx12SwapChain.d3d12Device.get();

	if (ffx::CreateContext(upscalingContext, nullptr, createUpscaling, backendDesc) != ffx::ReturnCode::Ok)
		logger::critical("[FidelityFX] Failed to create FSR3 API context");

	// Query version information after context creation
	QueryVersion();
}

void FidelityFX::DestroyFSRResources()
{
	if (ffx::DestroyContext(upscalingContext) != ffx::ReturnCode::Ok)
		logger::critical("[FidelityFX] Failed to destroy FSR3 API context");
	upscalingContext = {};
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
	ID3D12Resource* a_transparencyCompositionMask,
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
	dispatchUpscale.transparencyAndComposition = ffxApiGetResourceDX12(a_transparencyCompositionMask, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);

	dispatchUpscale.jitterOffset.x = -a_jitter.x;
	dispatchUpscale.jitterOffset.y = -a_jitter.y;
	dispatchUpscale.motionVectorScale.x = (globals::game::isVR ? 0.5f : 1.0f) * (float)a_renderWidth;
	dispatchUpscale.motionVectorScale.y = (float)a_renderHeight;
	dispatchUpscale.reset = false;
	dispatchUpscale.enableSharpening = true;
	dispatchUpscale.sharpness = 0.0f;

	dispatchUpscale.frameTimeDelta = static_cast<float>(RE::GetSecondsSinceLastFrame() * 1000.f);

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

void FidelityFX::QueryVersion()
{
	auto& upscaling = globals::features::upscaling;

	// Clear existing version info
	versionInfo.clear();

	// Query upscaler versions if available
	if (featureFSR3) {
		ffxQueryDescGetVersions upscalerQuery{};
		upscalerQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
		upscalerQuery.header.pNext = nullptr;

		ffx::CreateContextDescUpscale dummyUpscaler{};
		upscalerQuery.createDescType = dummyUpscaler.header.type;
		upscalerQuery.device = upscaling.dx12SwapChain.d3d12Device.get();

		uint64_t upscalerCount = 0;
		upscalerQuery.outputCount = &upscalerCount;
		upscalerQuery.versionIds = nullptr;
		upscalerQuery.versionNames = nullptr;

		if (ffxModule.Query(nullptr, &upscalerQuery.header) == (ffxReturnCode_t)ffx::ReturnCode::Ok && upscalerCount > 0) {
			// Allocate arrays for version info
			std::vector<uint64_t> upscalerIds(upscalerCount);
			std::vector<const char*> upscalerNames(upscalerCount);

			upscalerQuery.versionIds = upscalerIds.data();
			upscalerQuery.versionNames = upscalerNames.data();

			// Second query to get actual data
			if (ffxModule.Query(nullptr, &upscalerQuery.header) == (ffxReturnCode_t)ffx::ReturnCode::Ok) {
				if (upscalerCount > 0 && upscalerNames[0]) {
					versionInfo = upscalerNames[0];
					logger::info("[FidelityFX] Upscaler version: {}", versionInfo);
				}
			}
		}
	}
}