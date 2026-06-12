#include "FidelityFX.h"

#include <directx/d3dx12.h>

#include "../../State.h"
#include "../../Utils/FileSystem.h"
#include "../HDRDisplay.h"
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
	for (const auto& [name, versionStr] : FidelityFX::dllVersions)
		logger::info("[FidelityFX] {} version: {}", name, versionStr);

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
void FidelityFX::Present(bool a_useFrameGeneration, bool a_isHDR)
{
	auto& upscaling = globals::features::upscaling;
	auto& swapChain = globals::features::upscaling.dx12SwapChain;

	// Cache peak nits first since we need HDR feature access
	auto* hdr = globals::features::hdrDisplay.loaded ? &globals::features::hdrDisplay : nullptr;
	float peakNits = hdr ? static_cast<float>(hdr->settings.hdrPeakNits) : 1000.0f;

	// Clamp peak nits to safe range [1.0f, 10000.0f] to prevent invalid values
	peakNits = std::clamp(peakNits, 1.0f, 10000.0f);

	// Detect if HDR parameters changed - if so, we need to reset FG history
	// because frames in the history were encoded with different parameters
	bool hdrParamsChanged = (a_isHDR != prevHDRActive) ||
	                        (a_isHDR && std::abs(peakNits - prevPeakNits) > 1.0f);

	// Update tracking for next frame
	prevHDRActive = a_isHDR;
	prevPeakNits = peakNits;

	// Store HDR state atomically for the callback to access (may be read from async thread)
	// Use seq_cst for both to ensure the callback sees both values consistently
	hdrPeakNits.store(peakNits, std::memory_order_seq_cst);
	isHDRActive.store(a_isHDR, std::memory_order_seq_cst);
	needsReset.store(hdrParamsChanged, std::memory_order_seq_cst);

	ffx::ConfigureDescFrameGeneration configParameters{};

	if (a_useFrameGeneration) {
		configParameters.frameGenerationEnabled = true;

		configParameters.frameGenerationCallback = [](ffxDispatchDescFrameGeneration* params, void* pUserCtx) -> ffxReturnCode_t {
			// Tell FidelityFX the color space so it can properly interpolate. Fixes pixel smearing that occured with HDR on.
			// PQ requires decoding to linear for correct motion interpolation
			// Read atomically with seq_cst since this callback may run on async thread
			bool hdrActive = FidelityFX::isHDRActive.load(std::memory_order_seq_cst);
			if (hdrActive) {
				params->backbufferTransferFunction = FFX_API_BACKBUFFER_TRANSFER_FUNCTION_PQ;
				// Set luminance range for PQ decoding (0 to peak nits)
				params->minMaxLuminance[0] = 0.0f;
				params->minMaxLuminance[1] = FidelityFX::hdrPeakNits.load(std::memory_order_seq_cst);
			} else {
				params->backbufferTransferFunction = FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
			}
			// Force reset when HDR parameters changed to clear internal buffers
			if (FidelityFX::needsReset.exchange(false, std::memory_order_seq_cst)) {
				params->reset = true;
			}
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

	// If HDR parameters changed, skip a frame ID to force FidelityFX to reset its history
	// This prevents interpolation artifacts when frames were encoded with different parameters
	// Per FidelityFX docs: "Any non-exactly-one difference will reset the frame generation logic"
	if (hdrParamsChanged && a_useFrameGeneration) {
		frameID += 2;  // Skip one ID to trigger reset
	}

	configParameters.frameID = frameID;
	configParameters.swapChain = swapChain.swapChain;
	configParameters.onlyPresentGenerated = false;
	configParameters.flags = 0;
	configParameters.allowAsyncWorkloads = true;

	auto renderSize = float2{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight } * upscaling.resolutionScale;

	configParameters.generationRect.left = (swapChain.swapChainDesc.Width - swapChain.swapChainDesc.Width) / 2;
	configParameters.generationRect.top = (swapChain.swapChainDesc.Height - swapChain.swapChainDesc.Height) / 2;
	configParameters.generationRect.width = swapChain.swapChainDesc.Width;
	configParameters.generationRect.height = swapChain.swapChainDesc.Height;

	if (ffx::Configure(frameGenContext, configParameters) != ffx::ReturnCode::Ok) {
		logger::critical("[FidelityFX] Failed to configure frame generation!");
	}

	// Register UI buffer with FidelityFX only when FG is active
	// When paused, UI is composited in HDROutputCS to avoid flickering from inconsistent FidelityFX compositing
	ffx::ConfigureDescFrameGenerationSwapChainRegisterUiResourceDX12 uiConfig{};
	if (a_useFrameGeneration) {
		uiConfig.uiResource = ffxApiGetResourceDX12(swapChain.uiBufferWrapped->resource.get());
		// Use both premultiplied alpha and double buffering for consistent blending
		uiConfig.flags = FFX_FRAMEGENERATION_UI_COMPOSITION_FLAG_USE_PREMUL_ALPHA |
		                 FFX_FRAMEGENERATION_UI_COMPOSITION_FLAG_ENABLE_INTERNAL_UI_DOUBLE_BUFFERING;
	} else {
		// No UI resource when FG is disabled - backbuffer already has UI composited
		uiConfig.uiResource = FfxApiResource({});
		uiConfig.flags = 0;
	}

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
	// Prevent multiple allocations
	if (fsrScratchBuffer) {
		logger::warn("[FidelityFX] FSR resources already created, skipping allocation");
		return;
	}

	auto fsrDevice = ffxGetDeviceDX11_Fsr31(globals::d3d::device);

	uint32_t numContexts = 1;
	size_t scratchBufferSize = ffxGetScratchMemorySizeDX11(numContexts);
	fsrScratchBuffer = calloc(scratchBufferSize, 1);
	if (!fsrScratchBuffer) {
		logger::critical("[FidelityFX] Failed to allocate FSR3 scratch buffer memory!");
		return;
	}
	memset(fsrScratchBuffer, 0, scratchBufferSize);

	FfxInterface fsrInterface;
	if (ffxGetInterfaceDX11(&fsrInterface, fsrDevice, fsrScratchBuffer, scratchBufferSize, numContexts) != FFX_OK) {
		logger::critical("[FidelityFX] Failed to initialize FSR3 backend interface!");
		free(fsrScratchBuffer);
		fsrScratchBuffer = nullptr;
		return;
	}

	float2 screenSize{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
	auto renderSize = Util::ConvertToDynamic(screenSize);

	uint32_t displayWidth = (uint32_t)screenSize.x;
	uint32_t displayHeight = (uint32_t)screenSize.y;
	uint32_t renderWidth = (uint32_t)renderSize.x;
	uint32_t renderHeight = (uint32_t)renderSize.y;

	FfxFsr3ContextDescription contextDescription;
	contextDescription.maxRenderSize.width = renderWidth;
	contextDescription.maxRenderSize.height = renderHeight;
	contextDescription.maxUpscaleSize.width = displayWidth;
	contextDescription.maxUpscaleSize.height = displayHeight;
	contextDescription.displaySize.width = displayWidth;
	contextDescription.displaySize.height = displayHeight;
	contextDescription.flags = FFX_FSR3_ENABLE_UPSCALING_ONLY | FFX_FSR3_ENABLE_AUTO_EXPOSURE;
	if (globals::features::hdrDisplay.loaded) {
		contextDescription.flags |= FFX_FSR3_ENABLE_HIGH_DYNAMIC_RANGE;
		contextDescription.backBufferFormat = FFX_SURFACE_FORMAT_R10G10B10A2_UNORM;
	} else {
		contextDescription.backBufferFormat = FFX_SURFACE_FORMAT_R8G8B8A8_UNORM;
	}
	contextDescription.backendInterfaceUpscaling = fsrInterface;

	if (ffxFsr3ContextCreate(&fsrContext[0], &contextDescription) != FFX_OK) {
		logger::critical("[FidelityFX] Failed to initialize FSR3 context!");
		free(fsrScratchBuffer);
		fsrScratchBuffer = nullptr;
		return;
	}
	logger::info("[FidelityFX] Created FSR3 context (Display: {}x{}, Render: {}x{})",
		displayWidth, displayHeight, renderWidth, renderHeight);
}

void FidelityFX::DestroyFSRResources()
{
	if (ffxFsr3ContextDestroy(&fsrContext[0]) != FFX_OK)
		logger::critical("[FidelityFX] Failed to destroy FSR3 context!");

	// Free the scratch buffer to prevent memory leak
	if (fsrScratchBuffer) {
		free(fsrScratchBuffer);
		fsrScratchBuffer = nullptr;
	}

	// Reset crash logging flag when resources are destroyed
	fsrDispatchCrashLogged = false;
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

	float2 screenSize{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
	auto renderSize = Util::ConvertToDynamic(screenSize);

	auto& upscaling = globals::features::upscaling;
	auto jitter = upscaling.jitter;

	if (state->frameAnnotations)
		state->BeginPerfEvent("FSR Dispatch");

	FfxFsr3DispatchUpscaleDescription dispatchParameters{};
	dispatchParameters.commandList = ffxGetCommandListDX11(context);
	dispatchParameters.color = ffxGetResource(a_upscalingTexture, L"FSR3_Input_OutputColor");
	dispatchParameters.depth = ffxGetResource(depthTexture.texture, L"FSR3_InputDepth");
	dispatchParameters.motionVectors = ffxGetResource(a_motionVectors, L"FSR3_InputMotionVectors");
	dispatchParameters.exposure = ffxGetResource(nullptr, L"FSR3_InputExposure");
	dispatchParameters.upscaleOutput = ffxGetResource(a_upscalingTexture, L"FSR3_OutputColor");
	dispatchParameters.reactive = ffxGetResource(a_reactiveMask, L"FSR3_InputReactiveMap");
	dispatchParameters.transparencyAndComposition = ffxGetResource(a_transparencyCompositionMask, L"FSR3_TransparencyAndCompositionMap");

	dispatchParameters.motionVectorScale.x = renderSize.x;
	dispatchParameters.motionVectorScale.y = renderSize.y;
	dispatchParameters.renderSize.width = (uint)renderSize.x;
	dispatchParameters.renderSize.height = (uint)renderSize.y;

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

	__try {
		if (ffxFsr3ContextDispatchUpscale(&fsrContext[0], &dispatchParameters) != FFX_OK)
			logger::critical("[FidelityFX] Failed to dispatch upscaling!");
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		if (!fsrDispatchCrashLogged) {
			logger::critical("[FidelityFX] FSR3 dispatch crashed - this may be caused by RenderDoc capture interfering with FSR operations. Try disabling RenderDoc capture.");
			fsrDispatchCrashLogged = true;
		}
	}

	if (state->frameAnnotations)
		state->EndPerfEvent();
}