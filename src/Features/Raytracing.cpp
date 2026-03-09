#include "Raytracing.h"

#include "Globals.h"
#include "State.h"

// Microsoft Pix
#include <filesystem>
#include <shlobj.h>
#include <windows.h>

#include "DX12Interop.h"

#include "Deferred.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Raytracing::Settings,
	PerfOverlay,
	CreationEngineRaytracingSettings)

////////////////////////////////////////////////////////////////////////////////////

void Raytracing::RestoreDefaultSettings()
{
	settings = {};
}

void Raytracing::LoadSettings(json& o_json)
{
	settings = o_json;

	if (initialized)
		creationEngineRaytracing->UpdateSettings(settings.CreationEngineRaytracingSettings);
}

void Raytracing::SaveSettings(json& o_json)
{
	o_json = settings;
}

static void DrawFloat2(const char* label, float2& v, float min = 0.0f, float max = 1.0f)
{
	float floats[2] = { v.x, v.y };
	if (ImGui::SliderFloat2(label, floats, min, max)) {
		v = { floats[0], floats[1] };
		v.Clamp({ min, min }, { max, max });
	}
}

template <typename T>
	requires std::is_enum_v<T>
static bool DrawEnumRadio(const char* label, T& variable, const char* tooltip = nullptr, const char* const* tooltips = nullptr)
{
	ImGui::PushID(label);

	auto variablePrev = variable;

	int denoiser = static_cast<int32_t>(variable);
	ImGui::TextUnformatted(label);

	if (tooltip != nullptr)
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", tooltip);

	ImGui::SameLine();
	ImGui::Dummy(ImVec2(25, 0));

	auto i = 0;

	for (auto& [value, name] : magic_enum::enum_entries<T>()) {
		ImGui::SameLine();
		ImGui::RadioButton(name.data(), &denoiser, static_cast<int32_t>(value));

		if (tooltips != nullptr)
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("%s", tooltips[i]);

		i++;
	}

	ImGui::PopID();

	variable = static_cast<T>(denoiser);

	return variable != variablePrev;
}

template <typename T>
	requires std::is_enum_v<T>
static bool DrawEnumCombo(const char* label, T& variable, const char* tooltip = nullptr, const char* const* tooltips = nullptr)
{
	ImGui::PushID(label);

	auto variablePrev = variable;

	if (ImGui::BeginCombo(label, magic_enum::enum_name(variable).data())) {
		auto i = 0;

		for (auto& value : magic_enum::enum_values<T>()) {
			bool isSelected = (variable == value);

			if (ImGui::Selectable(magic_enum::enum_name(value).data(), isSelected))
				variable = value;

			if (tooltips != nullptr)
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text("%s", tooltips[i]);

			if (isSelected)
				ImGui::SetItemDefaultFocus();

			i++;
		}

		ImGui::EndCombo();
	} else if (tooltip != nullptr) {
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", tooltip);
	}

	ImGui::PopID();

	return variable != variablePrev;
}

void Raytracing::DrawSettings()
{
	bool forcedDisabledReason = disableReason != DisableReason::None;

	if (forcedDisabledReason)
		ImGui::BeginDisabled();

	auto ceRTSettingsBefore = settings.CreationEngineRaytracingSettings;

	ImGui::Checkbox("Enabled", &settings.CreationEngineRaytracingSettings.Enabled);

	DrawEnumRadio("Mode", settings.CreationEngineRaytracingSettings.GeneralSettings.Mode);

	bool ptMode = settings.CreationEngineRaytracingSettings.GeneralSettings.Mode == CreationEngineRaytracing::Mode::PathTracing;

	if (ptMode)
		ImGui::BeginDisabled();

	ImGui::Checkbox("Raytraced Shadows", &settings.CreationEngineRaytracingSettings.GeneralSettings.RaytracedShadows);

	if (ptMode)
		ImGui::EndDisabled();

	if (forcedDisabledReason)
		ImGui::EndDisabled();

	if (forcedDisabledReason) {
		ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Ray tracing is disabled: %s", [&]() {
			switch (disableReason) {
			case DisableReason::UnsupportedGPU:
				return "Unsupported GPU.";
			case DisableReason::OutdatedDrivers:
				return "Outdated Drivers.";
			case DisableReason::MissingPlugin:
				return "Missing 'CreationEngineRaytracing.dll', check your mod manager.";
			case DisableReason::InitFailed:
				return "Initialization Failed, check CreationEngineRaytracing.txt log";
			default:
				return "Unknown Reason";
			}
		}());
	}

	if (ImGui::BeginTabBar("Settings")) {
		DrawGeneralSettings();
		DrawDebugSettings();

		ImGui::EndTabBar();
	}

	if (ceRTSettingsBefore != settings.CreationEngineRaytracingSettings)
		creationEngineRaytracing->UpdateSettings(settings.CreationEngineRaytracingSettings);
}

void Raytracing::DrawGeneralSettings()
{
	if (!ImGui::BeginTabItem("General"))
		return;

	ImGui::PushID("GeneralSettings");

	auto& ceRTSettings = settings.CreationEngineRaytracingSettings;

	// RT
	{
		auto& rtSettings = ceRTSettings.RaytracingSettings;

		// Bounces
		if (ImGui::SliderInt("Bounces", &rtSettings.Bounces, 1, 32))
			rtSettings.Bounces = std::clamp(rtSettings.Bounces, 1, 32);

		// Samples Per Pixel
		if (ImGui::SliderInt("Samples Per Pixel", &rtSettings.SamplesPerPixel, 1, 32))
			rtSettings.SamplesPerPixel = std::clamp(rtSettings.SamplesPerPixel, 1, 32);
	}

	DrawSHaRCSettings();

	// Material
	DrawFloat2("Roughness", ceRTSettings.MaterialSettings.Roughness);
	DrawFloat2("Metalness", ceRTSettings.MaterialSettings.Metalness);

	if (ImGui::CollapsingHeader("Light")) {
		auto& lightSettings = ceRTSettings.LightSettings;

		if (ImGui::DragFloat("Directional Strength", &lightSettings.Directional, 0.001f))
			lightSettings.Directional = std::max(0.0f, lightSettings.Directional);

		if (ImGui::DragFloat("Point Strength", &lightSettings.Point, 0.001f))
			lightSettings.Point = std::max(0.0f, lightSettings.Point);

		ImGui::Checkbox("Lod Dimmer", &lightSettings.LodDimmer);

	}

	if (ImGui::CollapsingHeader("Lighting")) {
		auto& lightingSettings = ceRTSettings.LightingSettings;

		if (ImGui::DragFloat("Emissive Strength", &lightingSettings.Emissive, 0.001f))
			lightingSettings.Emissive = std::max(0.0f, lightingSettings.Emissive);

		if (ImGui::DragFloat("Effect Strength", &lightingSettings.Effect, 0.001f))
			lightingSettings.Effect = std::max(0.0f, lightingSettings.Effect);

		if (ImGui::DragFloat("Sky Strength", &lightingSettings.Sky, 0.001f))
			lightingSettings.Sky = std::max(0.0f, lightingSettings.Sky);
	}

	ImGui::PopID();

	ImGui::EndTabItem();
}

void Raytracing::DrawSHaRCSettings()
{
	if (ImGui::CollapsingHeader("SHaRC")) {
		auto& sharcSettings = settings.CreationEngineRaytracingSettings.SHaRCSettings;

		ImGui::DragFloat("Scale", &sharcSettings.SceneScale, 0.001f, 0.1f, 10.0f);
		sharcSettings.SceneScale = std::clamp(sharcSettings.SceneScale, 0.1f, 10.0f);

		ImGui::InputInt("Accumulation Frames", &sharcSettings.AccumFrameNum);
		sharcSettings.AccumFrameNum = std::clamp(sharcSettings.AccumFrameNum, 5, 100);

		ImGui::InputInt("Stale Frames", &sharcSettings.StaleFrameNum);
		sharcSettings.StaleFrameNum = std::clamp(sharcSettings.StaleFrameNum, 8, 128);

		ImGui::Checkbox("Antifirefly Filter", &sharcSettings.AntifireflyFilter);
	}
}

void Raytracing::DrawDebugSettings()
{
	if (!ImGui::BeginTabItem("Debug"))
		return;

	ImGui::PushID("DebugSettings");

	ImGui::Checkbox("Path Tracing Cull", &settings.CreationEngineRaytracingSettings.DebugSettings.PathTracingCull);

	ImGui::Checkbox("Performance Overlay", &settings.PerfOverlay);

	ImGui::PopID();

	ImGui::EndTabItem();
}

void Raytracing::DrawOverlay()
{
	auto* menu = Menu::GetSingleton();

	if (!globals::state || !menu)
		return;

	// Set window flags - no decoration and only movable when ShowBorder is true
	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize;

	// Only allow mouse interaction when the main menu is open
	if (!menu->IsEnabled) {
		windowFlags |= ImGuiWindowFlags_NoInputs;
	}

	windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse;

	if (!PositionSet) {
		Position = ImVec2(10, 10);
		ImGui::SetNextWindowPos(Position);	
		PositionSet = true;
	} else {
		ImGui::SetNextWindowPos(Position, ImGuiCond_FirstUseEver);
	}

	ImGui::Begin("Raytracing Overlay", NULL, windowFlags);

	auto DrawRow = [](const char* label, size_t instances, float cpums, float gpums, [[maybe_unused]] double frameTime = 0.0f) {
		ImGui::TableNextRow();

		ImGui::TableNextColumn();
		ImGui::Text(label);

		ImGui::TableNextColumn();
		ImGui::Text("%zu", instances);

		ImGui::TableNextColumn();
		ImGui::Text("%g ms", cpums);

		ImGui::TableNextColumn();
		ImGui::Text("%g ms", gpums);
	};

	if (ImGui::BeginTable("Effects", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
		ImGui::TableSetupColumn("Effect");
		ImGui::TableSetupColumn("Instances");
		ImGui::TableSetupColumn("CPU");
		ImGui::TableSetupColumn("GPU");
		ImGui::TableHeadersRow();

		// GI/PT
		DrawRow("Frame Time", 0, 0, *frameTime);

		ImGui::EndTable();
	}

	ImGui::End();
}

void Raytracing::Load()
{
	Hooks::Install();
}

void Raytracing::PostPostLoad()
{
	creationEngineRaytracing = eastl::make_unique<CreationEngineRaytracing>();

	if (!creationEngineRaytracing->handle) {
		settings.CreationEngineRaytracingSettings.Enabled = false;
		forcedDisabled = true;
		disableReason = DisableReason::MissingPlugin;
		return;
	}

	RE::GetINISetting("bReflectLODLand:Water")->data.b = false;
	RE::GetINISetting("bReflectLODObjects:Water")->data.b = false;
	RE::GetINISetting("bReflectLODTrees:Water")->data.b = false;
	RE::GetINISetting("bReflectSky:Water")->data.b = true;
}

void Raytracing::DataLoaded()
{
	BGSActorCellEventHandler::Register();
}

void Raytracing::CompileShaders()
{
	const auto skyHemiSize = std::to_string(SKY_HEMI_SIZE);
	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\Raytracing\\CubeToHemiCS.hlsl", { { "RESOLUTION", skyHemiSize.c_str() } }, "cs_5_0")); rawPtr)
		cubeToHemiCS.attach(rawPtr);

	if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\Raytracing\\PTCompositeCS.hlsl", {}, "cs_5_0")); rawPtr)
		ptCompositeCS.attach(rawPtr);
}

void Raytracing::InitializeCERaytracing(ID3D11Device5* d3d11Device, ID3D12Device5* d3d12Device, ID3D12CommandQueue* commandQueue, ID3D12CommandQueue* computeCommandQueue, ID3D12CommandQueue* copyCommandQueue)
{
	if (initialized)
		return;

	bool result = creationEngineRaytracing->Initialize(d3d11Device, d3d12Device, commandQueue, computeCommandQueue, copyCommandQueue);

	if (!result) {
		settings.CreationEngineRaytracingSettings.Enabled = false;
		forcedDisabled = true;
		disableReason = DisableReason::InitFailed;

		logger::error("[Raytracing] Failed to initialize Creation Engine ray tracing.");
		return;
	}

	initialized = true;

	creationEngineRaytracing->UpdateSettings(settings.CreationEngineRaytracingSettings);

	UpdateResolution();

	frameTime = creationEngineRaytracing->GetFrameTime();

	logger::info("[Raytracing] Successfully initialized Creation Engine ray tracing.");
}

bool Raytracing::UpdateResolution()
{
	uint2 resolution = { static_cast<uint32_t>(globals::state->screenSize.x), static_cast<uint32_t>(globals::state->screenSize.y) };

	if (resolution == m_Resolution)
		return false;

	m_Resolution = resolution;

	creationEngineRaytracing->SetResolution(m_Resolution.x, m_Resolution.y);

	return true;
}

void ShareTexture(ID3D11Texture2D* d3d11Texture, ID3D12Resource** d3d12Resource, bool nt = false, uint accessFlags = DXGI_SHARED_RESOURCE_READ) // DXGI_SHARED_RESOURCE_WRITE
{
	D3D11_TEXTURE2D_DESC desc;
	d3d11Texture->GetDesc(&desc);

	IDXGIResource1* dxgiResource;
	DX::ThrowIfFailed(d3d11Texture->QueryInterface(IID_PPV_ARGS(&dxgiResource)));

	HANDLE sharedHandle = nullptr;

	if (nt)
		DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(nullptr, accessFlags, nullptr, &sharedHandle));
	else
		DX::ThrowIfFailed(dxgiResource->GetSharedHandle(&sharedHandle));

	DX::ThrowIfFailed(globals::features::dx12Interop.d3d12Device->OpenSharedHandle(sharedHandle, IID_PPV_ARGS(d3d12Resource)));

	CloseHandle(sharedHandle);
}

void Raytracing::SetupResources()
{
	auto renderer = globals::game::renderer;

	D3D11_TEXTURE2D_DESC mainDesc;
	auto mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	mainTex.texture->GetDesc(&mainDesc);

	if (initialized) {
		creationEngineRaytracing->SetResolution(mainDesc.Width, mainDesc.Height);
	}

	auto& d3d11Device = globals::features::dx12Interop.d3d11Device;

	// Gbuffer Textures
	{
		ShareTexture(renderer->GetRuntimeData().renderTargets[ALBEDO].texture, albedoTexture.put());
		ShareTexture(renderer->GetRuntimeData().renderTargets[MASKS2].texture, gnmaoTexture.put());
	}

	featureData = eastl::make_unique<FeatureData>();

	screenCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<ScreenData>());

	screenData = eastl::make_unique<ScreenData>();

	logger::debug("Creating samplers...");
	{
		D3D11_SAMPLER_DESC samplerDesc = {
			.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
			.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
			.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
			.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
			.MaxAnisotropy = 1,
			.MinLOD = 0,
			.MaxLOD = D3D11_FLOAT32_MAX
		};
		DX::ThrowIfFailed(d3d11Device->CreateSamplerState(&samplerDesc, samplerState.put()));
	}

	// Sky Hemisphere
	{
		D3D11_TEXTURE2D_DESC texDesc{};
		texDesc.Width = SKY_HEMI_SIZE;
		texDesc.Height = SKY_HEMI_SIZE;
		texDesc.MipLevels = 1;
		texDesc.ArraySize = 1;
		texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		texDesc.SampleDesc.Count = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		skyHemisphere = eastl::make_unique<WrappedResource>(texDesc);
		DX::ThrowIfFailed(skyHemisphere->resource->SetName(L"Sky Hemisphere"));

		// Setup TESWaterReflections
		waterReflections = RE::NiPointer(new RE::TESWaterReflections());

		waterReflections->flags.set(true, RE::TESWaterReflections::Flags::kDirty, RE::TESWaterReflections::Flags::kDynamicCubemap, RE::TESWaterReflections::Flags::kWorldOrigin);

		for (uint i = 0; i < 6; i++) {
			waterReflections->cubeMapSides[i] = RE::TESWaterReflections::CubeMapSide(i, 0.0f);
		}
	
		creationEngineRaytracing->SetSkyHemisphere(skyHemisphere->resource.get());
	}

	CompileShaders();
}

Raytracing::SharedData Raytracing::GetCommonBufferData() const
{
	return {
		.InteriorDirectional = settings.CreationEngineRaytracingSettings.Enabled ? 0.0f : 1.0f,
		.Ambient = settings.CreationEngineRaytracingSettings.Enabled ? 0.0f : 1.0f,
		.EnvMap = settings.CreationEngineRaytracingSettings.Enabled ? 0.0f : 1.0f,
		.Albedo = settings.CreationEngineRaytracingSettings.Enabled
	};
}

void Raytracing::UpdateFeatureData()
{
	auto wetnessEffect = globals::features::wetnessEffects.GetCommonBufferData();
	auto linearLighting = globals::features::linearLighting.GetCommonBufferData();

	std::memcpy(&featureData->ExtendedMaterials, &globals::features::extendedMaterials.settings, sizeof(ExtendedMaterials::Settings));
	std::memcpy(&featureData->WetnessEffects, &wetnessEffect, sizeof(WetnessEffects::PerFrame));
	std::memcpy(&featureData->CloudShadows, &globals::features::cloudShadows.settings, sizeof(CloudShadows::Settings));
	std::memcpy(&featureData->HairSpecular, &globals::features::hairSpecular.settings, sizeof(HairSpecular::Settings));
	std::memcpy(&featureData->ExtendedTranslucency, &globals::features::extendedTranslucency.GetCommonBufferData(), sizeof(ExtendedTranslucency::PerFrame));
	std::memcpy(&featureData->LinearLighting, &linearLighting, sizeof(LinearLighting::PerFrameData));

	static_assert(sizeof(FeatureData::ExtendedMaterials) == sizeof(ExtendedMaterials::Settings));
	static_assert(sizeof(FeatureData::WetnessEffects) == sizeof(WetnessEffects::PerFrame));
	static_assert(sizeof(FeatureData::CloudShadows) == sizeof(CloudShadows::Settings));
	static_assert(sizeof(FeatureData::HairSpecular) == sizeof(HairSpecular::Settings));
	static_assert(sizeof(FeatureData::ExtendedTranslucency) == sizeof(ExtendedTranslucency::PerFrame));
	static_assert(sizeof(FeatureData::LinearLighting) == sizeof(LinearLighting::PerFrameData));

	creationEngineRaytracing->UpdateFeatureData(featureData.get(), sizeof(FeatureData));
}

void Raytracing::SkyCubeToHemi() const
{
	auto context = globals::d3d::context;

	context->CSSetShader(cubeToHemiCS.get(), nullptr, 0);

	auto reflections = globals::game::renderer->GetRendererData().cubemapRenderTargets[RE::RENDER_TARGET_CUBEMAP::kREFLECTIONS];
	auto reflectionOcc = globals::features::cloudShadows.loaded ? globals::features::cloudShadows.texCubemapCloudOccCopy->srv.get() : nullptr;

	eastl::array<ID3D11ShaderResourceView*, 2> srvs = {
		reflections.SRV,
		reflectionOcc
	};
	context->CSSetShaderResources(0, (UINT)srvs.size(), srvs.data());

	auto sampler = samplerState.get();
	context->CSSetSamplers(0, 1, &sampler);

	ID3D11UnorderedAccessView* uav = skyHemisphere->uav;
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

	uint dispatch = (uint)std::ceil(SKY_HEMI_SIZE / 8.0f);
	context->Dispatch(dispatch, dispatch, 1);

	uav = nullptr;
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
}

void Raytracing::DeferredPasses()
{
	if (!settings.CreationEngineRaytracingSettings.Enabled)
		return;

	bool resolutionChanged = UpdateResolution();

	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = m_Resolution.x;
	desc.Height = m_Resolution.y;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	if (!mainTexture || resolutionChanged) {
		desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		mainTexture = eastl::make_unique<WrappedResource>(desc);
		creationEngineRaytracing->SetCopyTarget(mainTexture->resource.get());
	}

	creationEngineRaytracing->WaitExecution();

	auto screenSize = globals::state->screenSize;
	auto dynamicScreenSize = Util::ConvertToDynamic(screenSize);

	screenData->Resolution = { static_cast<uint>(screenSize.x), static_cast<uint>(screenSize.y) };
	screenData->DynamicResolution = { static_cast<uint>(dynamicScreenSize.x), static_cast<uint>(dynamicScreenSize.y) };

	screenCB->Update(screenData.get(), sizeof(ScreenData));

	auto mode = settings.CreationEngineRaytracingSettings.GeneralSettings.Mode;

	auto renderer = globals::game::renderer;
	auto* context = globals::d3d::context;

	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	if (mode == CreationEngineRaytracing::Mode::GlobalIllumination) {
		context->CopyResource(main.texture, mainTexture->resource11);
	} 
	else if (mode == CreationEngineRaytracing::Mode::PathTracing) {
		// Blend PT and Sky
		{
			ID3D11Buffer* cb = screenCB->CB();
			context->CSSetConstantBuffers(0, 1, &cb);
			context->CSSetShader(ptCompositeCS.get(), nullptr, 0);

			context->CSSetShaderResources(0, 1, &mainTexture->srv);

			ID3D11UnorderedAccessView* uav = main.UAV;
			context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

			auto dispatchCount = Util::GetScreenDispatchCount(true);
			context->Dispatch(dispatchCount.x, dispatchCount.y, 1);

			uav = nullptr;
			context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		}

		// Clear Specular RT
		{
			float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			context->ClearRenderTargetView(renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kINDIRECT_DOWNSCALED].RTV, clearColor);
		}
	}

	if (pixCapture && pixCaptureStarted) {
		ga->EndCapture();

		pixCapture = false;
		pixCaptureStarted = false;
	}

	if (pixCapture && !pixCaptureStarted) {
		pixCaptureStarted = true;

		ga->BeginCapture();
	}
}

RE::BSEventNotifyControl Raytracing::BGSActorCellEventHandler::ProcessEvent(const RE::BGSActorCellEvent* a_event, RE::BSTEventSource<RE::BGSActorCellEvent>*)
{
	if (a_event->flags.underlying() != static_cast<uint32_t>(RE::BGSActorCellEvent::CellFlag::kEnter))
		return RE::BSEventNotifyControl::kContinue;

	auto* tesWaterSystem = RE::TESWaterSystem::GetSingleton();

	if (tesWaterSystem->waterReflections.empty()) {
		tesWaterSystem->waterReflections.push_back(globals::features::raytracing.waterReflections);
	}

	tesWaterSystem->Enable();

	return RE::BSEventNotifyControl::kContinue;
}