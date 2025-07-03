#include "Streamline.h"

#include <dxgi.h>
#include <dxgi1_3.h>

#include "Hooks.h"
#include "State.h"
#include "Util.h"

#include "DX12SwapChain.h"
#include "Deferred.h"
#include "Upscaling.h"

#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss.h>
#include <sl_dlss_g.h>
#include <sl_matrix_helpers.h>
#include <sl_pcl.h>
#include <sl_reflex.h>
#include <sl_version.h>

#include <magic_enum.hpp>
#include <string>

// Helper to provide user-friendly explanations for sl::Result error codes
static std::string StreamlineResultExplanation(sl::Result result)
{
	switch (result) {
	case sl::Result::eOk:
		return "Success";
	case sl::Result::eErrorExceptionHandler:
		return "An unhandled exception occurred inside the Streamline SDK or plugin. This may indicate a missing or incompatible DLL, a bug in the SDK, or a problem with the driver. Check that all required DLLs are present and compatible, and try enabling Streamline's verbose logging for more details.";
	case sl::Result::eErrorFeatureNotSupported:
		return "Feature is not supported on this hardware or driver.";
	case sl::Result::eErrorDriverOutOfDate:
		return "GPU driver is too old for this feature.";
	case sl::Result::eErrorNoPlugins:
		return "Required plugin DLL is missing or failed to load.";
	case sl::Result::eErrorAdapterNotSupported:
		return "The current GPU/adapter is not supported for this feature.";
	case sl::Result::eErrorOSOutOfDate:
		return "Operating system is too old for this feature.";
	case sl::Result::eErrorInvalidParameter:
		return "Invalid parameter passed to Streamline API.";
	case sl::Result::eErrorMissingInputParameter:
		return "A required input parameter was missing.";
	case sl::Result::eErrorInitNotCalled:
		return "Streamline was not initialized before use.";
	case sl::Result::eErrorNotInitialized:
		return "Streamline is not initialized.";
	case sl::Result::eErrorFeatureFailedToLoad:
		return "Feature plugin failed to load (possibly missing DLL or dependency).";
	case sl::Result::eErrorNoSupportedAdapterFound:
		return "No supported GPU/adapter found for this feature (older or non-NVIDIA GPU, etc).";
	case sl::Result::eErrorOSDisabledHWS:
		return "Hardware-accelerated GPU scheduling (HWS) is disabled. Enable it in Windows graphics settings.";
	case sl::Result::eErrorDeviceNotCreated:
		return "D3D/Vulkan device was not created or is invalid.";
	case sl::Result::eErrorVulkanAPI:
		return "Vulkan API error occurred.";
	case sl::Result::eErrorDXGIAPI:
		return "DXGI API error occurred.";
	case sl::Result::eErrorD3DAPI:
		return "Direct3D API error occurred.";
	case sl::Result::eErrorMissingProxy:
		return "Missing required proxy DLL for Streamline integration.";
	case sl::Result::eErrorMissingResourceState:
		return "Missing required resource state for Streamline operation.";
	case sl::Result::eErrorInvalidIntegration:
		return "Invalid Streamline integration detected.";
	case sl::Result::eErrorComputeFailed:
		return "Compute operation failed in Streamline plugin.";
	case sl::Result::eErrorFeatureMissing:
		return "Requested feature is missing from the loaded plugins.";
	case sl::Result::eErrorFeatureMissingHooks:
		return "Feature is missing required hooks for operation.";
	case sl::Result::eErrorFeatureWrongPriority:
		return "Feature priority is incorrect or conflicts with another feature.";
	case sl::Result::eErrorFeatureMissingDependency:
		return "Feature is missing a required dependency plugin.";
	case sl::Result::eErrorFeatureManagerInvalidState:
		return "Feature manager is in an invalid state.";
	case sl::Result::eErrorInvalidState:
		return "Streamline is in an invalid state.";
	case sl::Result::eWarnOutOfVRAM:
		return "Warning: Out of VRAM. Performance or quality may be degraded.";
	default:
		{
			std::string enumName = std::string(magic_enum::enum_name(result));
			if (enumName.empty())
				enumName = "unknown";
			return std::format("Unknown error code ({}: {}). See Streamline SDK documentation for this error code.",
				std::to_underlying(result), enumName);
		}
	}
}

void LoggingCallback(sl::LogType type, const char* msg)
{
	// Remove trailing newlines from the raw message
	std::string rawMsg(msg);
	while (!rawMsg.empty() && (rawMsg.back() == '\n' || rawMsg.back() == '\r'))
		rawMsg.pop_back();

	// Remove leading bracketed metadata
	const char* p = msg;
	while (*p == '[') {
		const char* close = strchr(p, ']');
		if (!close)
			break;
		p = close + 1;
		// Skip whitespace after each bracketed section
		while (*p == ' ' || *p == '\t') ++p;
	}
	// Now p points to the first non-bracketed section (file/line info or message)
	std::string cleanMsg(p);
	// Trim leading/trailing whitespace and newlines
	size_t start = cleanMsg.find_first_not_of(" \t\r\n");
	size_t end = cleanMsg.find_last_not_of(" \t\r\n");
	if (start != std::string::npos && end != std::string::npos)
		cleanMsg = cleanMsg.substr(start, end - start + 1);
	else
		cleanMsg.clear();

	// If the cleaned message is empty or only bracketed tokens, log the raw message
	bool onlyBrackets = true;
	for (char c : cleanMsg) {
		if (c != '[' && c != ']' && c != ' ' && c != '\t') {
			onlyBrackets = false;
			break;
		}
	}
	if (cleanMsg.empty() || onlyBrackets) {
		logger::info("[StreamlineSDK:RAW] {}", rawMsg);
		return;
	}

	// Use a clear prefix
	const char* prefix = "[StreamlineSDK]";
	switch (type) {
	case sl::LogType::eInfo:
		logger::info("{} {}", prefix, cleanMsg);
		break;
	case sl::LogType::eWarn:
		logger::warn("{} {}", prefix, cleanMsg);
		break;
	case sl::LogType::eError:
		logger::error("{} {}", prefix, cleanMsg);
		break;
	}
}

std::vector<std::pair<std::string, std::string>> Streamline::dllVersions = {};

void Streamline::LoadInterposer()
{
	triedInitialization = true;

	std::wstring interposerPath = std::wstring(Streamline::PluginDir) + L"\\sl.interposer.dll";
	interposer = LoadLibraryW(interposerPath.c_str());
	if (interposer == nullptr) {
		DWORD errorCode = GetLastError();
		logger::info("[Streamline] Failed to load interposer: Error Code {0:x}", errorCode);
		return;
	} else {
		logger::info("[Streamline] Interposer loaded at address: {0:p}", static_cast<void*>(interposer));
	}

	// Dynamically log all DLL versions in the Streamline plugin directory
	std::filesystem::path pluginDir = std::filesystem::path(Streamline::PluginDir);
	Streamline::dllVersions.clear();
	for (const auto& entry : std::filesystem::directory_iterator(pluginDir)) {
		if (entry.is_regular_file() && entry.path().extension() == L".dll") {
			const auto& path = entry.path();
			auto version = Util::GetDllVersion(path.c_str());
			auto name = path.filename().string();
			std::string versionStr = version ? Util::GetFormattedVersion(*version) : "Unknown";
			Streamline::dllVersions.emplace_back(name, versionStr);
			if (version)
				logger::info("[Streamline] {} version: {}", name, versionStr);
			else
				logger::info("[Streamline] {} version: Unknown", name);
		}
	}

	logger::info("[Streamline] Initializing Streamline");

	sl::Preferences pref;

	sl::Feature featuresToLoad[] = { sl::kFeatureDLSS, sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL };
	sl::Feature featuresToLoadVR[] = { sl::kFeatureDLSS };

	pref.featuresToLoad = REL::Module::IsVR() ? featuresToLoadVR : featuresToLoad;
	pref.numFeaturesToLoad = _countof(featuresToLoad);

	// Set log level from settings
	switch (globals::upscaling->settings.streamlineLogLevel) {
	case 2:
		pref.logLevel = sl::LogLevel::eVerbose;
		break;
	case 1:
		pref.logLevel = sl::LogLevel::eDefault;
		break;
	case 0:
	default:
		pref.logLevel = sl::LogLevel::eOff;
		break;
	}
	pref.logMessageCallback = LoggingCallback;
	pref.showConsole = false;

	pref.engine = sl::EngineType::eCustom;
	pref.engineVersion = "1.0.0";
	pref.projectId = "f8776929-c969-43bd-ac2b-294b4de58aac";

	pref.renderAPI = sl::RenderAPI::eD3D11;
	pref.flags = sl::PreferenceFlags::eDisableCLStateTracking | sl::PreferenceFlags::eUseDXGIFactoryProxy | sl::PreferenceFlags::eUseFrameBasedResourceTagging;

	// Hook up all of the functions exported by the SL Interposer Library
	slInit = (PFun_slInit*)GetProcAddress(interposer, "slInit");
	slShutdown = (PFun_slShutdown*)GetProcAddress(interposer, "slShutdown");
	slIsFeatureSupported = (PFun_slIsFeatureSupported*)GetProcAddress(interposer, "slIsFeatureSupported");
	slIsFeatureLoaded = (PFun_slIsFeatureLoaded*)GetProcAddress(interposer, "slIsFeatureLoaded");
	slSetFeatureLoaded = (PFun_slSetFeatureLoaded*)GetProcAddress(interposer, "slSetFeatureLoaded");
	slEvaluateFeature = (PFun_slEvaluateFeature*)GetProcAddress(interposer, "slEvaluateFeature");
	slAllocateResources = (PFun_slAllocateResources*)GetProcAddress(interposer, "slAllocateResources");
	slFreeResources = (PFun_slFreeResources*)GetProcAddress(interposer, "slFreeResources");
	slSetTag = (PFun_slSetTag*)GetProcAddress(interposer, "slSetTag");
	slSetTagForFrame = (PFun_slSetTagForFrame*)GetProcAddress(interposer, "slSetTagForFrame");
	slGetFeatureRequirements = (PFun_slGetFeatureRequirements*)GetProcAddress(interposer, "slGetFeatureRequirements");
	slGetFeatureVersion = (PFun_slGetFeatureVersion*)GetProcAddress(interposer, "slGetFeatureVersion");
	slUpgradeInterface = (PFun_slUpgradeInterface*)GetProcAddress(interposer, "slUpgradeInterface");
	slSetConstants = (PFun_slSetConstants*)GetProcAddress(interposer, "slSetConstants");
	slGetNativeInterface = (PFun_slGetNativeInterface*)GetProcAddress(interposer, "slGetNativeInterface");
	slGetFeatureFunction = (PFun_slGetFeatureFunction*)GetProcAddress(interposer, "slGetFeatureFunction");
	slGetNewFrameToken = (PFun_slGetNewFrameToken*)GetProcAddress(interposer, "slGetNewFrameToken");
	slSetD3DDevice = (PFun_slSetD3DDevice*)GetProcAddress(interposer, "slSetD3DDevice");

	slCreateDXGIFactory1 = (decltype(&CreateDXGIFactory1))GetProcAddress(interposer, "CreateDXGIFactory1");
	slD3D11CreateDeviceAndSwapChain = (decltype(&D3D11CreateDeviceAndSwapChain))GetProcAddress(interposer, "D3D11CreateDeviceAndSwapChain");

	if (SL_FAILED(res, slInit(pref, sl::kSDKVersion))) {
		logger::critical("[Streamline] Failed to initialize Streamline");
	} else {
		initialized = true;
		logger::info("[Streamline] Successfully initialized Streamline");
	}
}

void Streamline::CheckFeatures(IDXGIAdapter* a_adapter)
{
	logger::info("[Streamline] Checking features");
	DXGI_ADAPTER_DESC adapterDesc;
	a_adapter->GetDesc(&adapterDesc);

	sl::AdapterInfo adapterInfo;
	adapterInfo.deviceLUID = (uint8_t*)&adapterDesc.AdapterLuid;
	adapterInfo.deviceLUIDSizeInBytes = sizeof(LUID);

	auto logFeatureStatus = [&](sl::Feature feature, const char* name, bool& outAvailable) {
		// First, check if the feature is supported on this system
		sl::Result supportResult = slIsFeatureSupported(feature, adapterInfo);
		if (supportResult != sl::Result::eOk) {
			std::string reason = StreamlineResultExplanation(supportResult);
			logger::warn("[Streamline] {}: NOT SUPPORTED - {}", name, reason);
			outAvailable = false;
			return;
		}

		// Check requirements
		sl::FeatureRequirements reqs{};
		sl::Result reqResult = slGetFeatureRequirements(feature, reqs);
		std::string reqStr;
		if (reqResult == sl::Result::eOk) {
			reqStr = "OK";
		} else {
			reqStr = std::string("NOT MET (") + std::string(magic_enum::enum_name(reqResult)) + ")";
		}

		// Check loaded status only if requirements are OK
		bool loaded = false;
		sl::Result loadedResult = sl::Result::eErrorFeatureMissing;  // default to missing
		if (reqResult == sl::Result::eOk) {
			loadedResult = slIsFeatureLoaded(feature, loaded);
			if (loadedResult != sl::Result::eOk) {
				logger::warn("[Streamline] {}: Feature load check failed - {}", name, StreamlineResultExplanation(loadedResult));
				outAvailable = false;
				return;
			}
		}
		outAvailable = loaded;

		// Get version (SL and NGX)
		sl::FeatureVersion version{};
		std::string versionSLStr = "unknown";
		std::string versionNGXStr = "n/a";
		if (slGetFeatureVersion(feature, version) == sl::Result::eOk) {
			versionSLStr = std::format("{}.{}.{}", version.versionSL.major, version.versionSL.minor, version.versionSL.build);
			if (version.versionNGX.major != 0 || version.versionNGX.minor != 0 || version.versionNGX.build != 0) {
				versionNGXStr = std::format("{}.{}.{}", version.versionNGX.major, version.versionNGX.minor, version.versionNGX.build);
			}
		}

		logger::info("[Streamline] {}: {}, requirements {}, version SL {}, NGX {}",
			name,
			loaded ? "loaded" : "not loaded",
			reqStr,
			versionSLStr,
			versionNGXStr);
	};

	logFeatureStatus(sl::kFeatureDLSS, "DLSS", featureDLSS);

	if (REL::Module::IsVR()) {
		featureDLSSG = false;
		featureReflex = false;
		featurePCL = false;
	} else {
		logFeatureStatus(sl::kFeatureDLSS_G, "DLSS-G", featureDLSSG);
		logFeatureStatus(sl::kFeatureReflex, "Reflex", featureReflex);
		logFeatureStatus(sl::kFeaturePCL, "PCL", featurePCL);
	}

	logger::info("[Streamline] Feature summary: DLSS [{}], DLSS-G [{}], Reflex [{}], PCL [{}]",
		featureDLSS ? "Available" : "Unavailable",
		featureDLSSG ? "Available" : "Unavailable",
		featureReflex ? "Available" : "Unavailable",
		featurePCL ? "Available" : "Unavailable");
}

void Streamline::PostDevice()
{
	// Hook up all of the feature functions using the sl function slGetFeatureFunction

	if (featureDLSS) {
		this->slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetOptimalSettings", (void*&)slDLSSGetOptimalSettings);
		this->slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetState", (void*&)slDLSSGetState);
		this->slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSSetOptions", (void*&)slDLSSSetOptions);
	}

	if (featureDLSSG) {
		this->slGetFeatureFunction(sl::kFeatureDLSS_G, "slDLSSGGetState", (void*&)slDLSSGGetState);
		this->slGetFeatureFunction(sl::kFeatureDLSS_G, "slDLSSGSetOptions", (void*&)slDLSSGSetOptions);
	}

	// Reflex Low Latency (NVIDIA only)
	if (featureReflex) {
		this->slGetFeatureFunction(sl::kFeatureReflex, "slReflexGetState", (void*&)slReflexGetState);
		this->slGetFeatureFunction(sl::kFeatureReflex, "slReflexSleep", (void*&)slReflexSleep);
		this->slGetFeatureFunction(sl::kFeatureReflex, "slReflexSetOptions", (void*&)slReflexSetOptions);

		if (featureDLSSG) {
			sl::ReflexOptions reflexOptions{};
			reflexOptions.mode = sl::ReflexMode::eLowLatency;
			reflexOptions.useMarkersToOptimize = false;
			reflexOptions.virtualKey = 0;
			reflexOptions.frameLimitUs = 0;

			if (SL_FAILED(res, slReflexSetOptions(reflexOptions))) {
				logger::error("[Streamline] Failed to set reflex options");
			} else {
				logger::info("[Streamline] Successfully set reflex options");
			}
		}
	}

	// PCL marker API (universal)
	if (featurePCL) {
		this->slGetFeatureFunction(sl::kFeaturePCL, "slPCLSetMarker", (void*&)slPCLSetMarker);
		this->slGetFeatureFunction(sl::kFeaturePCL, "slPCLSetOptions", (void*&)slPCLSetOptions);
		sl::PCLOptions pclOptions{};
		pclOptions.virtualKey = sl::PCLHotKey::eUsePingMessage;
		pclOptions.idThread = 0;
		if (SL_FAILED(res, this->slPCLSetOptions(pclOptions))) {
			logger::error("[Streamline] Failed to set PCL options");
		} else {
			logger::info("[Streamline] Successfully set PCL options");
		}
	}
}

/**
 * @brief Updates and sets camera and frame constants for the current Streamline frame.
 *
 * Populates and submits camera parameters, projection matrices, motion vector settings, and other per-frame constants to the Streamline SDK for the current frame. Uses cached framebuffer data and global state to ensure correct configuration for upscaling and frame generation features.
 */
void Streamline::CheckFrameConstants()
{
	if (frameChecker.IsNewFrame() && globals::streamline->initialized) {
		slGetNewFrameToken(frameToken, &globals::state->frameCount);

		auto state = globals::state;

		sl::Constants slConstants = {};

		if (globals::game::isVR) {
			slConstants.cameraAspectRatio = (state->screenSize.x * 0.5f) / state->screenSize.y;
		} else {
			slConstants.cameraAspectRatio = state->screenSize.x / state->screenSize.y;
		}

		slConstants.cameraFOV = Util::GetVerticalFOVRad();
		slConstants.cameraNear = *globals::game::cameraNear;
		slConstants.cameraFar = *globals::game::cameraFar;

		auto viewMatrix = globals::upscaling->frameBufferCached.CameraViewInverse.Transpose();
		auto cameraViewToClip = globals::upscaling->frameBufferCached.CameraProjUnjittered.Transpose();

		slConstants.cameraMotionIncluded = sl::Boolean::eTrue;
		slConstants.cameraPinholeOffset = { 0.f, 0.f };
		slConstants.cameraRight = { viewMatrix._11, viewMatrix._12, viewMatrix._13 };
		slConstants.cameraUp = { viewMatrix._21, viewMatrix._22, viewMatrix._23 };
		slConstants.cameraFwd = { viewMatrix._31, viewMatrix._32, viewMatrix._33 };
		slConstants.cameraPos = *(sl::float3*)&globals::upscaling->frameBufferCached.CameraPosAdjust;
		slConstants.cameraViewToClip = *(sl::float4x4*)&cameraViewToClip;
		slConstants.depthInverted = sl::Boolean::eFalse;

		recalculateCameraMatrices(slConstants);

		auto upscaling = globals::upscaling;
		auto jitter = upscaling->jitter;
		slConstants.jitterOffset = { -jitter.x, -jitter.y };
		slConstants.reset = sl::Boolean::eFalse;

		slConstants.mvecScale = { (globals::game::isVR ? 0.5f : 1.0f), 1 };
		slConstants.motionVectors3D = sl::Boolean::eTrue;
		slConstants.motionVectorsInvalidValue = FLT_MIN;
		slConstants.orthographicProjection = sl::Boolean::eFalse;
		slConstants.motionVectorsDilated = sl::Boolean::eFalse;
		slConstants.motionVectorsJittered = sl::Boolean::eFalse;

		if (SL_FAILED(res, this->slSetConstants(slConstants, *frameToken, viewport))) {
			logger::error("[Streamline] Could not set constants");
		}
	}
}

void Streamline::Upscale(Texture2D* a_upscaleTexture, Texture2D* a_alphaMask, sl::DLSSPreset a_preset)
{
	CheckFrameConstants();

	auto state = globals::state;

	auto renderer = globals::game::renderer;
	auto& depthTexture = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
	auto& motionVectorsTexture = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];

	static auto previousDlssPreset = a_preset;

	if (previousDlssPreset != a_preset)
		DestroyDLSSResources();
	previousDlssPreset = a_preset;

	{
		sl::DLSSOptions dlssOptions{};
		dlssOptions.mode = sl::DLSSMode::eMaxQuality;
		dlssOptions.outputWidth = (uint)state->screenSize.x;
		dlssOptions.outputHeight = (uint)state->screenSize.y;
		dlssOptions.colorBuffersHDR = sl::Boolean::eFalse;
		dlssOptions.preExposure = 1.0f;
		dlssOptions.sharpness = 0.0f;

		dlssOptions.dlaaPreset = a_preset;
		dlssOptions.qualityPreset = a_preset;
		dlssOptions.balancedPreset = a_preset;
		dlssOptions.performancePreset = a_preset;
		dlssOptions.ultraPerformancePreset = a_preset;

		if (SL_FAILED(result, this->slDLSSSetOptions(viewport, dlssOptions))) {
			logger::critical("[Streamline] Could not enable DLSS");
		}
	}

	{
		sl::Extent fullExtent{ 0, 0, (uint)state->screenSize.x, (uint)state->screenSize.y };

		sl::Resource colorIn = { sl::ResourceType::eTex2d, a_upscaleTexture->resource.get(), 0 };
		sl::Resource colorOut = { sl::ResourceType::eTex2d, a_upscaleTexture->resource.get(), 0 };
		sl::Resource depth = { sl::ResourceType::eTex2d, depthTexture.texture, 0 };
		sl::Resource mvec = { sl::ResourceType::eTex2d, motionVectorsTexture.texture, 0 };

		sl::ResourceTag colorInTag = sl::ResourceTag{ &colorIn, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eOnlyValidNow, &fullExtent };
		sl::ResourceTag colorOutTag = sl::ResourceTag{ &colorOut, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eOnlyValidNow, &fullExtent };
		sl::ResourceTag depthTag = sl::ResourceTag{ &depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eOnlyValidNow, &fullExtent };
		sl::ResourceTag mvecTag = sl::ResourceTag{ &mvec, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eOnlyValidNow, &fullExtent };

		bool needsMask = a_preset != sl::DLSSPreset::ePresetA && a_preset != sl::DLSSPreset::ePresetB;

		sl::Resource alpha = { sl::ResourceType::eTex2d, needsMask ? a_alphaMask->resource.get() : nullptr, 0 };
		sl::ResourceTag alphaTag = sl::ResourceTag{ &alpha, sl::kBufferTypeBiasCurrentColorHint, sl::ResourceLifecycle::eValidUntilPresent, &fullExtent };

		sl::ResourceTag resourceTags[] = { colorInTag, colorOutTag, depthTag, mvecTag, alphaTag };
		this->slSetTagForFrame(*frameToken, viewport, resourceTags, _countof(resourceTags), globals::d3d::context);
	}

	sl::ViewportHandle view(viewport);
	const sl::BaseStructure* inputs[] = { &view };
	slEvaluateFeature(sl::kFeatureDLSS, *frameToken, inputs, _countof(inputs), globals::d3d::context);
}

/**
 * @brief Submits frame resources and markers for DLSS-G frame generation and Reflex latency tracking.
 *
 * Updates DLSS-G frame generation mode if needed, sets Reflex simulation and render markers, and binds required resources (depth, motion vectors, HUD-less color, UI) for the current frame to the Streamline SDK. No action is taken if Streamline is uninitialized, DLSS-G is unavailable, VR mode is active, or D3D12 interop is not enabled.
 */
void Streamline::Present()
{
	if (!initialized || !featureDLSSG || globals::game::isVR || !globals::upscaling->d3d12Interop)
		return;

	CheckFrameConstants();

	auto upscaling = globals::upscaling;

	static auto currentFrameGenerationMode = sl::DLSSGMode::eOff;
	auto frameGenerationMode = upscaling->settings.frameGenerationMode ? sl::DLSSGMode::eOn : sl::DLSSGMode::eOff;

	// Set isFrameGenActive based on whether DLSS-G frame generation is enabled
	isFrameGenActive = (frameGenerationMode == sl::DLSSGMode::eOn);

	if (currentFrameGenerationMode != frameGenerationMode) {
		currentFrameGenerationMode = frameGenerationMode;

		sl::DLSSGOptions options{};
		options.mode = upscaling->settings.frameGenerationMode ? sl::DLSSGMode::eOn : sl::DLSSGMode::eOff;

		if (SL_FAILED(result, this->slDLSSGSetOptions(viewport, options))) {
			logger::error("[Streamline] Could not set DLSSG");
		}
	}

	auto state = globals::state;

	slPCLSetMarker(sl::PCLMarker::eSimulationEnd, *frameToken);
	slPCLSetMarker(sl::PCLMarker::eRenderSubmitStart, *frameToken);

	sl::Extent fullExtent{ 0, 0, (uint)state->screenSize.x, (uint)state->screenSize.y };

	float2 dynamicScreenSize = Util::ConvertToDynamic(state->screenSize);
	sl::Extent dynamicExtent{ 0, 0, (uint)dynamicScreenSize.x, (uint)dynamicScreenSize.y };

	sl::Resource depth = { sl::ResourceType::eTex2d, upscaling->depthBufferShared->resource.get(), 0 };
	sl::ResourceTag depthTag = sl::ResourceTag{ &depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &dynamicExtent };

	sl::Resource mvec = { sl::ResourceType::eTex2d, upscaling->motionVectorBufferShared->resource.get(), 0 };
	sl::ResourceTag mvecTag = sl::ResourceTag{ &mvec, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &dynamicExtent };

	sl::Resource hudLess = { sl::ResourceType::eTex2d, upscaling->HUDLessBufferShared->resource.get(), 0 };
	sl::ResourceTag hudLessTag = sl::ResourceTag{ &hudLess, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent, &fullExtent };

	sl::Resource ui = { sl::ResourceType::eTex2d, nullptr, 0 };
	sl::ResourceTag uiTag = sl::ResourceTag{ &ui, sl::kBufferTypeUIColorAndAlpha, sl::ResourceLifecycle::eValidUntilPresent, &fullExtent };

	sl::ResourceTag inputs[] = { depthTag, mvecTag, hudLessTag, uiTag };
	this->slSetTagForFrame(*frameToken, viewport, inputs, _countof(inputs), globals::d3d::context);
}

/**
 * @brief Releases DLSS resources and disables DLSS for the current viewport.
 *
 * Sets the DLSS mode to off and frees all DLSS-related resources associated with the viewport.
 */
void Streamline::DestroyDLSSResources()
{
	sl::DLSSOptions dlssOptions{};
	dlssOptions.mode = sl::DLSSMode::eOff;
	this->slDLSSSetOptions(viewport, dlssOptions);
	this->slFreeResources(sl::kFeatureDLSS, viewport);
}