#include "FidelityFX.h"

#include "Upscaling.h"

#include "State.h"
#include "Util.h"

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

void FidelityFX::Init()
{
	dll = LoadLibrary(L"Data\\SKSE\\Plugins\\FidelityFX\\amd_fidelityfx_dx12.dll");

	ffxLoadFunctions(&ffxModule, dll);
}

void FidelityFX::WrapSwapChain()
{
	auto swapChain = DX12SwapChain::GetSingleton();

	ffx::CreateContextDescFrameGenerationSwapChainWrapDX12 desc{};
	desc.swapchain = swapChain->swapChain.put();
	desc.gameQueue = swapChain->commandQueue.get();

	ffx::Context swapChainContext{};

	ffx::CreateContext(swapChainContext, nullptr, desc);
}

void FidelityFX::CreateFrameGenerationResources()
{
	auto swapChain = DX12SwapChain::GetSingleton();

	ffx::CreateContextDescFrameGeneration createFg{};
	createFg.displaySize = { swapChain->swapChainDesc.Width, swapChain->swapChainDesc.Height };
	createFg.maxRenderSize = createFg.displaySize;
	createFg.flags = 0;
	createFg.backBufferFormat = FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;

	ffx::CreateBackendDX12Desc createBackend{};
	createBackend.device = swapChain->d3d12Device.get();

	ffx::CreateContext(frameGenContext, nullptr, createFg, createBackend);
}

void FidelityFX::ConfigureFrameGeneration()
{
	// Update frame generation config

	auto upscaling = Upscaling::GetSingleton();

	ffx::ConfigureDescFrameGeneration configParameters{};

	configParameters.frameGenerationEnabled = true;
	configParameters.flags = 0;
	//configParameters.flags |= m_DrawFrameGenerationDebugTearLines ? FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_TEAR_LINES : 0;
	//configParameters.flags |= m_DrawFrameGenerationDebugResetIndicators ? FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_RESET_INDICATORS : 0;
	//configParameters.flags |= m_DrawFrameGenerationDebugView ? FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_VIEW : 0;
	configParameters.HUDLessColor = ffxApiGetResourceDX12(upscaling->colorBufferShared12.get(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
	configParameters.allowAsyncWorkloads = false;
	// assume symmetric letterbox

	auto swapChain = DX12SwapChain::GetSingleton();

	configParameters.generationRect.left = (swapChain->swapChainDesc.Width - swapChain->swapChainDesc.Width) / 2;
	configParameters.generationRect.top = (swapChain->swapChainDesc.Height - swapChain->swapChainDesc.Height) / 2;
	configParameters.generationRect.width = swapChain->swapChainDesc.Width;
	configParameters.generationRect.height = swapChain->swapChainDesc.Height;

	configParameters.frameGenerationCallback = nullptr;
	configParameters.frameGenerationCallbackUserContext = nullptr;

	configParameters.onlyPresentGenerated = false;

	static uint64_t frameID = 0;
	configParameters.frameID = frameID;

	configParameters.swapChain = swapChain->swapChain.get();

	ffx::Configure(frameGenContext, configParameters);

	ffx::DispatchDescFrameGenerationPrepare dispatchParameters{};

	dispatchParameters.commandList = swapChain->commandList.get();

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

	dispatchParameters.depth = ffxApiGetResourceDX12(upscaling->depthBufferShared12.get(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
	dispatchParameters.motionVectors = ffxApiGetResourceDX12(swapChain->renderTargetsD3D12[RE::RENDER_TARGETS::RENDER_TARGET::kMOTION_VECTOR].d3d12Resource.get(), FFX_API_RESOURCE_STATE_COMPUTE_READ);

	ffx::Dispatch(frameGenContext, dispatchParameters);

	frameID++;
}

void FidelityFX::CreateFSRResources()
{
	auto state = State::GetSingleton();

	auto fsrDevice = ffxGetDeviceDX11(state->device);

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
	static auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	static auto& depthTexture = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
	static auto& motionVectorsTexture = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kMOTION_VECTOR];

	static auto gameViewport = RE::BSGraphics::State::GetSingleton();
	static auto context = reinterpret_cast<ID3D11DeviceContext*>(renderer->GetRuntimeData().context);

	auto state = State::GetSingleton();

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

		dispatchParameters.motionVectorScale.x = state->isVR ? state->screenSize.x / 2 : state->screenSize.x;
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
