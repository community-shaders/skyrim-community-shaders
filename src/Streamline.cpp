#include "Streamline.h"

#include <dxgi.h>
#include <dxgi1_3.h>

#include "Hooks.h"
#include "State.h"
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

	sl::Preferences pref;

	sl::Feature featuresToLoad[] = { sl::kFeatureDLSS, sl::kFeatureDLSS_G, sl::kFeatureReflex };
	pref.featuresToLoad = featuresToLoad;
	pref.numFeaturesToLoad = _countof(featuresToLoad);

	pref.logLevel = sl::LogLevel::eOff;
	pref.logMessageCallback = LoggingCallback;
	pref.showConsole = false;

	pref.engine = sl::EngineType::eCustom;
	pref.engineVersion = "1.0.0";
	pref.projectId = "f8776929-c969-43bd-ac2b-294b4de58aac";
	pref.flags |= sl::PreferenceFlags::eUseManualHooking;

	pref.renderAPI = sl::RenderAPI::eD3D11;

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

void Streamline::PostDevice(DXGI_SWAP_CHAIN_DESC* a_swapChainDesc)
{
	// Hook up all of the feature functions using the sl function slGetFeatureFunction

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
		slGetFeatureFunction(sl::kFeatureReflex, "slReflexSetMarker", (void*&)slReflexSetMarker);
		slGetFeatureFunction(sl::kFeatureReflex, "slReflexSleep", (void*&)slReflexSleep);
		slGetFeatureFunction(sl::kFeatureReflex, "slReflexSetOptions", (void*&)slReflexSetOptions);
	}

	MONITORINFOEX monitorInfo = {};
	monitorInfo.cbSize = sizeof(MONITORINFOEX);

	HMONITOR monitor = MonitorFromWindow(a_swapChainDesc->OutputWindow, MONITOR_DEFAULTTONEAREST);
	GetMonitorInfo(monitor, &monitorInfo);

	DEVMODE devMode = {};
	devMode.dmSize = sizeof(DEVMODE);

	EnumDisplaySettings(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &devMode);
	refreshRate = devMode.dmDisplayFrequency;
}

HRESULT Streamline::CreateDXGIFactory(REFIID riid, void** ppFactory)
{
	Initialize();
	logger::info("[Streamline] Proxying CreateDXGIFactory");
	auto slCreateDXGIFactory1 = reinterpret_cast<decltype(&CreateDXGIFactory1)>(GetProcAddress(interposer, "CreateDXGIFactory1"));
	return slCreateDXGIFactory1(riid, ppFactory);
}

extern decltype(&D3D11CreateDeviceAndSwapChain) ptrD3D11CreateDeviceAndSwapChain;

HRESULT Streamline::CreateDeviceAndSwapChain(IDXGIAdapter* pAdapter,
	D3D_DRIVER_TYPE DriverType,
	HMODULE Software,
	UINT Flags,
	const D3D_FEATURE_LEVEL* pFeatureLevels,
	UINT FeatureLevels,
	UINT SDKVersion,
	DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
	IDXGISwapChain** ppSwapChain,
	ID3D11Device** ppDevice,
	D3D_FEATURE_LEVEL* pFeatureLevel,
	ID3D11DeviceContext** ppImmediateContext)
{
	logger::info("[Streamline] Creating Device and Swapchain");
	DXGI_ADAPTER_DESC adapterDesc;
	pAdapter->GetDesc(&adapterDesc);

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

	logger::info("[Streamline] DLSS {} available", featureDLSS ? "is" : "is not");
	logger::info("[Streamline] DLSSG {} available", featureDLSSG && !REL::Module::IsVR() ? "is" : "is not");
	logger::info("[Streamline] Reflex {} available", featureReflex ? "is" : "is not");

	HRESULT hr = S_OK;

	if (featureDLSSG && !REL::Module::IsVR()) {
		logger::info("[Streamline] Proxying D3D11CreateDeviceAndSwapChain to add D3D12 swapchain");

		pSwapChainDesc->Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

		auto slD3D11CreateDeviceAndSwapChain = reinterpret_cast<decltype(&D3D11CreateDeviceAndSwapChain)>(GetProcAddress(interposer, "D3D11CreateDeviceAndSwapChain"));

		hr = slD3D11CreateDeviceAndSwapChain(
			pAdapter,
			DriverType,
			Software,
			Flags,
			pFeatureLevels,
			FeatureLevels,
			SDKVersion,
			pSwapChainDesc,
			ppSwapChain,
			ppDevice,
			pFeatureLevel,
			ppImmediateContext);
	} else {
		hr = ptrD3D11CreateDeviceAndSwapChain(
			pAdapter,
			DriverType,
			Software,
			Flags,
			pFeatureLevels,
			FeatureLevels,
			SDKVersion,
			pSwapChainDesc,
			ppSwapChain,
			ppDevice,
			pFeatureLevel,
			ppImmediateContext);

		slSetD3DDevice(*ppDevice);
	}

	PostDevice(pSwapChainDesc);

	return hr;
}

void Streamline::SetupResources()
{
	if (featureDLSSG && !REL::Module::IsVR()) {
		sl::DLSSGOptions options{};
		options.mode = sl::DLSSGMode::eAuto;
		options.flags = sl::DLSSGFlags::eRetainResourcesWhenOff;

		if (SL_FAILED(result, slDLSSGSetOptions(viewport, options))) {
			logger::critical("[Streamline] Could not enable DLSSG");
		} else {
			logger::info("[Streamline] Successfully enabled DLSSG");
		}
	}

	if (featureReflex) {
		sl::ReflexOptions reflexOptions{};
		reflexOptions.mode = sl::ReflexMode::eLowLatencyWithBoost;
		reflexOptions.useMarkersToOptimize = false;
		reflexOptions.virtualKey = 0;
		reflexOptions.frameLimitUs = 0;

		if (SL_FAILED(res, slReflexSetOptions(reflexOptions))) {
			logger::error("[Streamline] Failed to set reflex options");
		} else {
			logger::info("[Streamline] Successfully set reflex options");
		}
	}

	if (featureDLSS || (featureDLSSG && !REL::Module::IsVR())) {
		logger::info("[Streamline] Creating resources");

		auto renderer = globals::game::renderer;
		auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

		D3D11_TEXTURE2D_DESC texDesc{};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

		main.texture->GetDesc(&texDesc);
		main.SRV->GetDesc(&srvDesc);
		main.RTV->GetDesc(&rtvDesc);
		main.UAV->GetDesc(&uavDesc);

		if (featureDLSSG && !REL::Module::IsVR()) {
			texDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
			texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

			texDesc.Format = DXGI_FORMAT_R16_UNORM;
			srvDesc.Format = texDesc.Format;
			rtvDesc.Format = texDesc.Format;
			uavDesc.Format = texDesc.Format;

			depthBufferShared = new Texture2D(texDesc);
			depthBufferShared->CreateUAV(uavDesc);

			copyDepthToSharedBufferCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data\\Shaders\\Streamline\\CopyDepthToSharedBufferCS.hlsl", {}, "cs_5_0");
		}

		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.Format = texDesc.Format;
		rtvDesc.Format = texDesc.Format;
		uavDesc.Format = texDesc.Format;

		colorBufferShared = new Texture2D(texDesc);
	}
}

void Streamline::CopyResourcesToSharedBuffers()
{
	if (!(featureDLSSG && !REL::Module::IsVR()) || settings.frameGenerationMode == sl::DLSSGMode::eOff)
		return;

	auto context = globals::d3d::context;
	auto renderer = globals::game::renderer;

	ID3D11RenderTargetView* backupViews[8];
	ID3D11DepthStencilView* backupDsv;
	context->OMGetRenderTargets(8, backupViews, &backupDsv);  // Backup bound render targets
	context->OMSetRenderTargets(0, nullptr, nullptr);         // Unbind all bound render targets

	auto& swapChain = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kFRAMEBUFFER];

	ID3D11Resource* swapChainResource;
	swapChain.SRV->GetResource(&swapChainResource);

	context->CopyResource(colorBufferShared->resource.get(), swapChainResource);

	{
		auto& depth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];

		{
			auto dispatchCount = Util::GetScreenDispatchCount(true);

			ID3D11ShaderResourceView* views[1] = { depth.depthSRV };
			context->CSSetShaderResources(0, ARRAYSIZE(views), views);

			ID3D11UnorderedAccessView* uavs[1] = { depthBufferShared->uav.get() };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

			context->CSSetShader(copyDepthToSharedBufferCS, nullptr, 0);

			context->Dispatch(dispatchCount.x, dispatchCount.y, 1);
		}

		ID3D11ShaderResourceView* views[1] = { nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[1] = { nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11ComputeShader* shader = nullptr;
		context->CSSetShader(shader, nullptr, 0);
	}

	context->OMSetRenderTargets(8, backupViews, backupDsv);  // Restore all bound render targets

	for (int i = 0; i < 8; i++) {
		if (backupViews[i])
			backupViews[i]->Release();
	}

	if (backupDsv)
		backupDsv->Release();
}

void Streamline::Present()
{
	if (!(featureDLSSG && !REL::Module::IsVR()))
		return;

	UpdateConstants();

	static auto currentFrameGenerationMode = settings.frameGenerationMode;

	if (currentFrameGenerationMode != settings.frameGenerationMode) {
		currentFrameGenerationMode = settings.frameGenerationMode;

		sl::DLSSGOptions options{};
		options.mode = settings.frameGenerationMode;
		options.flags = sl::DLSSGFlags::eRetainResourcesWhenOff;

		if (SL_FAILED(result, slDLSSGSetOptions(viewport, options))) {
			logger::error("[Streamline] Could not set DLSSG");
		}
	}

	if (featureReflex) {
		// Fake NVIDIA Reflex to prevent DLSSG errors
		slReflexSetMarker(sl::ReflexMarker::eInputSample, *frameToken);
		slReflexSetMarker(sl::ReflexMarker::eSimulationStart, *frameToken);
		slReflexSetMarker(sl::ReflexMarker::eSimulationEnd, *frameToken);
		slReflexSetMarker(sl::ReflexMarker::eRenderSubmitStart, *frameToken);
		slReflexSetMarker(sl::ReflexMarker::eRenderSubmitEnd, *frameToken);
		slReflexSetMarker(sl::ReflexMarker::ePresentStart, *frameToken);
		slReflexSetMarker(sl::ReflexMarker::ePresentEnd, *frameToken);
	}

	auto renderer = globals::game::renderer;
	auto state = globals::state;

	auto& motionVectorsBuffer = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::RENDER_TARGET::kMOTION_VECTOR];

	sl::Extent fullExtent{ 0, 0, (uint)state->screenSize.x, (uint)state->screenSize.y };

	float2 dynamicScreenSize = Util::ConvertToDynamic(state->screenSize);
	sl::Extent dynamicExtent{ 0, 0, (uint)dynamicScreenSize.x, (uint)dynamicScreenSize.y };

	sl::Resource depth = { sl::ResourceType::eTex2d, depthBufferShared->resource.get(), 0 };
	sl::ResourceTag depthTag = sl::ResourceTag{ &depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &dynamicExtent };

	sl::Resource mvec = { sl::ResourceType::eTex2d, motionVectorsBuffer.texture, 0 };
	sl::ResourceTag mvecTag = sl::ResourceTag{ &mvec, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &dynamicExtent };

	sl::Resource hudLess = { sl::ResourceType::eTex2d, colorBufferShared->resource.get(), 0 };
	sl::ResourceTag hudLessTag = sl::ResourceTag{ &hudLess, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent, &fullExtent };

	sl::Resource ui = { sl::ResourceType::eTex2d, nullptr, 0 };
	sl::ResourceTag uiTag = sl::ResourceTag{ &ui, sl::kBufferTypeUIColorAndAlpha, sl::ResourceLifecycle::eValidUntilPresent, &fullExtent };

	sl::ResourceTag inputs[] = { depthTag, mvecTag, hudLessTag, uiTag };
	slSetTag(viewport, inputs, _countof(inputs), globals::d3d::context);
}

void Streamline::Upscale(Texture2D* a_upscaleTexture, Texture2D* a_alphaMask, sl::DLSSPreset a_preset)
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

	if (SL_FAILED(res, slGetNewFrameToken(frameToken, nullptr))) {
		logger::error("[Streamline] Could not get frame token");
		return;
	}

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

static void TimerSleepQPC(int64_t targetQPC)
{
	LARGE_INTEGER currentQPC;
	do {
		QueryPerformanceCounter(&currentQPC);
	} while (currentQPC.QuadPart < targetQPC);
}

void Streamline::BeginFrame()
{
	if (featureDLSSG && settings.frameGenerationMode == sl::DLSSGMode::eOn && settings.frameLimitMode) {
		LARGE_INTEGER qpf;
		QueryPerformanceFrequency(&qpf);

		double bestRefreshRate = refreshRate - (refreshRate * refreshRate) / 3600.0;
		int64_t targetFrameTicks = int64_t(double(qpf.QuadPart) / (bestRefreshRate * 0.5));

		static LARGE_INTEGER lastFrame = {};
		LARGE_INTEGER timeNow;
		QueryPerformanceCounter(&timeNow);
		int64_t delta = timeNow.QuadPart - lastFrame.QuadPart;
		if (delta < targetFrameTicks) {
			TimerSleepQPC(lastFrame.QuadPart + targetFrameTicks);
		}
		QueryPerformanceCounter(&lastFrame);
	}
}

void Streamline::Main_RenderWorld::thunk(bool a1)
{
	if (!globals::game::isVR || !globals::state->upscalerLoaded) {
		// With upscaler, VR hangs on this function, specifically at slSetConstants
		globals::streamline->UpdateConstants();
	}
	func(a1);
}