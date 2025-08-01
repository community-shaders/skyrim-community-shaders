#include "FidelityFX.h"
#include "Utils/FileSystem.h"

#include "State.h"
#include "Upscaling.h"

#include "DX12SwapChain.h"
#include <dx12/ffx_api_dx12.hpp>

ffxFunctions ffxModule;

// Define the static member
std::vector<std::pair<std::string, std::string>> FidelityFX::dllVersions = {};

FfxResource ffxGetResource(ID3D11Resource* dx11Resource,
	[[maybe_unused]] wchar_t const* ffxResName,
	FfxResourceStates state /*=FFX_RESOURCE_STATE_COMPUTE_READ*/)
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
	} else {
		featureFSR3FG = false;
	}
}

void FidelityFX::SetupFrameGeneration()
{
	auto swapChain = globals::dx12SwapChain;

	ffx::CreateContextDescFrameGeneration createFg{};
	createFg.displaySize = { swapChain->swapChainDesc.Width, swapChain->swapChainDesc.Height };
	createFg.maxRenderSize = createFg.displaySize;
	createFg.flags = FFX_FRAMEGENERATION_ENABLE_ASYNC_WORKLOAD_SUPPORT;
	createFg.backBufferFormat = ffxApiGetSurfaceFormatDX12(swapChain->swapChainDesc.Format);

	ffx::CreateBackendDX12Desc createBackend{};
	createBackend.device = swapChain->d3d12Device.get();

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

	auto HUDLessColor = upscaling->HUDLessBufferShared12.get();
	auto depth = upscaling->depthBufferShared12.get();
	auto motionVectors = upscaling->motionVectorBufferShared12.get();

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

	configParameters.generationRect.left = (swapChain->swapChainDesc.Width - swapChain->swapChainDesc.Width) / 2;
	configParameters.generationRect.top = (swapChain->swapChainDesc.Height - swapChain->swapChainDesc.Height) / 2;
	configParameters.generationRect.width = swapChain->swapChainDesc.Width;
	configParameters.generationRect.height = swapChain->swapChainDesc.Height;

	if (ffx::Configure(frameGenContext, configParameters) != ffx::ReturnCode::Ok) {
		logger::critical("[FidelityFX] Failed to configure frame generation!");
	}

	if (a_useFrameGeneration) {
		ffx::DispatchDescFrameGenerationPrepare dispatchParameters{};

		dispatchParameters.commandList = commandList;

		dispatchParameters.motionVectorScale.x = (float)swapChain->swapChainDesc.Width;
		dispatchParameters.motionVectorScale.y = (float)swapChain->swapChainDesc.Height;
		dispatchParameters.renderSize.width = swapChain->swapChainDesc.Width;
		dispatchParameters.renderSize.height = swapChain->swapChainDesc.Height;

		auto gameViewport = globals::game::graphicsState;

		float2 jitter;

		if (globals::game::isVR)
			jitter.x = -gameViewport->projectionPosScaleX * float(swapChain->swapChainDesc.Width);
		else
			jitter.x = -gameViewport->projectionPosScaleX * float(swapChain->swapChainDesc.Width) / 2.0f;

		jitter.y = gameViewport->projectionPosScaleY * (float)swapChain->swapChainDesc.Height / 2.0f;

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

		ffx::DispatchDescFrameGenerationPrepareCameraInfo cameraConfig{};

		auto viewMatrix = globals::upscaling->frameBufferCached.CameraViewInverse.Transpose();
		auto cameraViewToClip = globals::upscaling->frameBufferCached.CameraProjUnjittered.Transpose();

		cameraConfig.cameraRight[0] = viewMatrix._11;
		cameraConfig.cameraRight[1] = viewMatrix._12;
		cameraConfig.cameraRight[2] = viewMatrix._13;

		cameraConfig.cameraUp[0] = viewMatrix._21;
		cameraConfig.cameraUp[1] = viewMatrix._22;
		cameraConfig.cameraUp[2] = viewMatrix._23;

		cameraConfig.cameraForward[0] = viewMatrix._31;
		cameraConfig.cameraForward[1] = viewMatrix._32;
		cameraConfig.cameraForward[2] = viewMatrix._33;

		cameraConfig.cameraPosition[0] = globals::upscaling->frameBufferCached.CameraPosAdjust.x;
		cameraConfig.cameraPosition[1] = globals::upscaling->frameBufferCached.CameraPosAdjust.y;
		cameraConfig.cameraPosition[2] = globals::upscaling->frameBufferCached.CameraPosAdjust.z;

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
	auto fsrDevice = ffxGetDeviceDX11(globals::d3d::device);

	size_t scratchBufferSize = ffxGetScratchMemorySizeDX11(FFX_FSR3UPSCALER_CONTEXT_COUNT);
	void* scratchBuffer = calloc(scratchBufferSize, 1);
	memset(scratchBuffer, 0, scratchBufferSize);

	FfxInterface fsrInterface;
	if (ffxGetInterfaceDX11(&fsrInterface, fsrDevice, scratchBuffer, scratchBufferSize, FFX_FSR3UPSCALER_CONTEXT_COUNT) != FFX_OK)
		logger::critical("[FidelityFX] Failed to initialize FSR3 backend interface!");

	auto upscaling = globals::upscaling;

	FfxFsr3ContextDescription contextDescription;
	contextDescription.maxRenderSize.width = upscaling->renderSize[0];
	contextDescription.maxRenderSize.height = upscaling->renderSize[1];
	contextDescription.maxUpscaleSize.width = upscaling->outputSize[0];
	contextDescription.maxUpscaleSize.height = upscaling->outputSize[1];
	contextDescription.displaySize.width = upscaling->outputSize[0];
	contextDescription.displaySize.height = upscaling->outputSize[1];
	contextDescription.flags = FFX_FSR3_ENABLE_UPSCALING_ONLY | FFX_FSR3_ENABLE_AUTO_EXPOSURE;
	contextDescription.backBufferFormat = FFX_SURFACE_FORMAT_R8G8B8A8_UNORM;

	contextDescription.backendInterfaceUpscaling = fsrInterface;

	if (ffxFsr3ContextCreate(&fsrContext, &contextDescription) != FFX_OK)
		logger::critical("[FidelityFX] Failed to initialize FSR3 context!");
}

void FidelityFX::DestroyFSRResources()
{
	if (ffxFsr3ContextDestroy(&fsrContext) != FFX_OK)
		logger::critical("[FidelityFX] Failed to destroy FSR3 context!");
}

void FidelityFX::Upscale(ID3D11Resource* a_originalTexture, ID3D11Resource* a_upscaledTexture, Texture2D* a_alphaMask, float2 a_jitter, float a_sharpness)
{
	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;
	auto state = globals::state;
	auto& depthTexture = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
	auto& motionVectorsTexture = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kMOTION_VECTOR];

	{
		FfxFsr3DispatchUpscaleDescription dispatchParameters{};

		dispatchParameters.commandList = ffxGetCommandListDX11(context);
		dispatchParameters.color = ffxGetResource(a_originalTexture, L"FSR3_Input_Color", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
		dispatchParameters.depth = ffxGetResource(depthTexture.texture, L"FSR3_InputDepth", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
		dispatchParameters.motionVectors = ffxGetResource(motionVectorsTexture.texture, L"FSR3_InputMotionVectors", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
		dispatchParameters.exposure = ffxGetResource(nullptr, L"FSR3_InputExposure", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
		dispatchParameters.upscaleOutput = ffxGetResource(a_upscaledTexture, L"FSR3_Output_Color", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
		dispatchParameters.reactive = ffxGetResource(a_alphaMask->resource.get(), L"FSR3_InputReactiveMap", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
		dispatchParameters.transparencyAndComposition = ffxGetResource(nullptr, L"FSR3_TransparencyAndCompositionMap", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);

		auto upscaling = globals::upscaling;

		dispatchParameters.motionVectorScale.x = globals::game::isVR ? state->screenSize.x / 2 : state->screenSize.x;
		dispatchParameters.motionVectorScale.y = state->screenSize.y;
		dispatchParameters.renderSize.width = upscaling->renderSize[0];
		dispatchParameters.renderSize.height = upscaling->renderSize[1];
		dispatchParameters.upscaleSize.width = upscaling->outputSize[0];
		dispatchParameters.upscaleSize.height = upscaling->outputSize[1];
		dispatchParameters.jitterOffset.x = -a_jitter.x;
		dispatchParameters.jitterOffset.y = -a_jitter.y;

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
