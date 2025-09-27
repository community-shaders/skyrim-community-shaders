#include "FidelityFX.h"

#include <directx/d3dx12.h>

#include "../../State.h"
#include "../../Utils/FileSystem.h"
#include "../Upscaling.h"
#include "DX12SwapChain.h"

ffxFunctions ffxModule;

std::vector<std::pair<std::string, std::string>> FidelityFX::dllVersions = {};

void FidelityFX::LoadFFX()
{
	// Load uframe generation DLL and its function pointers
	std::wstring framegenDllName = L"amd_fidelityfx_framegeneration_dx12.dll";
	std::wstring framegenPath = std::wstring(FidelityFX::PluginDir) + L"\\" + framegenDllName;
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
	createFg.flags = FFX_FRAMEGENERATION_ENABLE_ASYNC_WORKLOAD_SUPPORT;
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

	ffx::ConfigureDescFrameGeneration configParameters{};

	if (a_useFrameGeneration) {
		configParameters.frameGenerationEnabled = true;

		configParameters.frameGenerationCallback = [](ffxDispatchDescFrameGeneration* params, void* pUserCtx) -> ffxReturnCode_t {
			return ffxModule.Dispatch(reinterpret_cast<ffxContext*>(pUserCtx), &params->header);
		};

		configParameters.frameGenerationCallbackUserContext = &frameGenContext;

	} else {
		configParameters.frameGenerationEnabled = false;

		configParameters.frameGenerationCallbackUserContext = nullptr;
		configParameters.frameGenerationCallback = nullptr;
	}

	configParameters.HUDLessColor = FfxApiResource({});

	configParameters.presentCallback = nullptr;
	configParameters.presentCallbackUserContext = nullptr;

	static uint64_t frameID = 0;
	configParameters.frameID = frameID;
	configParameters.swapChain = swapChain.swapChain;
	configParameters.onlyPresentGenerated = false;
	configParameters.flags = 0;
	configParameters.allowAsyncWorkloads = true;

	auto state = globals::state;

	auto renderSize = state->screenSize * upscaling.resolutionScale;

	configParameters.generationRect.left = (swapChain.swapChainDesc.Width - swapChain.swapChainDesc.Width) / 2;
	configParameters.generationRect.top = (swapChain.swapChainDesc.Height - swapChain.swapChainDesc.Height) / 2;
	configParameters.generationRect.width = swapChain.swapChainDesc.Width;
	configParameters.generationRect.height = swapChain.swapChainDesc.Height;

	if (ffx::Configure(frameGenContext, configParameters) != ffx::ReturnCode::Ok) {
		logger::critical("[FidelityFX] Failed to configure frame generation!");
	}

	ffx::ConfigureDescFrameGenerationSwapChainRegisterUiResourceDX12 uiConfig{};
	uiConfig.uiResource = ffxApiGetResourceDX12(swapChain.uiBufferWrapped->resource.get());
	uiConfig.flags = FFX_FRAMEGENERATION_UI_COMPOSITION_FLAG_USE_PREMUL_ALPHA | FFX_FRAMEGENERATION_UI_COMPOSITION_FLAG_ENABLE_INTERNAL_UI_DOUBLE_BUFFERING;

	if (ffx::Configure(swapChainContext, uiConfig) != ffx::ReturnCode::Ok) {
		logger::critical("[FidelityFX] Failed to configure UI composition!");
	}

	if (a_useFrameGeneration) {
		ffx::DispatchDescFrameGenerationPrepare dispatchParameters{};

		dispatchParameters.commandList = swapChain.commandLists[swapChain.frameIndex].get();

		dispatchParameters.motionVectorScale.x = renderSize.x;
		dispatchParameters.motionVectorScale.y = renderSize.y;
		dispatchParameters.renderSize.width = static_cast<uint32_t>(renderSize.x);
		dispatchParameters.renderSize.height = static_cast<uint32_t>(renderSize.y);

		dispatchParameters.jitterOffset.x = -upscaling.jitter.x;
		dispatchParameters.jitterOffset.y = -upscaling.jitter.y;

		dispatchParameters.frameTimeDelta = RE::GetSecondsSinceLastFrame() * 1000.f;

		dispatchParameters.cameraFar = *globals::game::cameraFar;
		dispatchParameters.cameraNear = *globals::game::cameraNear;

		dispatchParameters.cameraFovAngleVertical = Util::GetVerticalFOVRad();
		dispatchParameters.viewSpaceToMetersFactor = 0.01428222656f;

		dispatchParameters.frameID = frameID;

		dispatchParameters.depth = ffxApiGetResourceDX12(swapChain.depthBufferShared12->resource.get());
		dispatchParameters.motionVectors = ffxApiGetResourceDX12(swapChain.motionVectorBufferShared12->resource.get());

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

	// Prevent multiple allocations
	if (fsrScratchBuffer) {
		logger::warn("[FidelityFX] FSR resources already created, skipping allocation");
		return;
	}

	auto fsrDevice = ffxGetDeviceDX11(globals::d3d::device);

	size_t scratchBufferSize = ffxGetScratchMemorySizeDX11(FFX_FSR3UPSCALER_CONTEXT_COUNT);
	fsrScratchBuffer = calloc(scratchBufferSize, 1);
	if (!fsrScratchBuffer) {
		logger::critical("[FidelityFX] Failed to allocate FSR3 scratch buffer memory!");
		return;
	}
	memset(fsrScratchBuffer, 0, scratchBufferSize);

	FfxInterface fsrInterface;
	if (ffxGetInterfaceDX11(&fsrInterface, fsrDevice, fsrScratchBuffer, scratchBufferSize, FFX_FSR3UPSCALER_CONTEXT_COUNT) != FFX_OK) {
		logger::critical("[FidelityFX] Failed to initialize FSR3 backend interface!");
		free(fsrScratchBuffer);
		fsrScratchBuffer = nullptr;
		return;
	}

	FfxFsr3ContextDescription contextDescription;
	contextDescription.maxRenderSize.width = (uint)state->screenSize.x;
	contextDescription.maxRenderSize.height = (uint)state->screenSize.y;
	contextDescription.maxUpscaleSize.width = (uint)state->screenSize.x;
	contextDescription.maxUpscaleSize.height = (uint)state->screenSize.y;
	contextDescription.displaySize.width = (uint)state->screenSize.x;
	contextDescription.displaySize.height = (uint)state->screenSize.y;
	contextDescription.flags = FFX_FSR3_ENABLE_UPSCALING_ONLY | FFX_FSR3_ENABLE_AUTO_EXPOSURE | FFX_FSR3_ENABLE_HIGH_DYNAMIC_RANGE;
	contextDescription.backendInterfaceUpscaling = fsrInterface;

	if (ffxFsr3ContextCreate(&fsrContext, &contextDescription) != FFX_OK) {
		logger::critical("[FidelityFX] Failed to initialize FSR3 context!");
		free(fsrScratchBuffer);
		fsrScratchBuffer = nullptr;
		return;
	}
}

void FidelityFX::DestroyFSRResources()
{
	if (ffxFsr3ContextDestroy(&fsrContext) != FFX_OK)
		logger::critical("[FidelityFX] Failed to destroy FSR3 context!");

	// Free the scratch buffer to prevent memory leak
	if (fsrScratchBuffer) {
		free(fsrScratchBuffer);
		fsrScratchBuffer = nullptr;
	}
}

float FidelityFX::GetInputResolutionScale([[maybe_unused]] uint32_t outputWidth, [[maybe_unused]] uint32_t outputHeight, uint32_t qualityMode)
{
	return 1.0f / ffxFsr3GetUpscaleRatioFromQualityMode((FfxFsr3QualityMode)qualityMode);
}

FfxResource ffxGetResource(ID3D11Resource* dx11Resource,
	[[maybe_unused]] wchar_t const* ffxResName,
	FfxResourceStates state = FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ)
{
	FfxResource resource = {};
	resource.resource = reinterpret_cast<void*>(const_cast<ID3D11Resource*>(dx11Resource));
	resource.state = state;
	resource.description = GetFfxResourceDescriptionDX11(dx11Resource);

#ifdef _DEBUG
	if (ffxResName) {
		wcscpy_s(resource.name, ffxResName);
	}
#endif

	return resource;
}

void FidelityFX::Upscale(ID3D11Resource* a_upscalingTexture, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors, float a_sharpness)
{
	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;
	auto state = globals::state;
	auto& depthTexture = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

	auto screenSize = state->screenSize;
	auto renderSize = Util::ConvertToDynamic(screenSize);

	{
		FfxFsr3DispatchUpscaleDescription dispatchParameters{};

		dispatchParameters.commandList = ffxGetCommandListDX11(context);
		dispatchParameters.color = ffxGetResource(a_upscalingTexture, L"FSR3_Input_OutputColor");
		dispatchParameters.depth = ffxGetResource(depthTexture.texture, L"FSR3_InputDepth");
		dispatchParameters.motionVectors = ffxGetResource(a_motionVectors, L"FSR3_InputMotionVectors");
		dispatchParameters.exposure = ffxGetResource(nullptr, L"FSR3_InputExposure");
		dispatchParameters.upscaleOutput = dispatchParameters.color;
		dispatchParameters.reactive = ffxGetResource(a_reactiveMask, L"FSR3_InputReactiveMap");
		dispatchParameters.transparencyAndComposition = ffxGetResource(a_transparencyCompositionMask, L"FSR3_TransparencyAndCompositionMap");

		dispatchParameters.motionVectorScale.x = globals::game::isVR ? renderSize.x * 0.5f : renderSize.x;
		dispatchParameters.motionVectorScale.y = renderSize.y;
		dispatchParameters.renderSize.width = (uint)renderSize.x;
		dispatchParameters.renderSize.height = (uint)renderSize.y;

		auto& upscaling = globals::features::upscaling;
		auto jitter = upscaling.jitter;

		dispatchParameters.jitterOffset.x = -jitter.x;
		dispatchParameters.jitterOffset.y = -jitter.y;

		dispatchParameters.frameTimeDelta = *globals::game::deltaTime * 1000.f;

		dispatchParameters.cameraFar = *globals::game::cameraFar;
		dispatchParameters.cameraNear = *globals::game::cameraNear;

		dispatchParameters.enableSharpening = true;
		dispatchParameters.sharpness = a_sharpness;

		dispatchParameters.cameraFovAngleVertical = Util::GetVerticalFOVRad();
		dispatchParameters.viewSpaceToMetersFactor = 0.01428222656f;
		dispatchParameters.reset = false;
		dispatchParameters.preExposure = 1.0f;

		dispatchParameters.flags = 0;

		if (ffxFsr3ContextDispatchUpscale(&fsrContext, &dispatchParameters) != FFX_OK)
			logger::critical("[FidelityFX] Failed to dispatch upscaling!");
	}
}