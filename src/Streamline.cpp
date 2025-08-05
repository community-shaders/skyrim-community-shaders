#include "Streamline.h"

#include <dxgi.h>
#include <dxgi1_3.h>

#include "Hooks.h"
#include "State.h"
#include "Util.h"

#include "DX12SwapChain.h"
#include "Deferred.h"
#include "Upscaling.h"

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

	sl::Feature featuresToLoad[] = { sl::kFeatureDLSS, sl::kFeatureDLSS_G, sl::kFeatureReflex };
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
	pref.flags = sl::PreferenceFlags::eDisableCLStateTracking | sl::PreferenceFlags::eUseDXGIFactoryProxy;

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
	slGetFeatureRequirements = (PFun_slGetFeatureRequirements*)GetProcAddress(interposer, "slGetFeatureRequirements");
	slGetFeatureVersion = (PFun_slGetFeatureVersion*)GetProcAddress(interposer, "slGetFeatureVersion");
	slUpgradeInterface = (PFun_slUpgradeInterface*)GetProcAddress(interposer, "slUpgradeInterface");
	slSetConstants = (PFun_slSetConstants*)GetProcAddress(interposer, "slSetConstants");
	slGetNativeInterface = (PFun_slGetNativeInterface*)GetProcAddress(interposer, "slGetNativeInterface");
	slGetFeatureFunction = (PFun_slGetFeatureFunction*)GetProcAddress(interposer, "slGetFeatureFunction");
	slGetNewFrameToken = (PFun_slGetNewFrameToken*)GetProcAddress(interposer, "slGetNewFrameToken");
	slSetD3DDevice = (PFun_slSetD3DDevice*)GetProcAddress(interposer, "slSetD3DDevice");

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

	slIsFeatureLoaded(sl::kFeatureDLSS, featureDLSS);
	if (featureDLSS) {
		logger::info("[Streamline] DLSS feature is loaded");
		featureDLSS = slIsFeatureSupported(sl::kFeatureDLSS, adapterInfo) == sl::Result::eOk;
	} else {
		logger::info("[Streamline] DLSS feature is not loaded");
		sl::FeatureRequirements featureRequirements;
		sl::Result result = slGetFeatureRequirements(sl::kFeatureDLSS, featureRequirements);
		if (result != sl::Result::eOk) {
			logger::info("[Streamline] DLSS feature failed to load due to: {}", magic_enum::enum_name(result));
		}
	}

	logger::info("[Streamline] DLSS {} available", featureDLSS ? "is" : "is not");
}

void Streamline::PostDevice()
{
	// Hook up all of the feature functions using the sl function slGetFeatureFunction

	if (featureDLSS) {
		slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetOptimalSettings", (void*&)slDLSSGetOptimalSettings);
		slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetState", (void*&)slDLSSGetState);
		slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSSetOptions", (void*&)slDLSSSetOptions);
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
		slConstants.motionVectors3D = sl::Boolean::eFalse;
		slConstants.motionVectorsInvalidValue = FLT_MIN;
		slConstants.orthographicProjection = sl::Boolean::eFalse;
		slConstants.motionVectorsDilated = sl::Boolean::eFalse;
		slConstants.motionVectorsJittered = sl::Boolean::eFalse;

		if (SL_FAILED(res, slSetConstants(slConstants, *frameToken, viewport))) {
			logger::error("[Streamline] Could not set constants");
		}
	}
}

void Streamline::Upscale(ID3D11Resource* a_inputTexture, ID3D11Resource* a_outputTexture, Texture2D* a_alphaMask, sl::DLSSPreset a_preset)
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
		dlssOptions.colorBuffersHDR = sl::Boolean::eTrue;
		dlssOptions.preExposure = 1.0f;
		dlssOptions.sharpness = 0.0f;

		dlssOptions.dlaaPreset = a_preset;
		dlssOptions.qualityPreset = a_preset;
		dlssOptions.balancedPreset = a_preset;
		dlssOptions.performancePreset = a_preset;
		dlssOptions.ultraPerformancePreset = a_preset;

		if (SL_FAILED(result, slDLSSSetOptions(viewport, dlssOptions))) {
			logger::critical("[Streamline] Could not enable DLSS");
		}
	}

	{
		auto screenSize = state->screenSize;
		auto renderSize = Util::ConvertToDynamic(screenSize);

		sl::Extent lowResExtent{ 0, 0, (uint)renderSize.x, (uint)renderSize.y };
		sl::Extent fullExtent{ 0, 0, (uint)screenSize.x, (uint)screenSize.y };

		sl::Resource colorIn = { sl::ResourceType::eTex2d, a_inputTexture, 0 };
		sl::Resource colorOut = { sl::ResourceType::eTex2d, a_outputTexture, 0 };
		sl::Resource depth = { sl::ResourceType::eTex2d, depthTexture.texture, 0 };
		sl::Resource mvec = { sl::ResourceType::eTex2d, motionVectorsTexture.texture, 0 };

		sl::ResourceTag colorInTag = sl::ResourceTag{ &colorIn, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eOnlyValidNow, &lowResExtent };
		sl::ResourceTag colorOutTag = sl::ResourceTag{ &colorOut, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eOnlyValidNow, &fullExtent };
		sl::ResourceTag depthTag = sl::ResourceTag{ &depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eOnlyValidNow, &lowResExtent };
		sl::ResourceTag mvecTag = sl::ResourceTag{ &mvec, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eOnlyValidNow, &lowResExtent };

		bool needsMask = a_preset != sl::DLSSPreset::ePresetA && a_preset != sl::DLSSPreset::ePresetB;

		sl::Resource alpha = { sl::ResourceType::eTex2d, needsMask ? a_alphaMask->resource.get() : nullptr, 0 };
		sl::ResourceTag alphaTag = sl::ResourceTag{ &alpha, sl::kBufferTypeBiasCurrentColorHint, sl::ResourceLifecycle::eValidUntilPresent, &lowResExtent };

		sl::ResourceTag resourceTags[] = { colorInTag, colorOutTag, depthTag, mvecTag, alphaTag };
		slSetTag(viewport, resourceTags, _countof(resourceTags), globals::d3d::context);
	}

	sl::ViewportHandle view(viewport);
	const sl::BaseStructure* inputs[] = { &view };
	slEvaluateFeature(sl::kFeatureDLSS, *frameToken, inputs, _countof(inputs), globals::d3d::context);
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
	slDLSSSetOptions(viewport, dlssOptions);
	slFreeResources(sl::kFeatureDLSS, viewport);
}