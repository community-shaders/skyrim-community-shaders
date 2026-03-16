#include "Streamline.h"

#include <dxgi.h>
#include <dxgi1_3.h>

#include "../../Deferred.h"
#include "../../Hooks.h"
#include "../../State.h"
#include "../../Util.h"
#include "../TAAReorder.h"
#include "../Upscaling.h"
#include "DX12SwapChain.h"

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

	sl::Feature featuresToLoad[] = { sl::kFeatureDLSS };
	sl::Feature featuresToLoadVR[] = { sl::kFeatureDLSS };

	pref.featuresToLoad = REL::Module::IsVR() ? featuresToLoadVR : featuresToLoad;
	pref.numFeaturesToLoad = REL::Module::IsVR() ? _countof(featuresToLoadVR) : _countof(featuresToLoad);

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

	pref.renderAPI = sl::RenderAPI::eD3D11;
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

	slIsFeatureLoaded(sl::kFeatureDLSS, featureDLSS);
	if (featureDLSS) {
		logger::info("[Streamline] DLSS feature is loaded");
		featureDLSS = slIsFeatureSupported(sl::kFeatureDLSS, adapterInfo) == sl::Result::eOk;

		isRTXBelow40series = IsRTXAndBelow40Series(a_adapter);

		if (isRTXBelow40series)
			logger::info("[Streamline] Older RTX GPU detected, DLSS 4.0 will be used instead of DLSS 4.5");
		else
			logger::info("[Streamline] Newer RTX GPU detected, DLSS 4.5 will be used instead of DLSS 4.0");

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

	// VR nasal offset: when the crop is shifted, tell DLSS the optical center is offset
	if (globals::game::isVR) {
		float nasalFrac = globals::features::upscaling.settings.vrDlssCropOffsetX;
		float vpScale = globals::features::upscaling.settings.vrDlssViewportScale;
		if (nasalFrac > 0.0f && vpScale < 1.0f) {
			// Pinhole offset in NDC: how far the crop center is from the eye's optical axis
			// Eye 0: shifted right (+X), Eye 1: shifted left (-X)
			float shiftNDC = nasalFrac / vpScale;  // normalized to crop width
			float sign = (eyeIndex == 0) ? 1.0f : -1.0f;
			slConstants.cameraPinholeOffset = { sign * shiftNDC, 0.f };
		}
	}

	slConstants.cameraRight = { viewMatrix._11, viewMatrix._12, viewMatrix._13 };
	slConstants.cameraUp = { viewMatrix._21, viewMatrix._22, viewMatrix._23 };
	slConstants.cameraFwd = { viewMatrix._31, viewMatrix._32, viewMatrix._33 };
	slConstants.cameraPos = *(sl::float3*)&globals::game::frameBufferCached.GetCameraPosAdjust(eyeIndex);
	slConstants.cameraViewToClip = *(sl::float4x4*)&cameraViewToClip;
	slConstants.depthInverted = sl::Boolean::eFalse;

	if (globals::game::isVR) {
		// When VR viewport scaling is active, DLSS processes a centered sub-region of each eye.
		// The projection matrix must be adjusted to describe only the crop's FOV, not the full eye.
		// Without this, DLSS's temporal reprojection maps pixels to wrong world positions,
		// destroying temporal accumulation (causing aliasing and instability).
		// Scaling rows 0 and 1 of the projection by 1/vpScale narrows the clip-space X/Y
		// to match the crop region. clipToPrevClip must also be conjugated (see below).
		float vpScale = globals::features::upscaling.settings.vrDlssViewportScale;
		if (vpScale < 1.0f) {
			float invScale = 1.0f / vpScale;
			// Row 0 → clip.x, Row 1 → clip.y (Streamline row-major, P * pos convention)
			slConstants.cameraViewToClip[0].x *= invScale;
			slConstants.cameraViewToClip[0].y *= invScale;
			slConstants.cameraViewToClip[0].z *= invScale;
			slConstants.cameraViewToClip[0].w *= invScale;
			slConstants.cameraViewToClip[1].x *= invScale;
			slConstants.cameraViewToClip[1].y *= invScale;
			slConstants.cameraViewToClip[1].z *= invScale;
			slConstants.cameraViewToClip[1].w *= invScale;
			// Narrow the reported FOV to match the crop
			slConstants.cameraFOV = 2.0f * atanf(vpScale * tanf(slConstants.cameraFOV * 0.5f));
		}

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

		// When viewport scaling is active, cameraViewToClip is adjusted (narrower FOV),
		// changing the clip space. clipToPrevClip (computed from unadjusted VP) maps between
		// unadjusted clip spaces. We must conjugate it to map between adjusted clip spaces:
		//   CTP_adj = inv(S) * CTP * S
		// where S = diag(invScale, invScale, 1, 1), inv(S) = diag(vpScale, vpScale, 1, 1).
		//
		// Derivation (row-vector convention: clip = view * P):
		//   clip_adj = clip_unadj * S  (scaling rows 0,1 of P scales clip x,y by invScale)
		//   clip_prev_adj = clip_prev_unadj * S
		//   clip_prev_unadj = clip_curr_unadj * CTP
		//   clip_prev_adj = (clip_curr_adj * inv(S)) * CTP * S = clip_curr_adj * (inv(S) * CTP * S)
		//
		// Element-wise: CTP_adj[i][j] = inv(S)[i] * CTP[i][j] * S[j]
		//   Rows 0,1, cols 0,1: vpScale * invScale = 1 (unchanged)
		//   Rows 0,1, cols 2,3: vpScale * 1 = vpScale
		//   Rows 2,3, cols 0,1: 1 * invScale = invScale
		//   Rows 2,3, cols 2,3: unchanged
		//
		// This ensures clipToPrevClip agrees with per-pixel motion vectors.
		// Without correct conjugation, DLSS sees disagreement between the camera-predicted
		// motion and per-pixel motion vectors, causing it to reject temporal accumulation
		// during camera motion. (When still, CTP ≈ I, and inv(S)*I*S = I → no mismatch.)
		if (vpScale < 1.0f) {
			float invScale = 1.0f / vpScale;
			// Rows 0,1 cols 2,3: multiply by vpScale (from left-multiply by inv(S))
			slConstants.clipToPrevClip[0].z *= vpScale;
			slConstants.clipToPrevClip[0].w *= vpScale;
			slConstants.clipToPrevClip[1].z *= vpScale;
			slConstants.clipToPrevClip[1].w *= vpScale;
			// Rows 2,3 cols 0,1: multiply by invScale (from right-multiply by S)
			slConstants.clipToPrevClip[2].x *= invScale;
			slConstants.clipToPrevClip[2].y *= invScale;
			slConstants.clipToPrevClip[3].x *= invScale;
			slConstants.clipToPrevClip[3].y *= invScale;
		}

		sl::matrixFullInvert(slConstants.prevClipToClip, slConstants.clipToPrevClip);

		// Per-eye diagnostic logging for temporal quality investigation
		{
			static uint32_t ctpDiagCounter = 0;
			bool ctpDiag = (ctpDiagCounter++ % 300 == 0) || (TAAReorder::g_diagCounter == 0 && vpScale < 1.0f);
			if (ctpDiag) {
				auto& ctp = slConstants.clipToPrevClip;
				logger::info("[DLSS-CTP] Eye {} clipToPrevClip diag=({:.6f},{:.6f},{:.6f},{:.6f})",
					eyeIndex, ctp[0].x, ctp[1].y, ctp[2].z, ctp[3].w);
				logger::info("[DLSS-CTP] Eye {} prevVP diag=({:.6f},{:.6f},{:.6f},{:.6f})",
					eyeIndex, prevViewProjSL[0].x, prevViewProjSL[1].y, prevViewProjSL[2].z, prevViewProjSL[3].w);
				logger::info("[DLSS-CTP] Eye {} currVP diag=({:.6f},{:.6f},{:.6f},{:.6f})",
					eyeIndex, currViewProjSL[0].x, currViewProjSL[1].y, currViewProjSL[2].z, currViewProjSL[3].w);
				logger::info("[DLSS-CTP] Eye {} cameraPos=({:.2f},{:.2f},{:.2f}) fov={:.4f} mvecScale=({:.4f},{:.4f})",
					eyeIndex, slConstants.cameraPos.x, slConstants.cameraPos.y, slConstants.cameraPos.z,
					slConstants.cameraFOV, slConstants.mvecScale.x, slConstants.mvecScale.y);
			}
		}
	} else {
		recalculateCameraMatrices(slConstants);
	}

	auto& upscaling = globals::features::upscaling;
	auto jitter = upscaling.jitter;
	slConstants.jitterOffset = { -jitter.x, -jitter.y };
	slConstants.reset = sl::Boolean::eFalse;

	// mvecScale normalizes motion vectors to [-1,1] range. The Streamline DLSS plugin
	// then multiplies by the input render dimensions to get pixel displacement:
	//   MV_Scale = mvecScale * renderWidth
	// The game's motion vectors are in [-1,1] normalized to the FULL per-eye dimensions.
	// Without viewport scaling, renderWidth = eyeWidthIn → MV_Scale = eyeWidthIn → correct.
	// With viewport scaling, renderWidth = cropWidthIn = eyeWidthIn * vpScale, so DLSS
	// underestimates motion by vpScale. Compensate by scaling mvecScale by 1/vpScale.
	if (globals::game::isVR && globals::features::upscaling.settings.vrDlssViewportScale < 1.0f) {
		float invScale = 1.0f / globals::features::upscaling.settings.vrDlssViewportScale;
		slConstants.mvecScale = { invScale, invScale };
	} else {
		slConstants.mvecScale = { 1.0f, 1.0f };
	}
	// Log mvecScale after assignment (was previously logged before assignment, showing uninitialized values)
	if (globals::game::isVR && TAAReorder::g_diagCounter == 0 && globals::features::upscaling.settings.vrDlssViewportScale < 1.0f) {
		logger::info("[TAAReorder] Eye {} mvecScale=({:.4f},{:.4f}) jitter=({:.4f},{:.4f})",
			eyeIndex, slConstants.mvecScale.x, slConstants.mvecScale.y,
			slConstants.jitterOffset.x, slConstants.jitterOffset.y);
	}

	slConstants.motionVectors3D = sl::Boolean::eFalse;
	slConstants.motionVectorsInvalidValue = FLT_MIN;
	slConstants.orthographicProjection = sl::Boolean::eFalse;
	slConstants.motionVectorsDilated = sl::Boolean::eFalse;
	slConstants.motionVectorsJittered = sl::Boolean::eFalse;

	if (SL_FAILED(res, slSetConstants(slConstants, *frameToken, p_viewport))) {
		logger::error("[Streamline] Could not set constants for eye {}", eyeIndex);
	} else {
		static uint32_t constDiagCounter = 0;
		if (constDiagCounter++ % 300 == 0) {
			logger::info("[Streamline] slSetConstants OK eye={} jitter=({:.4f},{:.4f}) fov={:.4f}",
				eyeIndex, slConstants.jitterOffset.x, slConstants.jitterOffset.y,
				slConstants.cameraFOV);
		}
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

void Streamline::SetDLSSOptions(sl::ViewportHandle p_viewport, uint32_t width, uint32_t height)
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

	dlssOptions.outputWidth = width;
	dlssOptions.outputHeight = height;

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

void Streamline::EvaluateDLSS(sl::ViewportHandle vp, uint32_t eyeIndex,
	ID3D11Resource* colorIn, ID3D11Resource* colorOut, ID3D11Resource* depth,
	ID3D11Resource* mvec, ID3D11Resource* reactiveMask, ID3D11Resource* transparencyMask,
	const sl::Extent& extentIn, const sl::Extent& extentOut, uint32_t outputWidth, uint32_t outputHeight)
{
	auto context = globals::d3d::context;

	sl::Resource colorInRes = { sl::ResourceType::eTex2d, colorIn, 0 };
	sl::Resource colorOutRes = { sl::ResourceType::eTex2d, colorOut, 0 };
	sl::Resource depthRes = { sl::ResourceType::eTex2d, depth, 0 };
	sl::Resource mvecRes = { sl::ResourceType::eTex2d, mvec, 0 };
	sl::Resource reactiveMaskRes = { sl::ResourceType::eTex2d, reactiveMask, 0 };
	sl::Resource transparencyMaskRes = { sl::ResourceType::eTex2d, transparencyMask, 0 };

	CheckFrameConstants(vp, eyeIndex);
	SetDLSSOptions(vp, outputWidth, outputHeight);

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

	// Rate-limited diagnostic logging for DLSS evaluation results
	{
		static uint32_t evalDiagCounter[2] = { 0, 0 };
		uint32_t logIdx = globals::game::isVR ? eyeIndex : 0;
		bool diagLog = (evalDiagCounter[logIdx]++ % 300 == 0);

		if (evalResult != sl::Result::eOk) {
			if (diagLog) {
				logger::error("[Streamline] slEvaluateFeature FAILED eye={} result={} (frame {})",
					eyeIndex, (int)evalResult, evalDiagCounter[logIdx]);
			}
		} else if (diagLog) {
			logger::info("[Streamline] slEvaluateFeature OK eye={} (frame {})", eyeIndex, evalDiagCounter[logIdx]);
		}
	}
}

void Streamline::Upscale(ID3D11Resource* a_upscalingTexture, ID3D11Resource* a_reactiveMask, ID3D11Resource* a_transparencyCompositionMask, ID3D11Resource* a_motionVectors, bool eye0Only)
{
	auto state = globals::state;

	auto renderer = globals::game::renderer;
	auto& depthTexture = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

	auto screenSize = state->screenSize;
	auto renderSize = Util::ConvertToDynamic(screenSize);

	// VR: Per-eye isolation is required. Each eye uses a separate per-eye texture
	// with its own viewport handle, avoiding cross-eye history contamination.
	// When viewport scaling is active (vrDlssViewportScale < 1.0):
	//   - All DLSS inputs are physically cropped to the center sub-region at {0,0}.
	//     This eliminates non-zero subrect base offsets which break temporal reprojection.
	//   - Camera matrices are adjusted in CheckFrameConstants to match the crop's FOV.
	//   - FillPeriphery bilinear-upscales the full render-res input to vrFinalOutput,
	//     then FinalizePerEyeOutputs pastes the DLSS crop output into the center.
	// When viewport scaling is off (scale == 1.0), all textures are full-size at {0,0}.
	if (globals::game::isVR) {
		auto& upscaling = globals::features::upscaling;
		uint32_t eyeWidthOut = (uint32_t)(screenSize.x / 2);
		uint32_t eyeHeightOut = (uint32_t)screenSize.y;
		uint32_t eyeWidthIn = (uint32_t)(renderSize.x / 2);
		uint32_t eyeHeightIn = (uint32_t)renderSize.y;

		float vpScale = upscaling.settings.vrDlssViewportScale;
		bool viewportScaling = vpScale < 1.0f;

		uint32_t dlssWidthIn = viewportScaling ? (uint32_t)(eyeWidthIn * vpScale) : eyeWidthIn;
		uint32_t dlssHeightIn = viewportScaling ? (uint32_t)(eyeHeightIn * vpScale) : eyeHeightIn;
		uint32_t dlssWidthOut = viewportScaling ? (uint32_t)(eyeWidthOut * vpScale) : eyeWidthOut;
		uint32_t dlssHeightOut = viewportScaling ? (uint32_t)(eyeHeightOut * vpScale) : eyeHeightOut;

		upscaling.PreparePerEyeInputs(a_upscalingTexture, depthTexture.texture, a_motionVectors, a_reactiveMask, a_transparencyCompositionMask);

		// Periphery TAA diagnostic
		if (TAAReorder::g_diagCounter == 0 && viewportScaling && upscaling.settings.vrPeripheryTAA) {
			logger::info("[TAAReorder] Periphery TAA: vrTAAdPerEye[0]={}, g_initialized={} (TAA injected at display RT level)",
				(void*)upscaling.vrTAAdPerEye[0].get(), TAAReorder::g_initialized);
		}

		uint32_t eyeCount = eye0Only ? 1 : 2;
		for (uint32_t i = 0; i < eyeCount; ++i) {
			sl::ViewportHandle vp = (i == 1) ? viewportRight : viewport;

			if (viewportScaling) {
				// Pre-fill composition target with bilinear upscale of full render-res eye.
				// DLSS output is pasted on top in FinalizePerEyeOutputs.
				upscaling.FillPeriphery(i, eyeWidthIn, eyeHeightIn, eyeWidthOut, eyeHeightOut);
			}

			// All extents are {0,0} - inputs are physically crop-sized (or full-sized when not scaling).
			// No non-zero subrect base offsets, which is critical for DLSS temporal reprojection.
			sl::Extent extentIn = { 0, 0, dlssWidthIn, dlssHeightIn };
			sl::Extent extentOut = { 0, 0, dlssWidthOut, dlssHeightOut };

			// When viewport scaling, use crop-sized vrCropColorIn; otherwise use full vrIntermediateColorIn
			ID3D11Resource* colorInput = viewportScaling ?
				upscaling.vrCropColorIn[i]->resource.get() :
				upscaling.vrIntermediateColorIn[i]->resource.get();

			EvaluateDLSS(vp, i,
				colorInput, upscaling.vrIntermediateColorOut[i]->resource.get(),
				upscaling.vrIntermediateDepth[i]->resource.get(), upscaling.vrIntermediateMotionVectors[i]->resource.get(),
				upscaling.vrIntermediateReactiveMask[i]->resource.get(), upscaling.vrIntermediateTransparencyMask[i]->resource.get(),
				extentIn, extentOut, dlssWidthOut, dlssHeightOut);
		}

		upscaling.FinalizePerEyeOutputs(a_upscalingTexture, eye0Only);
	} else {
		// Non-VR: Simple full-texture upscale
		sl::Extent extentIn{ 0, 0, (uint)renderSize.x, (uint)renderSize.y };
		sl::Extent extentOut{ 0, 0, (uint)screenSize.x, (uint)screenSize.y };

		EvaluateDLSS(viewport, 0,
			a_upscalingTexture, a_upscalingTexture,
			depthTexture.texture, a_motionVectors, a_reactiveMask, a_transparencyCompositionMask,
			extentIn, extentOut, (uint)screenSize.x, (uint)screenSize.y);
	}
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
