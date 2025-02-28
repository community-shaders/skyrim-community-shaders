#include "Streamline.h"

#include <dxgi.h>
#include <dxgi1_3.h>

#include "Hooks.h"
#include "State.h"
#include "Util.h"

#include "DX12SwapChain.h"
#include "Upscaling.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Streamline::Settings,
	frameLimitMode,
	frameGenerationMode);

void LoggingCallback(sl::LogType type, const char* msg)
{
	switch (type) {
	case sl::LogType::eInfo:
		logger::info("{}", msg);
		break;
	case sl::LogType::eWarn:
		logger::warn("{}", msg);
		break;
	case sl::LogType::eError:
		logger::error("{}", msg);
		break;
	}
}

void Streamline::DrawSettings()
{
	if (!globals::game::isVR) {
		ImGui::Text("Frame Generation uses a D3D11 to D3D12 proxy which can create compatibility issues");
		ImGui::Text("Frame Generation can only be enabled or disabled in the mod manager, it can only be temporarily toggled in-game");

		if (ImGui::TreeNodeEx("NVIDIA DLSS Frame Generation", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Text("Requires an NVIDIA GeForce RTX 40/50 Series or newer");
			if (featureDLSSG) {
				const char* frameGenerationModes[] = { "Off", "On" };
				ImGui::SliderInt("Frame Limit (Refresh Rate)", (int*)&settings.frameLimitMode, 0, 1, std::format("{}", frameGenerationModes[(uint)settings.frameLimitMode]).c_str());
				ImGui::SliderInt("Frame Generation", (int*)&settings.frameGenerationMode, 0, 1, std::format("{}", frameGenerationModes[(uint)settings.frameGenerationMode]).c_str());
				settings.frameGenerationMode = (sl::DLSSGMode)std::min(2u, (uint)settings.frameGenerationMode);
			} else {
			}
			ImGui::TreePop();
		}
		ImGui::Checkbox("reflex", &reflex);

		if (ImGui::TreeNodeEx("AMD FSR 3.1 Frame Generation", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Text("Not currently supported");
			ImGui::TreePop();
		}
	}
}

void Streamline::LoadInterposer()
{
	if (globals::state->IsFeatureDisabled("Frame Generation")) {
		return;
	}

	interposer = LoadLibraryW(L"Data/SKSE/Plugins/Streamline/sl.interposer.dll");
	if (interposer == nullptr) {
		DWORD errorCode = GetLastError();
		logger::info("[Streamline] Failed to load interposer: Error Code {0:x}", errorCode);
	} else {
		logger::info("[Streamline] Interposer loaded at address: {0:p}", static_cast<void*>(interposer));
	}
}

void Streamline::Initialize()
{
	logger::info("[Streamline] Initializing Streamline");

	sl::Preferences pref{};

	sl::Feature featuresToLoad[] = { sl::kFeatureDLSS, sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeatureNIS, sl::kFeaturePCL };
	pref.featuresToLoad = featuresToLoad;
	pref.numFeaturesToLoad = _countof(featuresToLoad);

	pref.logLevel = sl::LogLevel::eVerbose;
	pref.logMessageCallback = LoggingCallback;
	pref.showConsole = false;

	pref.engine = sl::EngineType::eCustom;
	pref.engineVersion = "1.0.0";
	pref.projectId = "f8776929-c969-43bd-ac2b-294b4de58aac";
	//pref.flags = sl::PreferenceFlags::eUseDXGIFactoryProxy;

	pref.renderAPI = sl::RenderAPI::eD3D12;

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
	slD3D12CreateDevice = (decltype(&D3D12CreateDevice))GetProcAddress(interposer, "D3D12CreateDevice");

	if (SL_FAILED(res, slInit(pref, sl::kSDKVersion))) {
		logger::critical("[Streamline] Failed to initialize Streamline");
	} else {
		initialized = true;
		logger::info("[Streamline] Successfully initialized Streamline");
	}
}

HRESULT Streamline::CreateDXGIFactory(REFIID riid, void** ppFactory)
{
	Initialize();
	logger::info("[Streamline] Proxying CreateDXGIFactory");
	auto slCreateDXGIFactory1 = reinterpret_cast<decltype(&CreateDXGIFactory1)>(GetProcAddress(interposer, "CreateDXGIFactory1"));
	return slCreateDXGIFactory1(riid, ppFactory);
}

void Streamline::CheckFeatures(DXGI_ADAPTER_DESC adapterDesc)
{
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

	slIsFeatureLoaded(sl::kFeatureDLSS_G, featureDLSSG);
	if (featureDLSSG && !REL::Module::IsVR()) {
		logger::info("[Streamline] DLSSG feature is loaded");
		featureDLSSG = slIsFeatureSupported(sl::kFeatureDLSS_G, adapterInfo) == sl::Result::eOk;
	} else {
		logger::info("[Streamline] DLSSG feature is not loaded");
		sl::FeatureRequirements featureRequirements;
		sl::Result result = slGetFeatureRequirements(sl::kFeatureDLSS_G, featureRequirements);
		if (result != sl::Result::eOk) {
			logger::info("[Streamline] DLSSG feature failed to load due to: {}", magic_enum::enum_name(result));
		}
	}

	slIsFeatureLoaded(sl::kFeatureReflex, featureReflex);
	if (featureReflex) {
		logger::info("[Streamline] Reflex feature is loaded");
		featureReflex = slIsFeatureSupported(sl::kFeatureReflex, adapterInfo) == sl::Result::eOk;
	} else {
		logger::info("[Streamline] Reflex feature is not loaded");
		sl::FeatureRequirements featureRequirements;
		sl::Result result = slGetFeatureRequirements(sl::kFeatureReflex, featureRequirements);
		if (result != sl::Result::eOk) {
			logger::info("[Streamline] Reflex feature failed to load due to: {}", magic_enum::enum_name(result));
		}
	}

	slIsFeatureLoaded(sl::kFeatureNIS, featureNIS);
	if (featureReflex) {
		logger::info("[Streamline] NIS feature is loaded");
		featureReflex = slIsFeatureSupported(sl::kFeatureNIS, adapterInfo) == sl::Result::eOk;
	} else {
		logger::info("[Streamline] NIS feature is not loaded");
		sl::FeatureRequirements featureRequirements;
		sl::Result result = slGetFeatureRequirements(sl::kFeatureNIS, featureRequirements);
		if (result != sl::Result::eOk) {
			logger::info("[Streamline] NIS feature failed to load due to: {}", magic_enum::enum_name(result));
		}
	}

	logger::info("[Streamline] DLSS {} available", featureDLSS ? "is" : "is not");
	logger::info("[Streamline] DLSSG {} available", featureDLSSG && !REL::Module::IsVR() ? "is" : "is not");
	logger::info("[Streamline] Reflex {} available", featureReflex ? "is" : "is not");
	logger::info("[Streamline] NIS {} available", featureNIS ? "is" : "is not");

	if (featureDLSS) {
		slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetOptimalSettings", (void*&)slDLSSGetOptimalSettings);
		slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetState", (void*&)slDLSSGetState);
		slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSSetOptions", (void*&)slDLSSSetOptions);
	}

	if (featureDLSSG && !REL::Module::IsVR()) {
		slGetFeatureFunction(sl::kFeatureDLSS_G, "slDLSSGGetState", (void*&)slDLSSGGetState);
		slGetFeatureFunction(sl::kFeatureDLSS_G, "slDLSSGSetOptions", (void*&)slDLSSGSetOptions);
	}

	if (featureReflex) {
		slGetFeatureFunction(sl::kFeatureReflex, "slReflexGetState", (void*&)slReflexGetState);
		slGetFeatureFunction(sl::kFeatureReflex, "slReflexSleep", (void*&)slReflexSleep);
		slGetFeatureFunction(sl::kFeatureReflex, "slReflexSetOptions", (void*&)slReflexSetOptions);
	}

	if (featureNIS) {
		slGetFeatureFunction(sl::kFeatureNIS, "slNISSetOptions", (void*&)slNISSetOptions);
		slGetFeatureFunction(sl::kFeatureNIS, "slNISGetState", (void*&)slNISGetState);
	}

	slGetFeatureFunction(sl::kFeaturePCL, "slPCLSetMarker", (void*&)slPCLSetMarker2);

	sl::ReflexOptions reflexOptions{};
	reflexOptions.frameLimitUs = 0;
	reflexOptions.mode = sl::ReflexMode::eLowLatency;
	reflexOptions.useMarkersToOptimize = false;
	slReflexSetOptions(reflexOptions);

	featureDLSS = false;
}

void Streamline::Present()
{
	if (!(featureDLSSG && !REL::Module::IsVR()))
		return;

	UpdateConstants();

	static auto currentFrameGenerationMode = sl::DLSSGMode::eOff;

	//if (currentFrameGenerationMode != settings.frameGenerationMode) {
		currentFrameGenerationMode = settings.frameGenerationMode;

		sl::DLSSGOptions options{};
		options.mode = sl::DLSSGMode::eOn;
		options.flags = sl::DLSSGFlags::eRetainResourcesWhenOff;

		if (SL_FAILED(result, slDLSSGSetOptions(viewport, options))) {
			logger::error("[Streamline] Could not set DLSSG");
		}
	//}

	auto state = globals::state;

	sl::Extent fullExtent{ 0, 0, (uint)state->screenSize.x, (uint)state->screenSize.y };

	float2 dynamicScreenSize = Util::ConvertToDynamic(state->screenSize);
	auto context = DX12SwapChain::GetSingleton()->commandList.get();

	{
		// Tag backbuffer resource mainly to pass extent data and therefore resource can be nullptr
		// If the viewport extent is invalid - set extent to null. This informs Streamline that full resource extent needs to be used
		sl::ResourceTag backBufferResourceTag = sl::ResourceTag{ nullptr, sl::kBufferTypeBackbuffer, sl::ResourceLifecycle{}, nullptr };
		sl::ResourceTag inputs[] = { backBufferResourceTag };
		slSetTag(viewport, inputs, _countof(inputs), context);
	}

	sl::Extent dynamicExtent{ 0, 0, (uint)dynamicScreenSize.x, (uint)dynamicScreenSize.y };

	auto upscaling = Upscaling::GetSingleton();

	sl::Resource depth = { sl::ResourceType::eTex2d, upscaling->depthBufferShared12.get(), 0 };
	sl::ResourceTag depthTag = sl::ResourceTag{ &depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &dynamicExtent };

	sl::Resource mvec = { sl::ResourceType::eTex2d, upscaling->motionVectorBufferShared12.get(), 0 };
	sl::ResourceTag mvecTag = sl::ResourceTag{ &mvec, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &dynamicExtent };

	sl::Resource hudLess = { sl::ResourceType::eTex2d, upscaling->colorBufferShared12.get(), 0 };
	sl::ResourceTag hudLessTag = sl::ResourceTag{ &hudLess, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent, &fullExtent };

	sl::Resource ui = { sl::ResourceType::eTex2d, nullptr, 0 };
	sl::ResourceTag uiTag = sl::ResourceTag{ &ui, sl::kBufferTypeUIColorAndAlpha, sl::ResourceLifecycle::eValidUntilPresent, &fullExtent };

	sl::ResourceTag inputs[] = { depthTag, mvecTag, hudLessTag, uiTag };
	slSetTag(viewport, inputs, _countof(inputs), context);
}

void Streamline::Upscale(Texture2D* a_upscaleTexture, Texture2D* a_alphaMask, sl::DLSSPreset a_preset, float a_sharpness)
{
	UpdateConstants();

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

		if (SL_FAILED(result, slDLSSSetOptions(viewport, dlssOptions))) {
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
		sl::ResourceTag depthTag = sl::ResourceTag{ &depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &fullExtent };
		sl::ResourceTag mvecTag = sl::ResourceTag{ &mvec, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &fullExtent };

		bool needsMask = a_preset != sl::DLSSPreset::ePresetA && a_preset != sl::DLSSPreset::ePresetB;

		sl::Resource alpha = { sl::ResourceType::eTex2d, needsMask ? a_alphaMask->resource.get() : nullptr, 0 };
		sl::ResourceTag alphaTag = sl::ResourceTag{ &alpha, sl::kBufferTypeBiasCurrentColorHint, sl::ResourceLifecycle::eValidUntilPresent, &fullExtent };

		sl::ResourceTag resourceTags[] = { colorInTag, colorOutTag, depthTag, mvecTag, alphaTag };
		slSetTag(viewport, resourceTags, _countof(resourceTags), globals::d3d::context);
	}

	sl::ViewportHandle view(viewport);
	const sl::BaseStructure* inputs[] = { &view };
	slEvaluateFeature(sl::kFeatureDLSS, *frameToken, inputs, _countof(inputs), globals::d3d::context);

	{
		sl::Extent fullExtent{ 0, 0, (uint)state->screenSize.x, (uint)state->screenSize.y };

		sl::Resource colorIn = { sl::ResourceType::eTex2d, a_upscaleTexture->resource.get(), 0 };
		sl::Resource colorOut = { sl::ResourceType::eTex2d, a_upscaleTexture->resource.get(), 0 };

		sl::ResourceTag colorInTag = sl::ResourceTag{ &colorIn, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eOnlyValidNow, &fullExtent };
		sl::ResourceTag colorOutTag = sl::ResourceTag{ &colorOut, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eOnlyValidNow, &fullExtent };

		sl::ResourceTag resourceTags[] = { colorInTag, colorOutTag };
		slSetTag(viewport, resourceTags, _countof(resourceTags), globals::d3d::context);
	}

	{
		sl::NISOptions nisOptions{};
		nisOptions.mode = sl::NISMode::eSharpen;
		nisOptions.hdrMode = sl::NISHDR::eNone;
		nisOptions.sharpness = a_sharpness / 3.0f;

		if (SL_FAILED(result, slNISSetOptions(viewport, nisOptions))) {
			logger::critical("[Streamline] Could not set NIS options");
		}
	}

	slEvaluateFeature(sl::kFeatureNIS, *frameToken, inputs, _countof(inputs), globals::d3d::context);
}

void Streamline::Sharpen(Texture2D* a_sharpenTexture, float a_sharpness)
{
	UpdateConstants();

	auto state = globals::state;

	{
		sl::Extent fullExtent{ 0, 0, (uint)state->screenSize.x, (uint)state->screenSize.y };

		sl::Resource colorIn = { sl::ResourceType::eTex2d, a_sharpenTexture->resource.get(), 0 };
		sl::Resource colorOut = { sl::ResourceType::eTex2d, a_sharpenTexture->resource.get(), 0 };

		sl::ResourceTag colorInTag = sl::ResourceTag{ &colorIn, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eOnlyValidNow, &fullExtent };
		sl::ResourceTag colorOutTag = sl::ResourceTag{ &colorOut, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eOnlyValidNow, &fullExtent };

		sl::ResourceTag resourceTags[] = { colorInTag, colorOutTag };
		slSetTag(viewport, resourceTags, _countof(resourceTags), globals::d3d::context);
	}

	{
		sl::NISOptions nisOptions{};
		nisOptions.mode = sl::NISMode::eSharpen;
		nisOptions.hdrMode = sl::NISHDR::eNone;
		nisOptions.sharpness = a_sharpness * 0.5f;

		if (SL_FAILED(result, slNISSetOptions(viewport, nisOptions))) {
			logger::critical("[Streamline] Could not set NIS options");
		}
	}

	sl::ViewportHandle view(viewport);
	const sl::BaseStructure* inputs[] = { &view };

	slEvaluateFeature(sl::kFeatureNIS, *frameToken, inputs, _countof(inputs), globals::d3d::context);
}

void Streamline::UpdateConstants()
{
	static Util::FrameChecker frameChecker;
	if (!frameChecker.IsNewFrame())
		return;

	auto state = globals::state;

	auto cameraData = Util::GetCameraData(0);
	auto eyePosition = Util::GetEyePosition(0);

	auto clipToCameraView = cameraData.viewMat.Invert();
	auto cameraToWorld = cameraData.viewProjMatrixUnjittered.Invert();
	auto cameraToWorldPrev = cameraData.previousViewProjMatrixUnjittered.Invert();

	float4x4 cameraToPrevCamera;

	calcCameraToPrevCamera(*(sl::float4x4*)&cameraToPrevCamera, *(sl::float4x4*)&cameraToWorld, *(sl::float4x4*)&cameraToWorldPrev);

	float4x4 prevCameraToCamera = cameraToPrevCamera;

	prevCameraToCamera.Invert();

	sl::Constants slConstants = {};

	if (globals::game::isVR) {
		slConstants.cameraAspectRatio = (state->screenSize.x * 0.5f) / state->screenSize.y;
	} else {
		slConstants.cameraAspectRatio = state->screenSize.x / state->screenSize.y;
	}
	slConstants.cameraFOV = Util::GetVerticalFOVRad();

	static auto& cameraNear = (*(float*)(REL::RelocationID(517032, 403540).address() + 0x40));
	static auto& cameraFar = (*(float*)(REL::RelocationID(517032, 403540).address() + 0x44));

	slConstants.cameraNear = cameraNear;
	slConstants.cameraFar = cameraFar;

	slConstants.cameraMotionIncluded = sl::Boolean::eTrue;
	slConstants.cameraPinholeOffset = { 0.f, 0.f };
	slConstants.cameraPos = *(sl::float3*)&eyePosition;
	slConstants.cameraFwd = *(sl::float3*)&cameraData.viewForward;
	slConstants.cameraUp = *(sl::float3*)&cameraData.viewUp;
	slConstants.cameraRight = *(sl::float3*)&cameraData.viewRight;
	slConstants.cameraViewToClip = *(sl::float4x4*)&cameraData.viewMat;
	slConstants.clipToCameraView = *(sl::float4x4*)&clipToCameraView;
	slConstants.clipToPrevClip = *(sl::float4x4*)&cameraToPrevCamera;
	slConstants.depthInverted = sl::Boolean::eFalse;

	auto upscaling = globals::upscaling;
	auto jitter = upscaling->jitter;
	slConstants.jitterOffset = { -jitter.x, -jitter.y };
	slConstants.reset = upscaling->reset ? sl::Boolean::eTrue : sl::Boolean::eFalse;

	slConstants.mvecScale = { (globals::game::isVR ? 0.5f : 1.0f), 1 };
	slConstants.prevClipToClip = *(sl::float4x4*)&prevCameraToCamera;
	slConstants.motionVectors3D = sl::Boolean::eTrue;
	slConstants.motionVectorsInvalidValue = FLT_MIN;
	slConstants.orthographicProjection = sl::Boolean::eFalse;
	slConstants.motionVectorsDilated = sl::Boolean::eFalse;
	slConstants.motionVectorsJittered = sl::Boolean::eFalse;

	slGetNewFrameToken(frameToken, nullptr);

	if (SL_FAILED(res, slSetConstants(slConstants, *frameToken, viewport))) {
		logger::error("[Streamline] Could not set constants");
	}
}

void Streamline::SaveSettings(json& o_json)
{
	o_json = settings;
}

void Streamline::LoadSettings(json& o_json)
{
	settings = o_json;
}

void Streamline::RestoreDefaultSettings()
{
	settings = {};
}

void Streamline::DestroyDLSSResources()
{
	sl::DLSSOptions dlssOptions{};
	dlssOptions.mode = sl::DLSSMode::eOff;
	slDLSSSetOptions(viewport, dlssOptions);
	slFreeResources(sl::kFeatureDLSS, viewport);
}

void Streamline::Main_RenderWorld::thunk(bool a1)
{
	//if (!globals::game::isVR || !globals::state->upscalerLoaded) {
	//	// With upscaler, VR hangs on this function, specifically at slSetConstants
	//	globals::streamline->UpdateConstants();
	//}
	func(a1);
}