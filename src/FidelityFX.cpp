#include "FidelityFX.h"

#include "State.h"
#include "Upscaling.h"

#include "DX12SwapChain.h"
#include "HDR.h"

#include <dx12/ffx_api_dx12.hpp>

ffxFunctions ffxModule;

FfxResource ffxGetResource(ID3D11Resource* dx11Resource,
	[[maybe_unused]] const wchar_t* ffxResName,
	FfxResourceStates state /*=FFX_RESOURCE_STATE_COMPUTE_READ*/)
{
	FfxResource resource = {};
	resource.resource = reinterpret_cast<void*>(dx11Resource);
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
	module = LoadLibrary(L"Data\\SKSE\\Plugins\\FidelityFX\\amd_fidelityfx_dx12.dll");

	if (module)
		ffxLoadFunctions(&ffxModule, module);
}

void FidelityFX::SetupFrameGeneration()
{
	auto swapChain = globals::dx12SwapChain;

	ffx::CreateContextDescFrameGeneration createFg{};
	createFg.displaySize = { swapChain->swapChainDesc.Width, swapChain->swapChainDesc.Height };
	createFg.maxRenderSize = createFg.displaySize;
	if (globals::hdr->settings.enabled) {
		createFg.flags = FFX_FRAMEGENERATION_ENABLE_ASYNC_WORKLOAD_SUPPORT | FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE;
		createFg.backBufferFormat = FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT;
	} else {
		createFg.flags = FFX_FRAMEGENERATION_ENABLE_ASYNC_WORKLOAD_SUPPORT;
		createFg.backBufferFormat = FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
	}

	ffx::CreateBackendDX12Desc createBackend{};
	createBackend.device = swapChain->d3d12Device.get();

	if (CreateContext(frameGenContext, nullptr, createFg, createBackend) != ffx::ReturnCode::Ok) {
		logger::critical("[FidelityFX] Failed to create frame generation context!");
	}
}

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

	if (Configure(swapChainContext, framePacingTuningParameters) != ffx::ReturnCode::Ok) {
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

	if (Configure(frameGenContext, configParameters) != ffx::ReturnCode::Ok) {
		logger::critical("[FidelityFX] Failed to configure frame generation!");
	}

	if (a_useFrameGeneration) {
		ffx::DispatchDescFrameGenerationPrepare dispatchParameters{};

		dispatchParameters.commandList = commandList;

		dispatchParameters.motionVectorScale.x = static_cast<float>(swapChain->swapChainDesc.Width);
		dispatchParameters.motionVectorScale.y = static_cast<float>(swapChain->swapChainDesc.Height);
		dispatchParameters.renderSize.width = swapChain->swapChainDesc.Width;
		dispatchParameters.renderSize.height = swapChain->swapChainDesc.Height;

		auto gameViewport = globals::game::graphicsState;

		float2 jitter;

		if (globals::game::isVR)
			jitter.x = -gameViewport->projectionPosScaleX * static_cast<float>(swapChain->swapChainDesc.Width);
		else
			jitter.x = -gameViewport->projectionPosScaleX * static_cast<float>(swapChain->swapChainDesc.Width) / 2.0f;

		jitter.y = gameViewport->projectionPosScaleY * static_cast<float>(swapChain->swapChainDesc.Height) / 2.0f;

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

		if (Dispatch(frameGenContext, dispatchParameters) != ffx::ReturnCode::Ok) {
			logger::critical("[FidelityFX] Failed to dispatch frame generation!");
		}
	}

	frameID++;
}

void FidelityFX::CreateFSRResources()
{
	auto state = globals::state;

	auto fsrDevice = ffxGetDeviceDX11(globals::d3d::device);

	size_t scratchBufferSize = ffxGetScratchMemorySizeDX11(FFX_FSR3UPSCALER_CONTEXT_COUNT);
	void* scratchBuffer = calloc(scratchBufferSize, 1);
	memset(scratchBuffer, 0, scratchBufferSize);

	FfxInterface fsrInterface;
	if (ffxGetInterfaceDX11(&fsrInterface, fsrDevice, scratchBuffer, scratchBufferSize, FFX_FSR3UPSCALER_CONTEXT_COUNT) != FFX_OK)
		logger::critical("[FidelityFX] Failed to initialize FSR3 backend interface!");

	FfxFsr3ContextDescription contextDescription;
	contextDescription.maxRenderSize.width = static_cast<uint>(state->screenSize.x);
	contextDescription.maxRenderSize.height = static_cast<uint>(state->screenSize.y);
	contextDescription.maxUpscaleSize.width = static_cast<uint>(state->screenSize.x);
	contextDescription.maxUpscaleSize.height = static_cast<uint>(state->screenSize.y);
	contextDescription.displaySize.width = static_cast<uint>(state->screenSize.x);
	contextDescription.displaySize.height = static_cast<uint>(state->screenSize.y);
	if (globals::hdr->settings.enabled) {
		contextDescription.flags = FFX_FSR3_ENABLE_UPSCALING_ONLY | FFX_FSR3_ENABLE_AUTO_EXPOSURE | FFX_FSR3_ENABLE_HIGH_DYNAMIC_RANGE;
		contextDescription.backBufferFormat = FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT;
	} else {
		contextDescription.flags = FFX_FSR3_ENABLE_UPSCALING_ONLY | FFX_FSR3_ENABLE_AUTO_EXPOSURE;
		contextDescription.backBufferFormat = FFX_SURFACE_FORMAT_R8G8B8A8_UNORM;
	}

	contextDescription.backendInterfaceUpscaling = fsrInterface;

	if (ffxFsr3ContextCreate(&fsrContext, &contextDescription) != FFX_OK)
		logger::critical("[FidelityFX] Failed to initialize FSR3 context!");
}

void FidelityFX::DestroyFSRResources()
{
	if (ffxFsr3ContextDestroy(&fsrContext) != FFX_OK)
		logger::critical("[FidelityFX] Failed to destroy FSR3 context!");
}

void FidelityFX::Upscale(Texture2D* a_color, Texture2D* a_alphaMask, float2 a_jitter, float a_sharpness)
{
	auto renderer = globals::game::renderer;
	auto context = globals::d3d::context;
	auto state = globals::state;
	auto& depthTexture = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
	auto& motionVectorsTexture = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kMOTION_VECTOR];

	{
		FfxFsr3DispatchUpscaleDescription dispatchParameters{};

		dispatchParameters.commandList = ffxGetCommandListDX11(context);
		dispatchParameters.color = ffxGetResource(a_color->resource.get(), L"FSR3_Input_OutputColor", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
		dispatchParameters.depth = ffxGetResource(depthTexture.texture, L"FSR3_InputDepth", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
		dispatchParameters.motionVectors = ffxGetResource(motionVectorsTexture.texture, L"FSR3_InputMotionVectors", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
		dispatchParameters.exposure = ffxGetResource(nullptr, L"FSR3_InputExposure", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
		dispatchParameters.upscaleOutput = dispatchParameters.color;
		dispatchParameters.reactive = ffxGetResource(a_alphaMask->resource.get(), L"FSR3_InputReactiveMap", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
		dispatchParameters.transparencyAndComposition = ffxGetResource(nullptr, L"FSR3_TransparencyAndCompositionMap", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);

		dispatchParameters.motionVectorScale.x = globals::game::isVR ? state->screenSize.x / 2 : state->screenSize.x;
		dispatchParameters.motionVectorScale.y = state->screenSize.y;
		dispatchParameters.renderSize.width = static_cast<uint>(state->screenSize.x);
		dispatchParameters.renderSize.height = static_cast<uint>(state->screenSize.y);
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
