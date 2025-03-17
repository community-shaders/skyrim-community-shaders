#include "Streamline.h"

#include <dxgi.h>
#include <dxgi1_3.h>

#include "Hooks.h"
#include "State.h"
#include "Util.h"

#include "DX12SwapChain.h"
#include "Deferred.h"
#include "Upscaling.h"
#include "HDR.h"

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

void Streamline::LoadInterposer()
{
	triedInitialization = true;

	interposer = LoadLibraryW(L"Data/SKSE/Plugins/Streamline/sl.interposer.dll");
	if (interposer == nullptr) {
		DWORD errorCode = GetLastError();
		logger::info("[Streamline] Failed to load interposer: Error Code {0:x}", errorCode);
		return;
	}
	logger::info("[Streamline] Interposer loaded at address: {0:p}", static_cast<void*>(interposer));

	logger::info("[Streamline] Initializing Streamline");

	sl::Preferences pref;

	sl::Feature featuresToLoad[] = { sl::kFeatureDLSS, sl::kFeatureDLSS_G, sl::kFeatureReflex };
	sl::Feature featuresToLoadVR[] = { sl::kFeatureDLSS };

	pref.featuresToLoad = REL::Module::IsVR() ? featuresToLoadVR : featuresToLoad;
	pref.numFeaturesToLoad = _countof(featuresToLoad);

	pref.logLevel = sl::LogLevel::eOff;
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
	if (REL::Module::IsVR()) {
		featureDLSSG = false;
	} else if (featureDLSSG) {
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
	if (REL::Module::IsVR()) {
		featureReflex = false;
	} else if (featureReflex) {
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

	logger::info("[Streamline] DLSS {} available", featureDLSS ? "is" : "is not");
	logger::info("[Streamline] DLSSG {} available", featureDLSSG && !REL::Module::IsVR() ? "is" : "is not");
	logger::info("[Streamline] Reflex {} available", featureReflex && !REL::Module::IsVR() ? "is" : "is not");
}

void Streamline::PostDevice()
{
	// Hook up all of the feature functions using the sl function slGetFeatureFunction

	if (featureDLSS) {
		slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetOptimalSettings", (void*&)slDLSSGetOptimalSettings);
		slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetState", (void*&)slDLSSGetState);
		slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSSetOptions", (void*&)slDLSSSetOptions);
	}

	if (featureDLSSG) {
		slGetFeatureFunction(sl::kFeatureDLSS_G, "slDLSSGGetState", (void*&)slDLSSGGetState);
		slGetFeatureFunction(sl::kFeatureDLSS_G, "slDLSSGSetOptions", (void*&)slDLSSGSetOptions);
	}

	if (featureReflex) {
		slGetFeatureFunction(sl::kFeatureReflex, "slReflexGetState", (void*&)slReflexGetState);
		slGetFeatureFunction(sl::kFeatureReflex, "slReflexSetMarker", (void*&)slReflexSetMarker);
		slGetFeatureFunction(sl::kFeatureReflex, "slReflexSleep", (void*&)slReflexSleep);
		slGetFeatureFunction(sl::kFeatureReflex, "slReflexSetOptions", (void*&)slReflexSetOptions);

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
}

void Streamline::CheckFrameConstants()
{
	if (frameChecker.IsNewFrame()) {
		slGetNewFrameToken(frameToken, nullptr);

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

		auto viewMatrix = frameBufferCached.CameraViewInverse.Transpose();
		auto cameraViewToClip = frameBufferCached.CameraProjUnjittered.Transpose();

		slConstants.cameraMotionIncluded = sl::Boolean::eTrue;
		slConstants.cameraPinholeOffset = { 0.f, 0.f };
		slConstants.cameraRight = { viewMatrix._11, viewMatrix._12, viewMatrix._13 };
		slConstants.cameraUp = { viewMatrix._21, viewMatrix._22, viewMatrix._23 };
		slConstants.cameraFwd = { viewMatrix._31, viewMatrix._32, viewMatrix._33 };
		slConstants.cameraPos = *(sl::float3*)&frameBufferCached.CameraPosAdjust;
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

		if (SL_FAILED(res, slSetConstants(slConstants, *frameToken, viewport))) {
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
		dlssOptions.outputWidth = static_cast<uint>(state->screenSize.x);
		dlssOptions.outputHeight = static_cast<uint>(state->screenSize.y);
		if (globals::hdr->settings.enabled) {
			dlssOptions.colorBuffersHDR = sl::Boolean::eTrue;
		} else {
			dlssOptions.colorBuffersHDR = sl::Boolean::eFalse;
		}
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
		sl::Extent fullExtent{ 0, 0, static_cast<uint>(state->screenSize.x), static_cast<uint>(state->screenSize.y) };

		sl::Resource colorIn = { sl::ResourceType::eTex2d, a_upscaleTexture->resource.get(), 0 };
		sl::Resource colorOut = { sl::ResourceType::eTex2d, a_upscaleTexture->resource.get(), 0 };
		sl::Resource depth = { sl::ResourceType::eTex2d, depthTexture.texture, 0 };
		sl::Resource mvec = { sl::ResourceType::eTex2d, motionVectorsTexture.texture, 0 };

		auto colorInTag = sl::ResourceTag{ &colorIn, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eOnlyValidNow, &fullExtent };
		auto colorOutTag = sl::ResourceTag{ &colorOut, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eOnlyValidNow, &fullExtent };
		auto depthTag = sl::ResourceTag{ &depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eOnlyValidNow, &fullExtent };
		auto mvecTag = sl::ResourceTag{ &mvec, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eOnlyValidNow, &fullExtent };

		bool needsMask = a_preset != sl::DLSSPreset::ePresetA && a_preset != sl::DLSSPreset::ePresetB;

		sl::Resource alpha = { sl::ResourceType::eTex2d, needsMask ? a_alphaMask->resource.get() : nullptr, 0 };
		auto alphaTag = sl::ResourceTag{ &alpha, sl::kBufferTypeBiasCurrentColorHint, sl::ResourceLifecycle::eValidUntilPresent, &fullExtent };

		sl::ResourceTag resourceTags[] = { colorInTag, colorOutTag, depthTag, mvecTag, alphaTag };
		slSetTag(viewport, resourceTags, _countof(resourceTags), globals::d3d::context);
	}

	sl::ViewportHandle view(viewport);
	const sl::BaseStructure* inputs[] = { &view };
	slEvaluateFeature(sl::kFeatureDLSS, *frameToken, inputs, _countof(inputs), globals::d3d::context);
}

void Streamline::Present()
{
	if (!initialized || !featureDLSSG || globals::game::isVR || !globals::upscaling->d3d12Interop)
		return;

	CheckFrameConstants();

	auto upscaling = globals::upscaling;

	static auto currentFrameGenerationMode = sl::DLSSGMode::eOff;
	auto frameGenerationMode = upscaling->settings.frameGenerationMode ? sl::DLSSGMode::eOn : sl::DLSSGMode::eOff;

	if (currentFrameGenerationMode != frameGenerationMode) {
		currentFrameGenerationMode = frameGenerationMode;

		sl::DLSSGOptions options{};
		options.mode = upscaling->settings.frameGenerationMode ? sl::DLSSGMode::eOn : sl::DLSSGMode::eOff;

		if (SL_FAILED(result, slDLSSGSetOptions(viewport, options))) {
			logger::error("[Streamline] Could not set DLSSG");
		}
	}

	auto state = globals::state;

	// Fake NVIDIA Reflex to prevent DLSSG errors
	slReflexSetMarker(sl::ReflexMarker::eInputSample, *frameToken);
	slReflexSetMarker(sl::ReflexMarker::eSimulationStart, *frameToken);
	slReflexSetMarker(sl::ReflexMarker::eSimulationEnd, *frameToken);
	slReflexSetMarker(sl::ReflexMarker::eRenderSubmitStart, *frameToken);
	slReflexSetMarker(sl::ReflexMarker::eRenderSubmitEnd, *frameToken);
	slReflexSetMarker(sl::ReflexMarker::ePresentStart, *frameToken);
	slReflexSetMarker(sl::ReflexMarker::ePresentEnd, *frameToken);

	sl::Extent fullExtent{ 0, 0, static_cast<uint>(state->screenSize.x), static_cast<uint>(state->screenSize.y) };

	float2 dynamicScreenSize = Util::ConvertToDynamic(state->screenSize);
	sl::Extent dynamicExtent{ 0, 0, static_cast<uint>(dynamicScreenSize.x), static_cast<uint>(dynamicScreenSize.y) };

	sl::Resource depth = { sl::ResourceType::eTex2d, upscaling->depthBufferShared->resource.get(), 0 };
	auto depthTag = sl::ResourceTag{ &depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &dynamicExtent };

	sl::Resource mvec = { sl::ResourceType::eTex2d, upscaling->motionVectorBufferShared->resource.get(), 0 };
	auto mvecTag = sl::ResourceTag{ &mvec, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &dynamicExtent };

	sl::Resource hudLess = { sl::ResourceType::eTex2d, upscaling->HUDLessBufferShared->resource.get(), 0 };
	auto hudLessTag = sl::ResourceTag{ &hudLess, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent, &fullExtent };

	sl::Resource ui = { sl::ResourceType::eTex2d, nullptr, 0 };
	auto uiTag = sl::ResourceTag{ &ui, sl::kBufferTypeUIColorAndAlpha, sl::ResourceLifecycle::eValidUntilPresent, &fullExtent };

	sl::ResourceTag inputs[] = { depthTag, mvecTag, hudLessTag, uiTag };
	slSetTag(viewport, inputs, _countof(inputs), globals::d3d::context);
}

void Streamline::DestroyDLSSResources()
{
	sl::DLSSOptions dlssOptions{};
	dlssOptions.mode = sl::DLSSMode::eOff;
	slDLSSSetOptions(viewport, dlssOptions);
	slFreeResources(sl::kFeatureDLSS, viewport);
}

void Streamline::InstallHooks(ID3D11DeviceContext* a_context)
{
	stl::detour_vfunc<14, ID3D11DeviceContext_Map>(a_context);
	stl::detour_vfunc<15, ID3D11DeviceContext_Unmap>(a_context);
}

HRESULT Streamline::ID3D11DeviceContext_Map::thunk(ID3D11DeviceContext* This, ID3D11Resource* pResource, UINT Subresource, D3D11_MAP MapType, UINT MapFlags, D3D11_MAPPED_SUBRESOURCE* pMappedResource)
{
	HRESULT hr = func(This, pResource, Subresource, MapType, MapFlags, pMappedResource);
	if (hr == S_OK) {
		if (*globals::game::perFrame.get() == pResource)
			globals::streamline->mappedFrameBuffer = pMappedResource;
	}
	return hr;
}

void Streamline::ID3D11DeviceContext_Unmap::thunk(ID3D11DeviceContext* This, ID3D11Resource* pResource, UINT Subresource)
{
	if (*globals::game::perFrame.get() == pResource && globals::streamline->mappedFrameBuffer)
		globals::streamline->CacheFramebuffer();
	func(This, pResource, Subresource);
}

void Streamline::CacheFramebuffer()
{
	auto frameBuffer = static_cast<FrameBuffer*>(mappedFrameBuffer->pData);
	frameBufferCached = *frameBuffer;
	mappedFrameBuffer = nullptr;
}
