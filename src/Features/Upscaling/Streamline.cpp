#include "Streamline.h"

#include <dxgi.h>
#include <dxgi1_3.h>

#include "../../Deferred.h"
#include "../../Hooks.h"
#include "../../State.h"
#include "../../Util.h"
#include "../Upscaling.h"
#include "DX12SwapChain.h"
#include "Features/Raytracing.h"

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

void Streamline::LoadInterposer(bool a_d3d12Mode)
{
	d3d12Mode = a_d3d12Mode;
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

	if (REL::Module::IsVR())
		features = { sl::kFeatureDLSS };
	else {
		features = { sl::kFeatureDLSS };

		if (globals::features::raytracing.loaded && d3d12Mode)
			features.push_back(sl::kFeatureDLSS_RR);
	}

	pref.featuresToLoad = features.data();
	pref.numFeaturesToLoad = static_cast<uint32_t>(features.size());

	// Set log level from settings
	switch (globals::features::upscaling.settings.streamlineLogLevel) {
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

	pref.renderAPI = d3d12Mode ? sl::RenderAPI::eD3D12 : sl::RenderAPI::eD3D11;
	pref.flags = sl::PreferenceFlags::eUseManualHooking;

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

	isRTXBelow40series = IsRTXAndBelow40Series(a_adapter);

	if (isRTXBelow40series)
		logger::info("[Streamline] Older RTX GPU detected, DLSS 4.0 will be used instead of DLSS 4.5");
	else
		logger::info("[Streamline] Newer RTX GPU detected, DLSS 4.5 will be used instead of DLSS 4.0");

	for (auto& feature : features) {
		auto featureEnum = Features::kNone;

		if (feature == sl::kFeatureDLSS)
			featureEnum = Features::kDLSS;

		if (feature == sl::kFeatureDLSS_RR)
			featureEnum = Features::kDLSS_RR;

		auto featureName = magic_enum::enum_name(featureEnum);

		bool featureLoaded = false;
		slIsFeatureLoaded(feature, featureLoaded);
		if (featureLoaded) {
			logger::info("[Streamline] {} feature is loaded", featureName);
			featureLoaded = slIsFeatureSupported(feature, adapterInfo) == sl::Result::eOk;

			if (featureLoaded)
				loadedFeatures |= featureEnum;
		} else {
			logger::info("[Streamline] {} feature is not loaded", featureName);
			sl::FeatureRequirements featureRequirements;
			sl::Result result = slGetFeatureRequirements(feature, featureRequirements);
			if (result != sl::Result::eOk) {
				logger::info("[Streamline] {} feature failed to load due to: {}", featureName, magic_enum::enum_name(result));
			}
		}

		logger::info("[Streamline] {} {} available", featureName, featureLoaded ? "is" : "is not");
	}
}

void Streamline::PostDevice()
{
	// Hook up all of the feature functions using the sl function slGetFeatureFunction

	if (loadedFeatures & Features::kDLSS) {
		slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetOptimalSettings", (void*&)slDLSSGetOptimalSettings);
		slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetState", (void*&)slDLSSGetState);
		slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSSetOptions", (void*&)slDLSSSetOptions);
	}

	if (loadedFeatures & Features::kDLSS_RR)
		SL_FEATURE_FUN_IMPORT(sl::kFeatureDLSS_RR, slDLSSDSetOptions);
}

/**
 * @brief Updates and sets camera and frame constants for the current Streamline frame.
 *
 * Populates and submits camera parameters, projection matrices, motion vector settings, and other per-frame constants to the Streamline SDK for the current frame. Uses cached framebuffer data and global state to ensure correct configuration for upscaling and frame generation features.
 */
void Streamline::CheckFrameConstants(sl::ViewportHandle p_viewport, uint32_t eyeIndex)
{
	if (!globals::features::upscaling.streamline.initialized)
		return;

	// Get new frame token once per frame (only on first call)
	if (frameChecker.IsNewFrame()) {
		slGetNewFrameToken(frameToken, &globals::state->frameCount);
	}

	// In VR, we need to set constants for each viewport/eye separately
	// In non-VR, this is called once per frame
	auto state = globals::state;

	sl::Constants slConstants = {};

	// Calculate aspect ratio for the SINGLE EYE
	float eyeWidth = state->screenSize.x * (globals::game::isVR ? 0.5f : 1.0f);
	slConstants.cameraAspectRatio = eyeWidth / state->screenSize.y;

	slConstants.cameraFOV = Util::GetVerticalFOVRad();
	slConstants.cameraNear = *globals::game::cameraNear;
	slConstants.cameraFar = *globals::game::cameraFar;

	auto viewMatrix = globals::game::frameBufferCached.GetCameraViewInverse(eyeIndex).Transpose();
	auto cameraViewToClip = globals::game::frameBufferCached.GetCameraProjUnjittered(eyeIndex).Transpose();

	slConstants.cameraMotionIncluded = sl::Boolean::eTrue;
	slConstants.cameraPinholeOffset = { 0.f, 0.f };
	slConstants.cameraRight = { viewMatrix._11, viewMatrix._12, viewMatrix._13 };
	slConstants.cameraUp = { viewMatrix._21, viewMatrix._22, viewMatrix._23 };
	slConstants.cameraFwd = { viewMatrix._31, viewMatrix._32, viewMatrix._33 };
	slConstants.cameraPos = *(sl::float3*)&globals::game::frameBufferCached.GetCameraPosAdjust(eyeIndex);
	slConstants.cameraViewToClip = *(sl::float4x4*)&cameraViewToClip;
	slConstants.depthInverted = sl::Boolean::eFalse;

	if (globals::game::isVR) {
		// VR: compute clipToCameraView / clipToPrevClip / prevClipToClip from Skyrim's per-eye matrices.
		// recalculateCameraMatrices() uses a single static prev-frame slot -- unusable for two viewports.
		sl::matrixFullInvert(slConstants.clipToCameraView, slConstants.cameraViewToClip);

		auto currViewProj = globals::game::frameBufferCached.GetCameraViewProjUnjittered(eyeIndex).Transpose();
		auto prevViewProj = globals::game::frameBufferCached.GetCameraPreviousViewProjUnjittered(eyeIndex).Transpose();

		sl::float4x4 currViewProjSL = *(sl::float4x4*)&currViewProj;
		sl::float4x4 prevViewProjSL = *(sl::float4x4*)&prevViewProj;

		sl::float4x4 invCurrViewProj;
		sl::matrixFullInvert(invCurrViewProj, currViewProjSL);
		sl::matrixMul(slConstants.clipToPrevClip, invCurrViewProj, prevViewProjSL);
		sl::matrixFullInvert(slConstants.prevClipToClip, slConstants.clipToPrevClip);
	} else {
		recalculateCameraMatrices(slConstants);
	}

	auto& upscaling = globals::features::upscaling;
	auto jitter = upscaling.jitter;
	slConstants.jitterOffset = { -jitter.x, -jitter.y };
	slConstants.reset = sl::Boolean::eFalse;

	slConstants.mvecScale = { 1.0f, 1.0f };
	slConstants.motionVectors3D = sl::Boolean::eFalse;
	slConstants.motionVectorsInvalidValue = FLT_MIN;
	slConstants.orthographicProjection = sl::Boolean::eFalse;
	slConstants.motionVectorsDilated = sl::Boolean::eFalse;
	slConstants.motionVectorsJittered = sl::Boolean::eFalse;

	if (SL_FAILED(res, slSetConstants(slConstants, *frameToken, p_viewport))) {
		logger::error("[Streamline] Could not set constants for eye {}", eyeIndex);
	}
}

bool Streamline::IsRTXAndBelow40Series(IDXGIAdapter* a_adapter)
{
	DXGI_ADAPTER_DESC adapterDesc = {};

	a_adapter->GetDesc(&adapterDesc);

	UINT vendorId = adapterDesc.VendorId;
	UINT deviceId = adapterDesc.DeviceId;

	// Check if NVIDIA
	if (vendorId != 0x10DE)
		return false;

	// RTX 30 series (Ampere) - 0x2200-0x25FF
	if (deviceId >= 0x2200 && deviceId <= 0x2600)
		return true;

	// RTX 20 series (Turing with RT cores) - 0x1E00-0x1FFF
	if (deviceId >= 0x1E00 && deviceId <= 0x1FFF)
		return true;

	return false;
}

void Streamline::SetDLSSOptions(sl::ViewportHandle p_viewport, uint32_t width)
{
	sl::DLSSOptions dlssOptions{};

	// Map quality mode to DLSS mode
	uint32_t qualityMode = globals::features::upscaling.settings.qualityMode;
	switch (qualityMode) {
	case 1:
		dlssOptions.mode = sl::DLSSMode::eMaxQuality;
		break;
	case 2:
		dlssOptions.mode = sl::DLSSMode::eBalanced;
		break;
	case 3:
		dlssOptions.mode = sl::DLSSMode::eMaxPerformance;
		break;
	case 4:
		dlssOptions.mode = sl::DLSSMode::eUltraPerformance;
		break;
	default:
		dlssOptions.mode = sl::DLSSMode::eDLAA;
		break;
	}

	auto state = globals::state;

	dlssOptions.outputWidth = width;
	dlssOptions.outputHeight = (uint)state->screenSize.y;

	// Detect HDR from kMAIN format at runtime -- VR kMAIN may be 8-bit while SE is FP16
	{
		auto renderer = globals::game::renderer;
		auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
		D3D11_TEXTURE2D_DESC mainDesc;
		static_cast<ID3D11Texture2D*>(main.texture)->GetDesc(&mainDesc);
		bool isHDR = mainDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM;
		dlssOptions.colorBuffersHDR = isHDR ? sl::Boolean::eTrue : sl::Boolean::eFalse;
	}
	dlssOptions.useAutoExposure = sl::Boolean::eTrue;

	std::optional<sl::DLSSPreset> customPreset;
	switch (globals::features::upscaling.settings.presetDLSS) {
	case 1:
		customPreset = sl::DLSSPreset::ePresetJ;
		break;
	case 2:
		customPreset = sl::DLSSPreset::ePresetK;
		break;
	case 3:
		customPreset = sl::DLSSPreset::ePresetL;
		break;
	case 4:
		customPreset = sl::DLSSPreset::ePresetM;
		break;
	}

	if (customPreset.has_value()) {
		dlssOptions.dlaaPreset = customPreset.value();
		dlssOptions.ultraQualityPreset = customPreset.value();
		dlssOptions.qualityPreset = customPreset.value();
		dlssOptions.balancedPreset = customPreset.value();
		dlssOptions.performancePreset = customPreset.value();
		dlssOptions.ultraPerformancePreset = customPreset.value();
	} else if (isRTXBelow40series) {
		dlssOptions.dlaaPreset = sl::DLSSPreset::ePresetJ;
		dlssOptions.ultraQualityPreset = sl::DLSSPreset::ePresetJ;
		dlssOptions.qualityPreset = sl::DLSSPreset::ePresetJ;
		dlssOptions.balancedPreset = sl::DLSSPreset::ePresetJ;
		dlssOptions.performancePreset = sl::DLSSPreset::ePresetJ;
		dlssOptions.ultraPerformancePreset = sl::DLSSPreset::ePresetM;
	} else {
		dlssOptions.dlaaPreset = sl::DLSSPreset::ePresetJ;
		dlssOptions.ultraQualityPreset = sl::DLSSPreset::ePresetJ;
		dlssOptions.qualityPreset = sl::DLSSPreset::ePresetM;
		dlssOptions.balancedPreset = sl::DLSSPreset::ePresetM;
		dlssOptions.performancePreset = sl::DLSSPreset::ePresetM;
		dlssOptions.ultraPerformancePreset = sl::DLSSPreset::ePresetL;
	}

	dlssOptions.preExposure = 1.0f;
	dlssOptions.sharpness = 0.0f;

	if (SL_FAILED(result, slDLSSSetOptions(p_viewport, dlssOptions))) {
		logger::critical("[Streamline] Could not enable DLSS");
	}
}

void Streamline::SetDLSSDOptions(sl::ViewportHandle p_viewport, uint32_t width)
{
	sl::DLSSDOptions dlssOptions{};

	auto worldToCameraView = globals::game::frameBufferCached.GetCameraView().Transpose();
	auto cameraViewToWorld = globals::game::frameBufferCached.GetCameraViewInverse().Transpose();

	dlssOptions.worldToCameraView = sl::float4x4{
		sl::float4{ worldToCameraView._11, worldToCameraView._12, worldToCameraView._13, worldToCameraView._14 },
		sl::float4{ worldToCameraView._21, worldToCameraView._22, worldToCameraView._23, worldToCameraView._24 },
		sl::float4{ worldToCameraView._31, worldToCameraView._32, worldToCameraView._33, worldToCameraView._34 },
		sl::float4{ worldToCameraView._41, worldToCameraView._42, worldToCameraView._43, worldToCameraView._44 }
	};

	dlssOptions.cameraViewToWorld = sl::float4x4{
		sl::float4{ cameraViewToWorld._11, cameraViewToWorld._12, cameraViewToWorld._13, cameraViewToWorld._14 },
		sl::float4{ cameraViewToWorld._21, cameraViewToWorld._22, cameraViewToWorld._23, cameraViewToWorld._24 },
		sl::float4{ cameraViewToWorld._31, cameraViewToWorld._32, cameraViewToWorld._33, cameraViewToWorld._34 },
		sl::float4{ cameraViewToWorld._41, cameraViewToWorld._42, cameraViewToWorld._43, cameraViewToWorld._44 }
	};

	// Map quality mode to DLSS mode
	uint32_t qualityMode = globals::features::upscaling.settings.qualityMode;
	switch (qualityMode) {
	case 1:
		dlssOptions.mode = sl::DLSSMode::eMaxQuality;
		break;
	case 2:
		dlssOptions.mode = sl::DLSSMode::eBalanced;
		break;
	case 3:
		dlssOptions.mode = sl::DLSSMode::eMaxPerformance;
		break;
	case 4:
		dlssOptions.mode = sl::DLSSMode::eUltraPerformance;
		break;
	default:
		dlssOptions.mode = sl::DLSSMode::eDLAA;
		break;
	}

	auto state = globals::state;

	dlssOptions.outputWidth = width;
	dlssOptions.outputHeight = (uint)state->screenSize.y;

	std::optional<sl::DLSSDPreset> customPreset;
	switch (globals::features::upscaling.settings.presetDLSSRR) {
	case 1:
		customPreset = sl::DLSSDPreset::ePresetE;
		break;
	case 2:
		customPreset = sl::DLSSDPreset::ePresetD;
		break;
	}

	if (customPreset.has_value()) {
		dlssOptions.dlaaPreset = customPreset.value();
		dlssOptions.ultraQualityPreset = customPreset.value();
		dlssOptions.qualityPreset = customPreset.value();
		dlssOptions.balancedPreset = customPreset.value();
		dlssOptions.performancePreset = customPreset.value();
		dlssOptions.ultraPerformancePreset = customPreset.value();
	} else {
		dlssOptions.dlaaPreset = sl::DLSSDPreset::eDefault;
		dlssOptions.ultraQualityPreset = sl::DLSSDPreset::eDefault;
		dlssOptions.qualityPreset = sl::DLSSDPreset::eDefault;
		dlssOptions.balancedPreset = sl::DLSSDPreset::eDefault;
		dlssOptions.performancePreset = sl::DLSSDPreset::eDefault;
		dlssOptions.ultraPerformancePreset = sl::DLSSDPreset::eDefault;
	}

	dlssOptions.preExposure = 1.0f;
	dlssOptions.sharpness = 0.0f;

	dlssOptions.colorBuffersHDR = sl::Boolean::eTrue;
	dlssOptions.normalRoughnessMode = sl::DLSSDNormalRoughnessMode::ePacked;
	dlssOptions.alphaUpscalingEnabled = sl::Boolean::eFalse;

	if (SL_FAILED(result, slDLSSDSetOptions(p_viewport, dlssOptions))) {
		logger::critical("[Streamline] Could not enable DLSSD");
	}
}

void Streamline::EvaluateDLSS(sl::ViewportHandle vp, uint32_t eyeIndex,
	ID3D11Resource* colorIn, ID3D11Resource* colorOut, ID3D11Resource* depth,
	ID3D11Resource* mvec, ID3D11Resource* reactiveMask, ID3D11Resource* transparencyMask,
	const sl::Extent& extentIn, const sl::Extent& extentOut, uint32_t outputWidth)
{
	auto context = globals::d3d::context;

	sl::Resource colorInRes = { sl::ResourceType::eTex2d, colorIn, 0 };
	sl::Resource colorOutRes = { sl::ResourceType::eTex2d, colorOut, 0 };
	sl::Resource depthRes = { sl::ResourceType::eTex2d, depth, 0 };
	sl::Resource mvecRes = { sl::ResourceType::eTex2d, mvec, 0 };
	sl::Resource reactiveMaskRes = { sl::ResourceType::eTex2d, reactiveMask, 0 };
	sl::Resource transparencyMaskRes = { sl::ResourceType::eTex2d, transparencyMask, 0 };

	CheckFrameConstants(vp, eyeIndex);
	SetDLSSOptions(vp, outputWidth);

	sl::ResourceTag tags[] = {
		{ &colorInRes, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eOnlyValidNow, &extentIn },
		{ &colorOutRes, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eOnlyValidNow, &extentOut },
		{ &depthRes, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &extentIn },
		{ &mvecRes, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &extentIn },
		{ &reactiveMaskRes, sl::kBufferTypeBiasCurrentColorHint, sl::ResourceLifecycle::eValidUntilPresent, &extentIn },
		{ &transparencyMaskRes, sl::kBufferTypeTransparencyHint, sl::ResourceLifecycle::eValidUntilPresent, &extentIn }
	};

	slSetTag(vp, tags, _countof(tags), context);

	sl::ViewportHandle view(vp);
	const sl::BaseStructure* inputs[] = { &view };

	auto state = globals::state;
	if (state->frameAnnotations) {
		if (globals::game::isVR) {
			char buf[32];
			snprintf(buf, sizeof(buf), "DLSS Evaluate Eye %u", eyeIndex);
			state->BeginPerfEvent(buf);
		} else {
			state->BeginPerfEvent("DLSS Evaluate");
		}
	}

	sl::Result evalResult = slEvaluateFeature(sl::kFeatureDLSS, *frameToken, inputs, _countof(inputs), context);

	if (state->frameAnnotations)
		state->EndPerfEvent();

	if (evalResult != sl::Result::eOk) {
		static bool evalErrorLogged[2] = { false, false };
		uint32_t logIdx = globals::game::isVR ? eyeIndex : 0;
		if (!evalErrorLogged[logIdx]) {
			evalErrorLogged[logIdx] = true;
			logger::error("[Streamline] slEvaluateFeature failed{} result={}", globals::game::isVR ? std::format(" for eye {}", eyeIndex) : "", (int)evalResult);
		}
	}
}

void Streamline::EvaluateDLSS(ID3D12GraphicsCommandList4* commandList, sl::ViewportHandle vp,
	ID3D12Resource* colorIn, ID3D12Resource* colorOut, ID3D12Resource* depth, ID3D12Resource* mvec, ID3D12Resource* reactiveMask,
	const sl::Extent& extentIn, const sl::Extent& extentOut, uint32_t outputWidth)
{
	sl::Resource colorInRes = { sl::ResourceType::eTex2d, colorIn, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE };
	sl::Resource colorOutRes = { sl::ResourceType::eTex2d, colorOut, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE };
	sl::Resource depthRes = { sl::ResourceType::eTex2d, depth, 0 };
	sl::Resource mvecRes = { sl::ResourceType::eTex2d, mvec, 0 };
	sl::Resource reactiveMaskRes = { sl::ResourceType::eTex2d, reactiveMask, 0 };
	//sl::Resource transparencyMaskRes = { sl::ResourceType::eTex2d, transparencyMask, 0 };

	CheckFrameConstants(vp, 0);
	SetDLSSOptions(vp, outputWidth);

	sl::ResourceTag tags[] = {
		{ &colorInRes, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eOnlyValidNow, &extentIn },
		{ &colorOutRes, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eOnlyValidNow, &extentOut },
		{ &depthRes, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &extentIn },
		{ &mvecRes, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &extentIn },
		{ &reactiveMaskRes, sl::kBufferTypeBiasCurrentColorHint, sl::ResourceLifecycle::eValidUntilPresent, &extentIn }, // kBufferTypeReactiveMaskHint
		//{ &transparencyMaskRes, sl::kBufferTypeTransparencyHint, sl::ResourceLifecycle::eValidUntilPresent, &extentIn },
	};

	if (SL_FAILED(result, slSetTag(vp, tags, _countof(tags), commandList))) {
		logger::error("[Streamline] Failed to set DLSS tags, error: {}", magic_enum::enum_name(result));
		return;
	}

	sl::ViewportHandle view(vp);
	const sl::BaseStructure* inputs[] = { &view };

	auto state = globals::state;
	if (state->frameAnnotations) {
		state->BeginPerfEvent("DLSS Evaluate");
	}

	sl::Result evalResult = slEvaluateFeature(sl::kFeatureDLSS, *frameToken, inputs, _countof(inputs), commandList);

	if (state->frameAnnotations)
		state->EndPerfEvent();

	if (evalResult != sl::Result::eOk) {
		static bool evalErrorLogged = false;

		if (!evalErrorLogged) {
			evalErrorLogged = true;
			logger::error("[Streamline] slEvaluateFeature failed, result: {}", magic_enum::enum_name(evalResult));
		}
	}
}

void Streamline::EvaluateDLSSD(ID3D12GraphicsCommandList4* commandList, sl::ViewportHandle vp,
	ID3D12Resource* colorIn, ID3D12Resource* colorOut, ID3D12Resource* depth, ID3D12Resource* mvec, ID3D12Resource* reactiveMask,
	ID3D12Resource* diffuseAlbedo, ID3D12Resource* specularAlbedo, ID3D12Resource* normalRoughness, ID3D12Resource* specHitDistance,
	const sl::Extent& extentIn, const sl::Extent& extentOut, uint32_t outputWidth)
{
	sl::Resource colorInRes = { sl::ResourceType::eTex2d, colorIn, D3D12_RESOURCE_STATE_COMMON };
	sl::Resource colorOutRes = { sl::ResourceType::eTex2d, colorOut, D3D12_RESOURCE_STATE_COMMON };
	sl::Resource depthRes = { sl::ResourceType::eTex2d, depth, 0 };
	sl::Resource mvecRes = { sl::ResourceType::eTex2d, mvec, 0 };
	sl::Resource reactiveMaskRes = { sl::ResourceType::eTex2d, reactiveMask, 0 };
	//sl::Resource transparencyMaskRes = { sl::ResourceType::eTex2d, transparencyMask, 0 };

	sl::Resource diffuseAlbedoRes = { sl::ResourceType::eTex2d, diffuseAlbedo, 0 };
	sl::Resource specularAlbedoRes = { sl::ResourceType::eTex2d, specularAlbedo, 0 };
	sl::Resource normalRoughnessRes = { sl::ResourceType::eTex2d, normalRoughness, 0 };
	sl::Resource specHitDistanceRes = { sl::ResourceType::eTex2d, specHitDistance, 0 };

	CheckFrameConstants(vp, 0);
	SetDLSSDOptions(vp, outputWidth);

	sl::ResourceTag tags[] = {
		{ &colorInRes, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eOnlyValidNow, &extentIn },
		{ &colorOutRes, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eOnlyValidNow, &extentOut },
		{ &depthRes, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &extentIn },
		{ &mvecRes, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &extentIn },
		{ &reactiveMaskRes, sl::kBufferTypeBiasCurrentColorHint, sl::ResourceLifecycle::eValidUntilPresent, &extentIn },
		//{ &transparencyMaskRes, sl::kBufferTypeTransparencyHint, sl::ResourceLifecycle::eValidUntilPresent, &extentIn },

		{ &diffuseAlbedoRes, sl::kBufferTypeAlbedo, sl::ResourceLifecycle::eValidUntilPresent, &extentIn },
		{ &specularAlbedoRes, sl::kBufferTypeSpecularAlbedo, sl::ResourceLifecycle::eValidUntilPresent, &extentIn },
		{ &normalRoughnessRes, sl::kBufferTypeNormalRoughness, sl::ResourceLifecycle::eValidUntilPresent, &extentIn },
		{ &specHitDistanceRes, sl::kBufferTypeSpecularHitDistance, sl::ResourceLifecycle::eValidUntilPresent, &extentIn }
	};

	if (SL_FAILED(result, slSetTag(vp, tags, _countof(tags), commandList))) {
		logger::error("[Streamline] Failed to set DLSSD tags, error: {}", magic_enum::enum_name(result));
		return;
	}

	sl::ViewportHandle view(vp);
	const sl::BaseStructure* inputs[] = { &view };

	auto state = globals::state;
	if (state->frameAnnotations) {
		state->BeginPerfEvent("DLSSD Evaluate");
	}

	sl::Result evalResult = slEvaluateFeature(sl::kFeatureDLSS_RR, *frameToken, inputs, _countof(inputs), commandList);

	if (state->frameAnnotations)
		state->EndPerfEvent();

	if (evalResult != sl::Result::eOk) {
		static bool evalErrorLogged = false;

		if (!evalErrorLogged) {
			evalErrorLogged = true;
			logger::error("[Streamline] slEvaluateFeature failed result={}", (int)evalResult);
		}
	}
}

void Streamline::Upscale(ID3D11Resource* a_upscalingTexture, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors)
{
	if (d3d12Mode) {
		logger::critical("[Upscaling] D3D11 Upscale method called in D3D12 mode.");
		return;
	}

	auto state = globals::state;

	auto renderer = globals::game::renderer;
	auto& depthTexture = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

	auto screenSize = state->screenSize;
	auto renderSize = Util::ConvertToDynamic(screenSize);

	// VR: Combined-buffer mode with extent offsets causes temporal ghosting on the right eye
	// because DLSS's internal history buffers use extent offsets as indices.
	// Per-eye isolation with extents at {0,0} is required.
	if (globals::game::isVR) {
		auto& upscaling = globals::features::upscaling;
		uint32_t eyeWidthOut = (uint32_t)(screenSize.x / 2);
		uint32_t eyeHeightOut = (uint32_t)screenSize.y;
		uint32_t eyeWidthIn = (uint32_t)(renderSize.x / 2);
		uint32_t eyeHeightIn = (uint32_t)renderSize.y;

		upscaling.PreparePerEyeInputs(a_upscalingTexture, depthTexture.texture, a_motionVectors, a_reactiveMask, a_transparencyCompositionMask);

		for (uint32_t i = 0; i < 2; ++i) {
			sl::ViewportHandle vp = (i == 1) ? viewportRight : viewport;
			sl::Extent extentIn{ 0, 0, eyeWidthIn, eyeHeightIn };
			sl::Extent extentOut{ 0, 0, eyeWidthOut, eyeHeightOut };

			EvaluateDLSS(vp, i,
				upscaling.vrIntermediateColorIn[i]->resource.get(), upscaling.vrIntermediateColorOut[i]->resource.get(),
				upscaling.vrIntermediateDepth[i]->resource.get(), upscaling.vrIntermediateMotionVectors[i]->resource.get(),
				upscaling.vrIntermediateReactiveMask[i]->resource.get(), upscaling.vrIntermediateTransparencyMask[i]->resource.get(),
				extentIn, extentOut, eyeWidthOut);
		}

		upscaling.FinalizePerEyeOutputs(a_upscalingTexture);
	} else {
		// Non-VR: Simple full-texture upscale
		sl::Extent extentIn{ 0, 0, (uint)renderSize.x, (uint)renderSize.y };
		sl::Extent extentOut{ 0, 0, (uint)screenSize.x, (uint)screenSize.y };

		EvaluateDLSS(viewport, 0,
			a_upscalingTexture, a_upscalingTexture,
			depthTexture.texture, a_motionVectors, a_reactiveMask, a_transparencyCompositionMask,
			extentIn, extentOut, (uint)screenSize.x);
	}
}

void Streamline::Upscale(ID3D12GraphicsCommandList4* a_commandList, ID3D12Resource* a_input, ID3D12Resource* a_output, ID3D12Resource* a_depth, ID3D12Resource* a_motionVectors, ID3D12Resource* a_reactiveMask)
{
	auto screenSize = globals::state->screenSize;
	auto renderSize = Util::ConvertToDynamic(screenSize);

	sl::Extent extentIn{ 0, 0, (uint)renderSize.x, (uint)renderSize.y };
	sl::Extent extentOut{ 0, 0, (uint)screenSize.x, (uint)screenSize.y };

	EvaluateDLSS(a_commandList, viewport,
		a_input, a_output, a_depth, a_motionVectors, a_reactiveMask,
		extentIn, extentOut, (uint)screenSize.x);
}

void Streamline::DenoiseUpscale(ID3D12GraphicsCommandList4* a_commandList, ID3D12Resource* a_upscalingTexture, ID3D12Resource* a_depth, ID3D12Resource* a_motionVectors, ID3D12Resource* a_reactiveMask)
{
	ID3D12Resource* diffuseAlbedo = nullptr;
	ID3D12Resource* specularAlbedo = nullptr;
	ID3D12Resource* normalRoughness = nullptr;
	ID3D12Resource* specHitDistance = nullptr;

	globals::features::raytracing.creationEngineRaytracing->GetRRInput(diffuseAlbedo, specularAlbedo, normalRoughness, specHitDistance);

	auto screenSize = globals::state->screenSize;
	auto renderSize = Util::ConvertToDynamic(screenSize);

	sl::Extent extentIn{ 0, 0, (uint)renderSize.x, (uint)renderSize.y };
	sl::Extent extentOut{ 0, 0, (uint)screenSize.x, (uint)screenSize.y };

	EvaluateDLSSD(a_commandList, viewport,
		a_upscalingTexture, a_upscalingTexture,
		a_depth, a_motionVectors, a_reactiveMask,
		diffuseAlbedo, specularAlbedo, normalRoughness, specHitDistance,
		extentIn, extentOut, (uint)screenSize.x);
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

	if (globals::game::isVR) {
		slDLSSSetOptions(viewportRight, dlssOptions);
		slFreeResources(sl::kFeatureDLSS, viewportRight);
	}
}
