#include "FidelityFX.h"

#include "State.h"
#include "Upscaling.h"

#include "DX12SwapChain.h"
#include <dx12/ffx_api_dx12.hpp>

ffxFunctions ffxModule;

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
	module = LoadLibrary(L"Data\\SKSE\\Plugins\\FidelityFX\\amd_fidelityfx_dx12.dll");

	if (module)
		ffxLoadFunctions(&ffxModule, module);
}

void FidelityFX::SetupFrameGeneration()
{
	auto swapChain = DX12SwapChain::GetSingleton();

	ffx::CreateContextDescFrameGeneration createFg{};
	createFg.displaySize = { swapChain->swapChainDesc.Width, swapChain->swapChainDesc.Height };
	createFg.maxRenderSize = createFg.displaySize;
	createFg.flags = FFX_FRAMEGENERATION_ENABLE_ASYNC_WORKLOAD_SUPPORT;
	createFg.backBufferFormat = FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;

	ffx::CreateBackendDX12Desc createBackend{};
	createBackend.device = swapChain->d3d12Device.get();

	if (ffx::CreateContext(frameGenContext, nullptr, createFg, createBackend) != ffx::ReturnCode::Ok) {
		logger::critical("[FidelityFX] Failed to create frame generation context!");
	}

	ffx::CreateContextDescFrameGenerationSwapChainWrapDX12 desc{};
	desc.swapchain = &swapChain->swapChain;
	desc.gameQueue = swapChain->commandQueue.get();

	if (ffx::CreateContext(swapChainContext, nullptr, desc) != ffx::ReturnCode::Ok) {
		logger::critical("[FidelityFX] Failed to create swap chain context!");
	}
}

void FidelityFX::Present(bool a_useFrameGeneration)
{
	auto upscaling = globals::upscaling;
	auto swapChain = DX12SwapChain::GetSingleton();
	auto commandList = swapChain->commandLists[swapChain->frameIndex].get();

	auto HUDLessColor = upscaling->colorBufferShared12.get();
	auto depth = upscaling->depthBufferShared12.get();
	auto motionVectors = upscaling->motionVectorBufferShared12.get();

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

	ffx::ConfigureDescFrameGenerationSwapChainRegisterUiResourceDX12 uiConfig{};
	uiConfig.uiResource = ffxApiGetResourceDX12(swapChain->uiBuffersWrapped[swapChain->frameIndex]->resource.get());
	uiConfig.flags = FFX_FRAMEGENERATION_UI_COMPOSITION_FLAG_USE_PREMUL_ALPHA;

	if (ffx::Configure(swapChainContext, uiConfig) != ffx::ReturnCode::Ok) {
		logger::critical("[FidelityFX] Failed to configure UI composition!");
	}

	if (a_useFrameGeneration) {
		ffx::DispatchDescFrameGenerationPrepare dispatchParameters{};

		dispatchParameters.commandList = commandList;

		dispatchParameters.motionVectorScale.x = (float)swapChain->swapChainDesc.Width;
		dispatchParameters.motionVectorScale.y = (float)swapChain->swapChainDesc.Height;
		dispatchParameters.renderSize.width = swapChain->swapChainDesc.Width;
		dispatchParameters.renderSize.height = swapChain->swapChainDesc.Height;
		dispatchParameters.jitterOffset.x = 0;
		dispatchParameters.jitterOffset.y = 0;

		static float& deltaTime = (*(float*)REL::RelocationID(523660, 410199).address());
		dispatchParameters.frameTimeDelta = deltaTime * 1000.f;

		static float& cameraNear = (*(float*)(REL::RelocationID(517032, 403540).address() + 0x40));
		static float& cameraFar = (*(float*)(REL::RelocationID(517032, 403540).address() + 0x44));
		dispatchParameters.cameraFar = cameraFar;
		dispatchParameters.cameraNear = cameraNear;

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
	contextDescription.maxRenderSize.width = (uint)state->screenSize.x;
	contextDescription.maxRenderSize.height = (uint)state->screenSize.y;
	contextDescription.maxUpscaleSize.width = (uint)state->screenSize.x;
	contextDescription.maxUpscaleSize.height = (uint)state->screenSize.y;
	contextDescription.displaySize.width = (uint)state->screenSize.x;
	contextDescription.displaySize.height = (uint)state->screenSize.y;
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

void FidelityFX::Upscale(Texture2D* a_color, Texture2D* a_alphaMask, float2 a_jitter, bool a_reset, float a_sharpness)
{
	static auto renderer = globals::game::renderer;
	static auto context = globals::d3d::context;
	auto state = globals::state;
	static auto& depthTexture = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
	static auto& motionVectorsTexture = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kMOTION_VECTOR];

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
		dispatchParameters.renderSize.width = (uint)state->screenSize.x;
		dispatchParameters.renderSize.height = (uint)state->screenSize.y;
		dispatchParameters.jitterOffset.x = -a_jitter.x;
		dispatchParameters.jitterOffset.y = -a_jitter.y;

		static float& deltaTime = (*(float*)REL::RelocationID(523660, 410199).address());
		dispatchParameters.frameTimeDelta = deltaTime * 1000.f;

		static float& cameraNear = (*(float*)(REL::RelocationID(517032, 403540).address() + 0x40));
		static float& cameraFar = (*(float*)(REL::RelocationID(517032, 403540).address() + 0x44));
		dispatchParameters.cameraFar = cameraFar;
		dispatchParameters.cameraNear = cameraNear;

		dispatchParameters.enableSharpening = true;
		dispatchParameters.sharpness = a_sharpness;

		dispatchParameters.cameraFovAngleVertical = Util::GetVerticalFOVRad();
		dispatchParameters.viewSpaceToMetersFactor = 0.01428222656f;
		dispatchParameters.reset = a_reset;
		dispatchParameters.preExposure = 1.0f;

		dispatchParameters.flags = 0;

		if (ffxFsr3ContextDispatchUpscale(&fsrContext, &dispatchParameters) != FFX_OK)
			logger::critical("[FidelityFX] Failed to dispatch upscaling!");
	}
}
