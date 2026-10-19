#include "State.h"

#include <algorithm>
#include <cmath>
#include <codecvt>
#include <unordered_set>

#include <pystring/pystring.h>

#include "BaselineManager.h"
#include "CSEditor/SceneManager/SceneManager.h"
#include "CSEditor/SceneManager/SceneSettingsManager.h"
#include "Deferred.h"
#include "EngineFixes/LODBlendRingFix.h"
#include "EngineFixes/LandscapeSeamsFix.h"
#include "EngineFixes/SunShadowMaskFix.h"
#include "FeatureIssues.h"
#include "Features/CSEditor.h"
#include "Features/CloudShadows.h"
#include "Features/Effects11.h"
#include "Features/ExponentialHeightFog.h"
#include "Features/HDRDisplay.h"
#include "Features/InteriorSun.h"
#include "Features/PerformanceOverlay.h"
#include "Features/PostProcessing.h"
#include "Features/RainRefraction.h"
#include "Features/RemoteControl/NvPerf.h"
#include "Features/Skin.h"
#include "Features/SkySync.h"
#include "Features/Skylighting.h"
#include "Features/TerrainBlending.h"
#include "Features/TerrainHelper.h"
#include "Features/Upscaling.h"
#include "Features/VolumetricShadows.h"
#include "Menu.h"
#include "PostProcessingMode.h"
#include "Presets/UnifiedPresetCatalog.h"
#include "ShaderCache.h"
#include "TruePBR.h"
#include "Utils/FileSystem.h"
#include "Utils/SettingsCatalog.h"
#include "Utils/SphericalHarmonics.h"

#ifdef TRACY_ENABLE
static thread_local std::vector<TracyCZoneCtx> s_tracyPerfZones;
#endif

void State::UpdateLightingShaderPermutation(RE::BSRenderPass* a_pass)
{
	constexpr auto additiveLighting = static_cast<uint32_t>(ExtraShaderDescriptors::AdditiveLighting);
	permutationData.ExtraShaderDescriptor &= ~additiveLighting;

	if (!a_pass || !a_pass->geometry)
		return;

	auto* alphaProperty = a_pass->geometry->GetGeometryRuntimeData().alphaProperty.get();
	if (alphaProperty && alphaProperty->GetAlphaBlending() &&
		alphaProperty->GetDestBlendMode() == RE::NiAlphaProperty::AlphaFunction::kOne) {
		permutationData.ExtraShaderDescriptor |= additiveLighting;
	}
}

void State::UpdateSkyShaderPermutation(RE::BSRenderPass* a_pass)
{
	permutationData.ExtraShaderDescriptor &= ~static_cast<uint32_t>(State::ExtraShaderDescriptors::IsSun);
	permutationData.ExtraShaderDescriptor &= ~static_cast<uint32_t>(State::ExtraShaderDescriptors::IsMoon);
	permutationData.ExtraShaderDescriptor &= ~static_cast<uint32_t>(State::ExtraShaderDescriptors::NoSkyScattering);

	if (!a_pass || !a_pass->shaderProperty)
		return;

	auto* skyProperty = static_cast<const RE::BSSkyShaderProperty*>(a_pass->shaderProperty);
	if (skyProperty->uiSkyObjectType == RE::BSSkyShaderProperty::SkyObject::SO_CLOUDS) {
		// Bottled Shaders keeps cloud layer 28 out of the scattering
		constexpr std::uint16_t kNoScatteringCloudLayer = 28;
		auto* sky = globals::game::sky;
		if (sky && sky->clouds && kNoScatteringCloudLayer < sky->clouds->numLayers &&
			sky->clouds->clouds[kNoScatteringCloudLayer].get() == a_pass->geometry)
			permutationData.ExtraShaderDescriptor |= static_cast<uint32_t>(State::ExtraShaderDescriptors::NoSkyScattering);
	}
	if (skyProperty->uiSkyObjectType == RE::BSSkyShaderProperty::SkyObject::SO_SUN ||
		skyProperty->uiSkyObjectType == RE::BSSkyShaderProperty::SkyObject::SO_SUN_GLARE) {
		permutationData.ExtraShaderDescriptor |= static_cast<uint32_t>(State::ExtraShaderDescriptors::IsSun);
	}
	if (skyProperty->uiSkyObjectType == RE::BSSkyShaderProperty::SkyObject::SO_MOON) {
		permutationData.ExtraShaderDescriptor |= static_cast<uint32_t>(State::ExtraShaderDescriptors::IsMoon);
	}

	// The glare VS fades itself by scene depth coverage around the sun
	if (skyProperty->uiSkyObjectType == RE::BSSkyShaderProperty::SkyObject::SO_SUN_GLARE) {
		auto* context = globals::d3d::context;
		ID3D11Buffer* buffers[] = { permutationCB->CB(), sharedDataCB->CB() };
		context->VSSetConstantBuffers(4, static_cast<UINT>(std::size(buffers)), buffers);
		// The sky draws with the z-prepass copy as its DSV, which would null an SRV of it; kMAIN holds the same depth
		auto* depthSRV = globals::game::renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].depthSRV;
		context->VSSetShaderResources(17, 1, &depthSRV);
	}
}

void State::UpdateEffectShaderPermutation(RE::BSRenderPass* a_pass)
{
	constexpr auto isAurora = static_cast<uint32_t>(ExtraShaderDescriptors::IsAurora);
	permutationData.ExtraShaderDescriptor &= ~isAurora;

	if (!a_pass || !a_pass->geometry)
		return;
	if (!(currentVertexDescriptor & static_cast<uint32_t>(SIE::ShaderCache::EffectShaderFlags::SkyObject)))
		return;

	const auto sky = globals::game::sky;
	if (!sky || !sky->auroraRoot)
		return;

	for (const RE::NiAVObject* node = a_pass->geometry; node; node = node->parent) {
		if (node == sky->auroraRoot.get()) {
			permutationData.ExtraShaderDescriptor |= isAurora;
			return;
		}
	}
}

void State::Draw()
{
	ZoneScoped;
	if (globals::features::sceneManager.loaded)
		globals::features::sceneManager.Update();

	auto shaderCache = globals::shaderCache;
	auto& terrainBlending = globals::features::terrainBlending;
	auto& terrainHelper = globals::features::terrainHelper;
	auto& cloudShadows = globals::features::cloudShadows;
	auto& skin = globals::features::skin;
	auto& truePBR = globals::features::truePBR;
	auto context = globals::d3d::context;
	auto& volumetricShadows = globals::features::volumetricShadows;
	auto& skylighting = globals::features::skylighting;

	if (shaderCache->IsEnabled()) {
		if (terrainBlending.loaded && terrainBlending.settings.Enabled) {
			ZoneScopedN("TerrainBlending::TerrainShaderHacks");
			terrainBlending.TerrainShaderHacks();
		}

		if (cloudShadows.loaded) {
			ZoneScopedN("CloudShadows::SkyShaderHacks");
			cloudShadows.SkyShaderHacks();
		}

		if (globals::features::rainRefraction.loaded) {
			ZoneScopedN("RainRefraction::ParticleShaderHacks");
			globals::features::rainRefraction.ParticleShaderHacks();
		}

		if (terrainHelper.loaded) {
			ZoneScopedN("TerrainHelper::SetShaderResources");
			terrainHelper.SetShaderResources(context);
		}

		if (skin.loaded) {
			ZoneScopedN("Skin::SetShaderResources");
			skin.SetShaderResources(context);
		}

		if (truePBR.loaded) {
			ZoneScopedN("TruePBR::SetShaderResources");
			truePBR.SetShaderResources(context);
		}

		if (permutationData != permutationDataPrevious) {
			permutationCB->Update(permutationData);
			permutationDataPrevious = permutationData;
		}

		if (currentShader && updateShader) {
			if (currentShader->shaderType.get() == RE::BSShader::Type::Utility) {
				if (currentPixelDescriptor & static_cast<uint32_t>(SIE::ShaderCache::UtilityShaderFlags::RenderShadowmask)) {
					if (volumetricShadows.loaded)
						volumetricShadows.CopyShadowLightData();
					if (globals::features::exponentialHeightFog.loaded)
						globals::features::exponentialHeightFog.CaptureDirectionalShadowMap();
					if (skylighting.loaded)
						skylighting.CaptureShadowCascadeSRV();
				}
			}
		}

		if (globals::menu->overlayVisible && globals::features::performanceOverlay.loaded && globals::features::performanceOverlay.IsOverlayVisible())
			Debug();

		updateShader = false;
	}
}

void State::Debug()
{
	auto lock = Lock();

	if (frameChecker.IsNewFrame()) {
		// Smooth draw calls and frame times for all shader types
		for (int i = 0; i < magic_enum::enum_integer(RE::BSShader::Type::Total) + 1; ++i) {
			smoothDrawCalls[i] = smoothDrawCalls[i] * static_cast<float>(0.95) + drawCalls[i] * static_cast<float>(0.05);
			smoothFrameTimePerType[i] = smoothFrameTimePerType[i] * static_cast<float>(0.95) + frameTimePerType[i] * static_cast<float>(0.05);
		}
		// Reset counters for next frame
		for (auto& c : drawCalls)
			c = 0;
		for (auto& ft : frameTimePerType)
			ft = 0.0f;

		// Reset active shader tracking for developer mode
		globals::shaderCache->ResetFrameShaderTracking();

		// Start timing for this frame
		if (frameTimingFrequency.QuadPart == 0) {
			QueryPerformanceFrequency(&frameTimingFrequency);
		}
		QueryPerformanceCounter(&frameStartTime);
		frameTimingActive = true;
	}

	// Track time for current shader type if timing is active
	if (frameTimingActive && currentShader) {
		LARGE_INTEGER currentTime;
		QueryPerformanceCounter(&currentTime);

		// Calculate elapsed time in milliseconds
		float elapsed = (currentTime.QuadPart - frameStartTime.QuadPart) * 1000.0f / frameTimingFrequency.QuadPart;

		// Add elapsed time to the current shader type
		frameTimePerType[magic_enum::enum_integer(currentShader->shaderType.get())] += elapsed;
		frameTimePerType[magic_enum::enum_integer(RE::BSShader::Type::Total)] += elapsed;

		// Update start time for next measurement
		frameStartTime = currentTime;
	}

	if (currentShader) {
		drawCalls[magic_enum::enum_integer(currentShader->shaderType.get())]++;
		drawCalls[magic_enum::enum_integer(RE::BSShader::Type::Total)]++;
	}

	if (currentShader && updateShader && frameAnnotations) {
		BeginPerfEvent(std::format("Draw: CS {}::{:x}::{}", magic_enum::enum_name(currentShader->shaderType.get()), permutationData.PixelShaderDescriptor, currentShader->fxpFilename));
		SetPerfMarker(std::format("Defines: {}", SIE::ShaderCache::GetDefinesString(*currentShader, permutationData.PixelShaderDescriptor)));
		EndPerfEvent();
	}
}

State::TonemapOwner State::GetTonemapOwner()
{
	if (tonemapOwner)
		return *tonemapOwner;
	auto& effects11 = globals::features::effects11;
	auto& postProcessing = globals::features::postProcessing;

	// Effects11 does not run over the main menu or loading screen: those composite UI and
	// scene into one buffer, so an ENB preset would grade the menu itself. It must not claim
	// ownership there either, or Post Processing would lose its tonemap to a pass that never
	// renders.
	const bool effects11CanRender = !IsMainOrLoadingMenuOpen();

	// Effects11 wins ties: its effects form a complete ENB preset (tonemap, bloom, lens,
	// adaptation) that looks wrong when only partially applied.
	if (effects11.loaded && effects11CanRender && effects11.WantsTonemapOwnership())
		tonemapOwner = TonemapOwner::kEffects11;
	else if (postProcessing.loaded && postProcessing.WantsTonemapOwnership())
		tonemapOwner = TonemapOwner::kPostProcessing;
	else
		tonemapOwner = TonemapOwner::kVanilla;
	postProcessing.UpdateVanillaEffects(*tonemapOwner == TonemapOwner::kPostProcessing);
	return *tonemapOwner;
}

bool State::HandlePostProcessing(RE::RENDER_TARGET a_input, RE::RENDER_TARGET a_output)
{
	if (GetTonemapOwner() != TonemapOwner::kEffects11)
		return false;

	if (!globals::features::effects11.RenderTonemap(a_input, a_output))
		return false;

	auto renderer = globals::game::renderer;
	auto& outputRT = renderer->GetRuntimeData().renderTargets[a_output];
	globals::d3d::context->OMSetRenderTargets(1, &outputRT.RTV, nullptr);

	auto shadowState = globals::game::shadowState;
	auto& stateData = shadowState->GetRuntimeData();
	stateData.renderTargets[0] = a_output;
	stateData.setRenderTargetMode[0] = RE::BSGraphics::SetRenderTargetMode::SRTM_NO_CLEAR;
	for (int i = 1; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++) {
		stateData.renderTargets[i] = RE::RENDER_TARGET::kNONE;
		stateData.setRenderTargetMode[i] = RE::BSGraphics::SetRenderTargetMode::SRTM_NO_CLEAR;
	}
	stateData.depthStencil = static_cast<uint32_t>(-1);

	return true;
}

/**
 * @brief Resets per-frame state and publishes frame counter to off-thread readers.
 *
 * Ends the current profiler frame, resets all loaded features, updates the
 * timer if the game is not paused, caches current menu open states, clears
 * shader descriptor tracking, increments and atomically publishes the frame
 * count, and disables reflection and improved snow shaders.
 */
void State::Reset()
{
	globals::profiler->EndFrame();

	Feature::ForEachLoadedFeature("Reset", [](Feature* feature) { feature->Reset(); });
	LandscapeSeamsFix::GetInstance().Reset();
	LODBlendRingFix::GetInstance().Update();
	SunShadowMaskFix::Update();

	// Cache menu open states once per frame to avoid repeated IsMenuOpen calls
	// (each call constructs a BSFixedString, which is expensive at scale).
	if (auto ui = globals::game::ui) {
		if (!ui->GameIsPaused())
			timer += RE::GetSecondsSinceLastFrame();

		isMainMenuOpen = ui->IsMenuOpen(RE::MainMenu::MENU_NAME);
		isLoadingMenuOpen = ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME);
		isMapMenuOpen = ui->IsMenuOpen(RE::MapMenu::MENU_NAME);
		isStatsMenuOpen = ui->IsMenuOpen(RE::StatsMenu::MENU_NAME);
	} else {
		isMainMenuOpen = false;
		isLoadingMenuOpen = false;
		isMapMenuOpen = false;
		isStatsMenuOpen = false;
	}

	lastModifiedPixelDescriptor = 0;
	lastModifiedVertexDescriptor = 0;
	lastPixelDescriptor = 0;
	lastVertexDescriptor = 0;
	std::memset(&permutationDataPrevious, 0xFF, sizeof(PermutationCB));
	frameCount++;
	tonemapOwner.reset();
	// Publish for off-thread readers (e.g. the MCP listener thread).
	frameCountAtomic.store(frameCount, std::memory_order_relaxed);

	if (auto* imageSpaceManager = globals::game::imageSpaceManager) {
		auto& BSImagespaceShaderApplyReflections = imageSpaceManager->GetRuntimeData().BSImagespaceShaderApplyReflections;

		// Disable reflections being applied to things other than water
		if (BSImagespaceShaderApplyReflections.get()) {
			BSImagespaceShaderApplyReflections->active = false;
		}
	}

	// Disable "improved" snow shader, unsupported
	RE::GetINISetting("bEnableImprovedSnow:Display")->data.b = false;

	activeReflections = false;
}

void State::Setup()
{
	globals::features::truePBR.SetupResources();
	SetupResources();

	// Probe typed UAV load support before features set up their resources, so any
	// gating logic that wants to read the log can run during feature SetupResources.
	CheckTypedUAVLoadSupport();

	Feature::ForEachLoadedFeature("SetupResources", [](Feature* feature) { feature->SetupResources(); });
	globals::deferred->SetupResources();
	Feature::ForEachLoadedFeature("PostSetupResources", [](Feature* feature) { feature->PostSetupResources(); });
	PostProcessingMode::ApplyPending();
	tonemapOwner.reset();
}

namespace
{
	/** @brief Loads layered settings into a feature, putting current back when a malformed layer throws partway.
	 *  @return Whether the layered settings were applied. */
	bool LoadLayeredSettings(Feature* feature, json& layered, json& current)
	{
		try {
			feature->LoadSettings(layered);
			return true;
		} catch (const std::exception& e) {
			logger::warn("Could not apply Baseline settings to {}, keeping its settings: {}", feature->GetName(), e.what());
			try {
				feature->LoadSettings(current);
			} catch (...) {
			}
			return false;
		}
	}

	/** @brief Equality that tolerates float noise: JSON stores float64 while features hold float32. */
	bool ValuesMatch(const json& lhs, const json& rhs)
	{
		if (lhs.is_number() && rhs.is_number()) {
			const double lhsValue = lhs.get<double>();
			const double rhsValue = rhs.get<double>();
			return std::abs(lhsValue - rhsValue) <= std::max(1e-6, std::abs(rhsValue) * 1e-5);
		}
		if (lhs.is_array() && rhs.is_array()) {
			if (lhs.size() != rhs.size())
				return false;
			for (size_t index = 0; index < lhs.size(); ++index)
				if (!ValuesMatch(lhs[index], rhs[index]))
					return false;
			return true;
		}
		if (lhs.is_object() && rhs.is_object()) {
			if (lhs.size() != rhs.size())
				return false;
			for (const auto& [key, value] : lhs.items())
				if (const auto found = rhs.find(key); found == rhs.end() || !ValuesMatch(value, *found))
					return false;
			return true;
		}
		return lhs == rhs;
	}

	/** @brief Whether edits set any setting the pack data also sets. */
	bool SharesSettings(const json& edits, const json& packData)
	{
		for (const auto& [key, value] : packData.items()) {
			const auto found = edits.find(key);
			if (found == edits.end())
				continue;
			if (!value.is_object() || !found->is_object() || SharesSettings(*found, value))
				return true;
		}
		return false;
	}

	/** @brief Removes from edits every setting the pack data sets, and any group left empty. */
	void RemoveSharedSettings(json& edits, const json& packData)
	{
		for (const auto& [key, value] : packData.items()) {
			const auto found = edits.find(key);
			if (found == edits.end())
				continue;
			if (value.is_object() && found->is_object()) {
				RemoveSharedSettings(*found, value);
				if (!found->empty())
					continue;
			}
			edits.erase(found);
		}
	}

	/** @brief What one Baseline pack sets for a feature, merged across its files. */
	json GetPackFeatureData(std::string_view packId, const std::string& featureShortName)
	{
		json data = json::object();
		for (const auto* info : BaselineManager::GetSingleton()->GetFeatureFiles(featureShortName))
			if ((packId.empty() || info->packId == packId) && info->unknownKeys.empty())
				Util::Settings::MergeSettings(data, info->values);
		return data;
	}

	/** @brief The parts of current that differ from base: objects compare key by key, anything else whole. */
	json DiffFromBase(const json& current, const json& base)
	{
		json diff = json::object();
		if (!current.is_object())
			return diff;
		for (const auto& [key, value] : current.items()) {
			const auto found = base.find(key);
			if (found == base.end()) {
				diff[key] = value;
			} else if (value.is_object() && found->is_object()) {
				if (auto nested = DiffFromBase(value, *found); !nested.empty())
					diff[key] = std::move(nested);
			} else if (!ValuesMatch(value, *found)) {
				diff[key] = value;
			}
		}
		return diff;
	}
}

void State::Load(bool a_allowReload)
{
	SceneSettingsManager::SceneLayerGuard sceneLayerGuard;
	bool errorDetected = false;

	const auto userConfigFilePath = Util::PathHelpers::GetSettingsUserPath();

	try {
		std::filesystem::create_directories(userConfigFilePath.parent_path());
	} catch (const std::filesystem::filesystem_error& e) {
		logger::warn("Error creating directory during Load ({}) : {}\n", userConfigFilePath.parent_path().string(), e.what());
		errorDetected = true;
	}

	// LOADING ORDER, per feature: the code's defaults → enabled Baseline packs → the user's edits.
	// Sections that are not feature settings (Menu, General, ...) come from the user's config as a whole.

	// Step 1: The code's defaults. Captured on the first load, before any settings are applied, so later
	// loads (Restore Saved Settings, A/B testing) don't take the user's values for defaults.
	if (defaultSettings.is_null()) {
		try {
			SaveToJson(defaultSettings);
		} catch (const std::exception& e) {
			logger::error("Failed to capture default settings: {}", e.what());
			defaultSettings = json::object();
		}
	}
	json settings = defaultSettings;

	// Step 2: Read the user's config
	json userSettings = json::object();
	if (auto loaded = Util::FileHelpers::ReadJsonObject(userConfigFilePath, "user config")) {
		userSettings = std::move(*loaded);
		logger::info("Applied user settings from: {}", userConfigFilePath.string());
	} else {
		logger::info("No usable user config at: {}", userConfigFilePath.string());
	}

	// Step 3: Layer each feature's settings; the user's edits win over Baseline packs
	BaselineManager::GetSingleton()->DiscoverFiles();
	featureBases.clear();
	InvalidateEditsUnderBaselines();
	userFeatureEdits.clear();
	for (auto* feature : Feature::GetFeatureList()) {
		if (!feature->UsesMainSettings())
			continue;
		const auto name = feature->GetName();
		json edits = json::object();
		if (const auto found = userSettings.find(name); found != userSettings.end()) {
			if (found->is_object())
				edits = *found;
			userSettings.erase(found);
		}
		json merged = BuildFeatureBase(feature);
		// A value whose type changed between versions would make LoadSettings throw and lose every edit.
		std::vector<std::string> mismatched;
		Util::Settings::EraseMismatchedSettings(edits, merged, "", mismatched);
		for (const auto& path : mismatched)
			logger::warn("Dropped {} setting '{}': its saved type no longer matches", name, path);
		featureBases[name] = merged;
		Util::Settings::MergeSettings(merged, edits);
		userFeatureEdits[name] = std::move(edits);
		if (merged.empty())
			settings.erase(name);
		else
			settings[name] = std::move(merged);
	}
	for (const auto& [key, value] : userSettings.items())
		settings[key] = value;

	try {
		// Load core settings (Menu, Advanced, General, Replace Original Shaders)
		logger::info("Loading core settings");
		LoadFromJson(settings);
		// Ensure 'Disable at Boot' section exists in the JSON
		if (!settings.contains("Disable at Boot") || !settings["Disable at Boot"].is_object()) {
			// Initialize to an empty object if it doesn't exist
			settings["Disable at Boot"] = json::object();
		}

		json& disabledFeaturesJson = settings["Disable at Boot"];
		logger::info("Loading 'Disable at Boot' settings");

		// Load runs again on config switches; stale entries would otherwise outlive the config that set them.
		ClearDisabledFeatures();
		for (auto& [featureName, featureStatus] : disabledFeaturesJson.items()) {
			if (featureStatus.is_boolean()) {
				disabledFeatures[featureName] = featureStatus.get<bool>();
			} else {
				logger::warn("Invalid entry for feature '{}' in 'Disable at Boot', expected boolean.", featureName);
			}
		}
		for (auto* feature : Feature::GetFeatureList()) {
			try {
				const std::string featureName = feature->GetShortName();
				// Resolved here rather than in Feature::Load so features disabled at boot,
				// which never reach Load, still report their install state to the UI.
				std::error_code ec;
				const bool iniExists = std::filesystem::exists(Util::PathHelpers::GetFeatureIniPath(featureName), ec);
				// exists() reports false on error, so treat an unreadable path as installed rather than letting a probe failure hide the feature.
				if (ec)
					logger::warn("Could not determine install state for feature '{}': {}", featureName, ec.message());
				feature->installed = ec || iniExists;
				if (feature->IsAlwaysEnabled()) {
					disabledFeatures.erase(featureName);
				} else if (!disabledFeatures.contains(featureName) && feature->IsDisabledByDefault()) {
					disabledFeatures[featureName] = true;
					logger::info("Feature '{}' is disabled by default", featureName);
				}
				bool isDisabled = !feature->IsAlwaysEnabled() && disabledFeatures.contains(featureName) && disabledFeatures[featureName];
				if (!isDisabled) {
					logger::info("Loading Feature: '{}'", featureName);

					// Settings arrive already layered: defaults, Baseline packs, then the user's edits
					feature->Load(settings);
				} else {
					logger::info("Feature '{}' is disabled at boot.", featureName);
				}
			} catch (const std::exception& e) {
				feature->failedLoadedMessage = feature->failedLoadedMessage.empty() ?
				                                   (feature->GetName() + " failed to load. Check CommunityShaders.log") :
				                                   (feature->failedLoadedMessage + "\n" + feature->GetName() + " failed to load. Check CommunityShaders.log");
				logger::warn("Error loading setting for feature '{}': {}", feature->GetShortName(), e.what());
			}
		}

		if (settings["Version"].is_string() && settings["Version"].get<std::string>() != Plugin::VERSION.string()) {
			logger::info("Found older config for version {}; upgrading to {}", (std::string)settings["Version"], Plugin::VERSION.string());
			Save();
		}

		FeatureIssues::ScanForOrphanedFeatureINIs();

		MarkSettingsSaved();
		logger::info("Loading Settings Complete");
	} catch (const json::exception& e) {
		logger::info("General JSON error accessing settings: {}; recreating config", e.what());
		Save();
		errorDetected = true;
	} catch (const std::exception& e) {
		logger::info("General error accessing settings: {}; recreating config", e.what());
		Save();
		errorDetected = true;
	}
	if (errorDetected && a_allowReload)
		Load(false);
}

void State::SaveToJson(nlohmann::json& settings)
{
	// Guard outlives the lock so its resolve-on-destruction does not run under m_mutex.
	SceneSettingsManager::SceneLayerGuard sceneLayerGuard;
	std::lock_guard<std::mutex> lock(m_mutex);
	SaveCoreToJson(settings);

	auto& upscaling = globals::features::upscaling;
	auto& upscalingJson = settings[upscaling.GetShortName()];
	upscaling.SaveSettings(upscalingJson);

	for (auto* feature : Feature::GetFeatureList())
		feature->Save(settings);
}

void State::SaveCoreToJson(json& settings)
{
	const auto shaderCache = globals::shaderCache;

	globals::menu->Save(settings["Menu"]);

	json advanced;
	advanced["Dump Shaders"] = shaderCache->IsDump();
	advanced["Log Level"] = logLevel;
	advanced["Developer Mode"] = enableDeveloperMode;
	advanced["Shader Defines"] = GetDefinesString();
	advanced["Compiler Threads"] = shaderCache->compilationThreadCount;
	advanced["Background Compiler Threads"] = shaderCache->backgroundCompilationThreadCount;
	advanced["Use FileWatcher"] = shaderCache->UseFileWatcher();
	advanced["Frame Annotations"] = frameAnnotations;
	advanced["Partial Precision"] = enablePartialPrecision.load(std::memory_order_relaxed);
	advanced["Content Store"] = enableContentStore.load(std::memory_order_relaxed);
	advanced["Content Store Max MB"] = contentStoreMaxMB.load(std::memory_order_relaxed);
	settings["Advanced"] = advanced;

	json general;
	general["Enable Shaders"] = shaderCache->IsEnabled();
	general["Enable Disk Cache"] = shaderCache->IsDiskCache();
	general["Skip Unchanged Shaders"] = shaderCache->IsSkipUnchangedShaders();
	general["Enable Async"] = shaderCache->IsAsync();
	general["Language"] = I18n::GetSingleton()->GetCurrentLocale();
	PostProcessingMode::Save(general);

	settings["General"] = general;

	json originalShaders;
	ForEachShaderTypeWithIndex([&](auto type, int classIndex) {
		originalShaders[magic_enum::enum_name(type)] = enabledClasses[classIndex];
	});
	settings["Replace Original Shaders"] = originalShaders;

	json disabledFeaturesJson;
	for (const auto& [featureName, isDisabled] : disabledFeatures) {
		disabledFeaturesJson[featureName] = isDisabled;
	}
	settings["Disable at Boot"] = disabledFeaturesJson;
	settings["Favorites"] = favoriteFeatures;

	settings["Version"] = Plugin::VERSION.string();
}

size_t State::HashCoreSettings()
{
	json core;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		SaveCoreToJson(core);
	}
	return std::hash<std::string>{}(core.dump());
}

void State::MarkSettingsSaved()
{
	settingsChanged = false;
	coreSettingsChanged = false;
	savedCoreHash = HashCoreSettings();
}

void State::CheckFeatureSettingsChanged(Feature* feature)
{
	if (settingsChanged || !feature->loaded || !feature->UsesMainSettings())
		return;
	const auto name = feature->GetName();
	const auto base = featureBases.find(name);
	const auto edits = userFeatureEdits.find(name);
	json saved = base != featureBases.end() ? base->second : json::object();
	if (edits != userFeatureEdits.end())
		Util::Settings::MergeSettings(saved, edits->second);
	SceneSettingsManager::SceneLayerGuard sceneLayerGuard;
	json current;
	feature->SaveSettings(current);
	if (!DiffFromBase(current, saved).empty())
		settingsChanged = true;
}

void State::CheckCoreSettingsChanged()
{
	coreSettingsChanged = HashCoreSettings() != savedCoreHash;
}

void State::LoadFromJson(nlohmann::json& settings)
{
	// Guard outlives the lock so its resolve-on-destruction does not run under m_mutex.
	SceneSettingsManager::SceneLayerGuard sceneLayerGuard;
	std::lock_guard<std::mutex> lock(m_mutex);
	const auto shaderCache = globals::shaderCache;

	// Load Menu settings
	if (settings.contains("Menu") && settings["Menu"].is_object()) {
		globals::menu->Load(settings["Menu"]);
	}

	if (settings.contains("Advanced") && settings["Advanced"].is_object()) {
		json& advanced = settings["Advanced"];
		if (advanced.contains("Dump Shaders") && advanced["Dump Shaders"].is_boolean())
			shaderCache->SetDump(advanced["Dump Shaders"]);
		if (advanced.contains("Log Level") && advanced["Log Level"].is_number_integer())
			logLevel = magic_enum::enum_cast<spdlog::level::level_enum>(advanced["Log Level"].get<int>()).value_or(spdlog::level::info);
		if (advanced.contains("Developer Mode") && advanced["Developer Mode"].is_boolean())
			enableDeveloperMode = advanced["Developer Mode"];
		if (advanced.contains("Shader Defines") && advanced["Shader Defines"].is_string())
			SetDefines(advanced["Shader Defines"]);
		if (advanced.contains("Compiler Threads") && advanced["Compiler Threads"].is_number_integer())
			shaderCache->compilationThreadCount = std::clamp(advanced["Compiler Threads"].get<int32_t>(), 1, static_cast<int32_t>(std::thread::hardware_concurrency()));
		if (advanced.contains("Background Compiler Threads") && advanced["Background Compiler Threads"].is_number_integer())
			shaderCache->backgroundCompilationThreadCount = std::clamp(advanced["Background Compiler Threads"].get<int32_t>(), 1, static_cast<int32_t>(std::thread::hardware_concurrency()));
		if (advanced.contains("Use FileWatcher") && advanced["Use FileWatcher"].is_boolean())
			shaderCache->SetFileWatcher(advanced["Use FileWatcher"]);
		if (advanced.contains("Frame Annotations") && advanced["Frame Annotations"].is_boolean())
			frameAnnotations = advanced["Frame Annotations"];
		if (advanced.contains("Partial Precision") && advanced["Partial Precision"].is_boolean())
			enablePartialPrecision.store(advanced["Partial Precision"].get<bool>(), std::memory_order_relaxed);
		if (advanced.contains("Content Store") && advanced["Content Store"].is_boolean())
			enableContentStore.store(advanced["Content Store"].get<bool>(), std::memory_order_relaxed);
		if (advanced.contains("Content Store Max MB") && advanced["Content Store Max MB"].is_number_unsigned())
			contentStoreMaxMB.store(static_cast<uint32_t>(std::clamp<uint64_t>(advanced["Content Store Max MB"].get<uint64_t>(), kContentStoreMinMB, kContentStoreMaxMB)), std::memory_order_relaxed);
	}

	if (settings.contains("General") && settings["General"].is_object()) {
		json& general = settings["General"];
		if (general.contains("Enable Shaders") && general["Enable Shaders"].is_boolean())
			shaderCache->SetEnabled(general["Enable Shaders"]);
		if (general.contains("Enable Disk Cache") && general["Enable Disk Cache"].is_boolean())
			shaderCache->SetDiskCache(general["Enable Disk Cache"]);
		if (general.contains("Skip Unchanged Shaders") && general["Skip Unchanged Shaders"].is_boolean())
			shaderCache->SetSkipUnchangedShaders(general["Skip Unchanged Shaders"]);
		if (general.contains("Enable Async") && general["Enable Async"].is_boolean())
			shaderCache->SetAsync(general["Enable Async"]);
		PostProcessingMode::Load(general);

		// Load i18n locale preference
		if (general.contains("Language") && general["Language"].is_string()) {
			auto locale = general["Language"].get<std::string>();
			auto* i18n = I18n::GetSingleton();
			if (locale != i18n->GetCurrentLocale()) {
				i18n->SetLocale(locale);
			}
		} else {
			// No saved language preference — auto-detect from system locale on first launch
			auto* i18n = I18n::GetSingleton();
			auto detected = i18n->DetectSystemLocale();
			if (detected != "en" && detected != i18n->GetCurrentLocale()) {
				i18n->SetLocale(detected);
				logger::info("[I18n] Auto-detected system locale: '{}'", detected);
			}
		}
	}

	if (settings.contains("Replace Original Shaders") && settings["Replace Original Shaders"].is_object()) {
		json& originalShaders = settings["Replace Original Shaders"];
		ForEachShaderTypeWithIndex([&](auto type, int classIndex) {
			auto name = magic_enum::enum_name(type);
			if (originalShaders.contains(name) && originalShaders[name].is_boolean()) {
				enabledClasses[classIndex] = originalShaders[name];
			} else {
				logger::warn("Invalid entry for shader class '{}', using current value", name);
			}
		});
	}

	if (settings.contains("Favorites") && settings["Favorites"].is_object()) {
		favoriteFeatures.clear();
		for (auto& [featureName, isFavorite] : settings["Favorites"].items()) {
			if (isFavorite.is_boolean() && isFavorite.get<bool>())
				favoriteFeatures[featureName] = true;
		}
	}

	// Load feature settings (only for already-loaded features)
	for (auto* feature : Feature::GetFeatureList()) {
		if (feature->loaded) {
			feature->Load(settings);
		}
	}
}

void State::Save()
{
	const auto configPath = Util::PathHelpers::GetSettingsUserPath().string();

	// Serialize fully before touching the file: a throwing feature must not truncate the config.
	json settings;
	try {
		SaveToJson(settings);
		KeepOnlyUserEdits(settings);
		// The catalog writes its own section whenever it changes; this keeps it from being dropped here.
		const auto& catalog = UnifiedPresetCatalog::GetSingleton();
		settings[UnifiedPresetCatalog::kUserSettingsKey] = catalog.SerializeActiveState();
	} catch (const std::exception& e) {
		logger::error("Failed to serialize settings, leaving {} untouched. Error: {}", configPath, e.what());
		return;
	}

	if (Util::FileHelpers::WriteJsonAtomically(configPath, settings, 1, "config")) {
		logger::info("Saving settings to {}", configPath);
		MarkSettingsSaved();
	}
}

std::filesystem::path State::DumpFeatureDefaults() const
{
	json dump = json::object();
	for (auto* feature : Feature::GetFeatureList()) {
		if (!feature->UsesMainSettings())
			continue;
		if (const auto found = defaultSettings.find(feature->GetName()); found != defaultSettings.end())
			dump[feature->GetName()] = *found;
	}
	auto path = Util::PathHelpers::GetCommunityShaderPath() / "FeatureDefaults.json";
	if (!Util::FileHelpers::WriteJsonAtomically(path, dump, 2, "feature defaults"))
		return {};
	logger::info("Dumped feature defaults to {}", path.string());
	return path;
}

json State::BuildFeatureBase(Feature* feature)
{
	const auto found = defaultSettings.find(feature->GetName());
	json base = found != defaultSettings.end() && found->is_object() ? *found : json::object();
	BaselineManager::GetSingleton()->ApplyBaselines(feature->GetShortName(), base);
	return base;
}

json State::DiffFromFeatureBase(const std::string& featureName, const json& current) const
{
	static const json empty = json::object();
	const auto* base = GetFeatureBase(featureName);
	return DiffFromBase(current, base ? *base : empty);
}

void State::KeepOnlyUserEdits(json& settings)
{
	for (auto* feature : Feature::GetFeatureList()) {
		if (!feature->UsesMainSettings())
			continue;
		const auto name = feature->GetName();
		// A feature that never loaded holds defaults, not the user's values, so its stored edits carry over.
		if (feature->loaded) {
			const auto current = settings.find(name);
			userFeatureEdits[name] = current == settings.end() ? json::object() : DiffFromFeatureBase(name, *current);
		}
		const auto edits = userFeatureEdits.find(name);
		if (edits == userFeatureEdits.end() || edits->second.empty())
			settings.erase(name);
		else
			settings[name] = edits->second;
	}
}

void State::ReapplySettingsLayers()
{
	// Base values are read and rewritten, so the scene's values must be out of the way.
	SceneSettingsManager::SceneLayerGuard sceneLayerGuard;
	InvalidateEditsUnderBaselines();
	for (auto* feature : Feature::GetFeatureList()) {
		if (!feature->loaded || !feature->UsesMainSettings())
			continue;
		auto& base = featureBases[feature->GetName()];
		json newBase = BuildFeatureBase(feature);
		if (newBase == base)
			continue;

		json current;
		feature->SaveSettings(current);
		json layered = newBase;
		Util::Settings::MergeSettings(layered, DiffFromBase(current, base));
		if (LoadLayeredSettings(feature, layered, current))
			base = std::move(newBase);
	}
}

std::vector<Feature*> State::FindEditsUnderPack(const std::string& packId)
{
	std::vector<Feature*> features;
	SceneSettingsManager::SceneLayerGuard sceneLayerGuard;
	for (auto* feature : Feature::GetFeatureList()) {
		if (!feature->loaded || !feature->UsesMainSettings())
			continue;
		const auto packData = GetPackFeatureData(packId, feature->GetShortName());
		if (packData.empty())
			continue;
		json current;
		feature->SaveSettings(current);
		if (SharesSettings(DiffFromFeatureBase(feature->GetName(), current), packData))
			features.push_back(feature);
	}
	return features;
}

void State::DropEditsUnder(Feature* feature, const json& packData)
{
	const auto& base = featureBases[feature->GetName()];
	json current;
	feature->SaveSettings(current);
	auto edits = DiffFromBase(current, base);
	RemoveSharedSettings(edits, packData);
	json layered = base;
	Util::Settings::MergeSettings(layered, edits);
	LoadLayeredSettings(feature, layered, current);
}

void State::ClearEditsUnderPack(const std::string& packId)
{
	SceneSettingsManager::SceneLayerGuard sceneLayerGuard;
	for (auto* feature : FindEditsUnderPack(packId))
		DropEditsUnder(feature, GetPackFeatureData(packId, feature->GetShortName()));
	MarkSettingsChanged();
	InvalidateEditsUnderBaselines();
}

bool State::HasEditsUnderBaselines(Feature* feature)
{
	const auto name = feature->GetName();
	if (const auto found = editsUnderBaselines.find(name); found != editsUnderBaselines.end())
		return found->second;
	bool shared = false;
	if (feature->loaded && feature->UsesMainSettings()) {
		const auto packData = GetPackFeatureData({}, feature->GetShortName());
		if (!packData.empty()) {
			SceneSettingsManager::SceneLayerGuard sceneLayerGuard;
			json current;
			feature->SaveSettings(current);
			shared = SharesSettings(DiffFromFeatureBase(name, current), packData);
		}
	}
	editsUnderBaselines[name] = shared;
	return shared;
}

void State::InvalidateEditsUnderBaselines(Feature* feature)
{
	if (feature)
		editsUnderBaselines.erase(feature->GetName());
	else
		editsUnderBaselines.clear();
}

void State::ClearFeatureEditsUnderBaselines(Feature* feature)
{
	{
		SceneSettingsManager::SceneLayerGuard sceneLayerGuard;
		DropEditsUnder(feature, GetPackFeatureData({}, feature->GetShortName()));
	}
	MarkSettingsChanged();
	InvalidateEditsUnderBaselines(feature);
}

void State::RestoreFeatureLayers(Feature* feature)
{
	MarkSettingsChanged();
	InvalidateEditsUnderBaselines(feature);
	SceneSettingsManager::SceneLayerGuard sceneLayerGuard;
	// Defaults first, so settings a partial base leaves out still reset.
	feature->RestoreDefaultSettings();
	const auto base = featureBases.find(feature->GetName());
	if (base == featureBases.end() || base->second.empty())
		return;
	json values = base->second;
	try {
		feature->LoadSettings(values);
	} catch (const std::exception& e) {
		logger::warn("Could not restore {} to its Baseline values, using defaults: {}", feature->GetName(), e.what());
		feature->RestoreDefaultSettings();
	}
}

bool State::ValidateCache(CSimpleIniA& a_ini)
{
	bool valid = true;
	for (auto* feature : Feature::GetFeatureList())
		valid = valid && feature->ValidateCache(a_ini);
	return valid;
}

void State::WriteDiskCacheInfo(CSimpleIniA& a_ini)
{
	for (auto* feature : Feature::GetFeatureList())
		feature->WriteDiskCacheInfo(a_ini);
}

void State::SetLogLevel(spdlog::level::level_enum a_level)
{
	logLevel = a_level;
	spdlog::set_level(logLevel);
	spdlog::flush_on(logLevel);
	logger::info("Log Level set to {} ({})", magic_enum::enum_name(logLevel), magic_enum::enum_integer(logLevel));
}

spdlog::level::level_enum State::GetLogLevel()
{
	return logLevel;
}

void State::SetDefines(std::string a_defines)
{
	std::vector<std::pair<std::string, std::string>> parsedDefines;
	std::string definesString;
	auto defines = pystring::split(a_defines, ";");
	for (const auto& define : defines) {
		auto cleanedDefine = pystring::strip(define);
		auto token = pystring::split(cleanedDefine, "=");
		if (token.empty() || token[0].empty())
			continue;
		if (token.size() > 2) {
			logger::warn("Define string has too many '='; ignoring {}", define);
			continue;
		}
		std::string name = pystring::strip(token[0]);
		std::string definition = token.size() == 2 ? pystring::strip(token[1]) : std::string{};
		definesString += pystring::strip(define) + ";";
		parsedDefines.emplace_back(std::move(name), std::move(definition));
	}
	definesString = definesString.substr(0, definesString.size() - 1);
	logger::debug("Shader Defines set to {}", definesString);
	{
		std::scoped_lock lock(shaderDefinesMutex);
		shaderDefinesString = std::move(definesString);
		shaderDefines = std::make_shared<const std::vector<std::pair<std::string, std::string>>>(std::move(parsedDefines));
	}
}

std::string State::GetDefinesString()
{
	std::scoped_lock lock(shaderDefinesMutex);
	return shaderDefinesString;
}

std::shared_ptr<const std::vector<std::pair<std::string, std::string>>> State::GetDefines()
{
	std::scoped_lock lock(shaderDefinesMutex);
	return shaderDefines;
}

bool State::ShaderEnabled(const RE::BSShader::Type a_type)
{
	return a_type > RE::BSShader::Type::None && a_type < RE::BSShader::Type::Total &&
	       enabledClasses[static_cast<size_t>(a_type) - 1];
}

bool State::IsShaderEnabled(const RE::BSShader& a_shader)
{
	return ShaderEnabled(a_shader.shaderType.get());
}

bool State::IsDeveloperMode()
{
	return enableDeveloperMode || GetLogLevel() <= spdlog::level::debug;
}

void State::ModifyRenderTarget(RE::RENDER_TARGETS::RENDER_TARGET a_target, RE::BSGraphics::RenderTargetProperties& a_properties)
{
	a_properties.supportUnorderedAccess = true;
	logger::debug("Adding UAV access to {}", magic_enum::enum_name(a_target));
}

void State::CheckTypedUAVLoadSupport()
{
	auto device = globals::d3d::device;
	if (!device) {
		logger::warn("[TypedUAVLoad] Device unavailable; skipping format support probe.");
		return;
	}

	// Formats this codebase does typed UAV loads on (RWTexture<T> read via subscript).
	// Identified by static analysis; keep in sync with new typed reads.
	// All require the optional D3D11 feature D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD —
	// guaranteed only for R32_FLOAT/R32_UINT/R32_SINT, otherwise gated by
	// D3D11_FEATURE_DATA_D3D11_OPTIONS2.TypedUAVLoadAdditionalFormats (FL12+).
	struct FormatEntry
	{
		DXGI_FORMAT format;
		const char* name;
		const char* usage;
	};
	static const FormatEntry kFormats[] = {
		{ DXGI_FORMAT_R11G11B10_FLOAT, "R11G11B10_FLOAT", "Dynamic Cubemaps (envCapture/Raw/Position) — non-HDR" },
		{ DXGI_FORMAT_R16G16B16A16_FLOAT, "R16G16B16A16_FLOAT", "Dynamic Cubemaps (HDR), Skylighting outProbeArray" },
		{ DXGI_FORMAT_R16G16B16A16_UNORM, "R16G16B16A16_UNORM", "Grass Collision (collisionTexture)" },
		{ DXGI_FORMAT_R16G16_UNORM, "R16G16_UNORM", "Terrain Shadows (RWTexShadowHeights)" },
		{ DXGI_FORMAT_R8G8B8A8_UNORM, "R8G8B8A8_UNORM", "HDR Display UI brightness (uiTexture)" },
		{ DXGI_FORMAT_R8_UINT, "R8_UINT", "Skylighting accumulation frames (outAccumFramesArray)" },
		{ DXGI_FORMAT_R16_FLOAT, "R16_FLOAT", "Vanilla volumetric lighting density (DensityRW)" },
	};

	bool anyUnsupported = false;
	logger::info("[TypedUAVLoad] Probing per-format UAV typed-load support:");
	for (const auto& entry : kFormats) {
		D3D11_FEATURE_DATA_FORMAT_SUPPORT2 support2{};
		support2.InFormat = entry.format;
		HRESULT hr = device->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2, &support2, sizeof(support2));
		if (FAILED(hr)) {
			logger::warn("[TypedUAVLoad] {} ({}): CheckFeatureSupport failed (hr=0x{:08x})", entry.name, entry.usage, static_cast<uint32_t>(hr));
			anyUnsupported = true;
			continue;
		}
		const bool supported = (support2.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD) != 0;
		if (supported) {
			logger::info("[TypedUAVLoad] {} — supported ({})", entry.name, entry.usage);
		} else {
			logger::warn("[TypedUAVLoad] {} — UNSUPPORTED ({})", entry.name, entry.usage);
			anyUnsupported = true;
		}
	}

	if (anyUnsupported) {
		logger::warn(
			"[TypedUAVLoad] One or more required formats lack typed-UAV-load support on this GPU. "
			"Affected features will read undefined data and may produce visual artifacts. "
			"Consider disabling: Dynamic Cubemaps, Grass Collision, Terrain Shadows, Skylighting, HDR Display.");
	}
}

void State::SetupResources()
{
	for (auto& c : drawCalls)
		c = 0;
	for (auto& c : smoothDrawCalls)
		c = 0;
	for (auto& ft : frameTimePerType)
		ft = 0.0f;
	for (auto& sft : smoothFrameTimePerType)
		sft = 0.0f;

	frameTimingActive = false;

	auto renderer = globals::game::renderer;

	permutationCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<PermutationCB>());
	sharedDataCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<SharedDataCB>());

	auto [data, size] = GetFeatureBufferData(false);
	(void)data;
	featureDataCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc((uint32_t)size));

	// Grab main texture to get resolution
	D3D11_TEXTURE2D_DESC texDesc{};
	renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].texture->GetDesc(&texDesc);

	globals::d3d::context->QueryInterface(__uuidof(pPerf), reinterpret_cast<void**>(&pPerf));

	featureLevel = globals::d3d::device->GetFeatureLevel();

	tracyCtx = TracyD3D11Context(globals::d3d::device, globals::d3d::context);
#ifdef TRACY_ENABLE
	Feature::SetTracyCtx(tracyCtx);
#endif

	globals::profiler->Initialize(globals::d3d::device, globals::d3d::context);

	// Checked per pass rather than bound once, so annotations forced on at runtime (RenderDoc capture, Nsight Perf)
	// also cover the profiler's passes. Toggles happen between frames, so a pass's begin and end agree.
	globals::profiler->SetPerfEventCallbacks(
		[this](std::string_view name) {
			if (frameAnnotations)
				BeginPerfEvent(name);
		},
		[this](std::string_view) {
			if (frameAnnotations)
				EndPerfEvent();
		});
}

void State::ModifyShaderLookup(const RE::BSShader& a_shader, uint& a_vertexDescriptor, uint& a_pixelDescriptor, bool a_forceDeferred)
{
	auto deferred = globals::deferred;

	if (a_shader.shaderType.get() != RE::BSShader::Type::Utility && a_shader.shaderType.get() != RE::BSShader::Type::ImageSpace) {
		switch (a_shader.shaderType.get()) {
		case RE::BSShader::Type::Lighting:
			{
				a_vertexDescriptor &= ~((uint32_t)SIE::ShaderCache::LightingShaderFlags::AdditionalAlphaMask |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::AmbientSpecular |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::DoAlphaTest |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::ShadowDir |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::DefShadow |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::CharacterLight |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::RimLighting |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::SoftLighting |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::BackLighting |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::Specular |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::AnisoLighting |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::BaseObjectIsSnow |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::Snow |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::TruePbr |
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::LandscapeSeams);

				a_pixelDescriptor &= ~((uint32_t)SIE::ShaderCache::LightingShaderFlags::AmbientSpecular |
									   (uint32_t)SIE::ShaderCache::LightingShaderFlags::ShadowDir |
									   (uint32_t)SIE::ShaderCache::LightingShaderFlags::DefShadow |
									   (uint32_t)SIE::ShaderCache::LightingShaderFlags::CharacterLight |
									   (uint32_t)SIE::ShaderCache::LightingShaderFlags::BaseObjectIsSnow);

				{
					uint32_t technique = 0x3F & (a_pixelDescriptor >> 24);
					if (technique != (uint32_t)SIE::ShaderCache::LightingShaderTechniques::LODLand &&
						technique != (uint32_t)SIE::ShaderCache::LightingShaderTechniques::LODLandNoise &&
						technique != (uint32_t)SIE::ShaderCache::LightingShaderTechniques::LODObjects &&
						technique != (uint32_t)SIE::ShaderCache::LightingShaderTechniques::LODObjectHD)
						a_pixelDescriptor &= ~((uint32_t)SIE::ShaderCache::LightingShaderFlags::Specular |
											   (uint32_t)SIE::ShaderCache::LightingShaderFlags::SoftLighting |
											   (uint32_t)SIE::ShaderCache::LightingShaderFlags::RimLighting |
											   (uint32_t)SIE::ShaderCache::LightingShaderFlags::BackLighting);
				}

				if (a_pixelDescriptor & (uint32_t)SIE::ShaderCache::LightingShaderFlags::AdditionalAlphaMask) {
					a_pixelDescriptor |= (uint32_t)SIE::ShaderCache::LightingShaderFlags::DoAlphaTest;
					a_pixelDescriptor &= ~(uint32_t)SIE::ShaderCache::LightingShaderFlags::AdditionalAlphaMask;
				}

				a_pixelDescriptor &= ~((uint32_t)SIE::ShaderCache::LightingShaderFlags::Snow);

				if (deferred->deferredPass || a_forceDeferred)
					a_pixelDescriptor |= (uint32_t)SIE::ShaderCache::LightingShaderFlags::Deferred;

				{
					uint32_t technique = 0x3F & (a_vertexDescriptor >> 24);
					if (technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::Glowmap ||
						technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::Parallax ||
						technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::Facegen ||
						technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::FacegenRGBTint ||
						technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::LODObjects ||
						technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::LODObjectHD ||
						technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::MultiIndexSparkle ||
						technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::Hair)
						a_vertexDescriptor &= ~(0x3F << 24);
				}

				{
					uint32_t technique = 0x3F & (a_pixelDescriptor >> 24);
					if (technique == (uint32_t)SIE::ShaderCache::LightingShaderTechniques::Glowmap)
						a_pixelDescriptor &= ~(0x3F << 24);
				}
			}
			break;
		case RE::BSShader::Type::Water:
			{
				auto flags = ~((uint32_t)SIE::ShaderCache::WaterShaderFlags::Reflections |
							   (uint32_t)SIE::ShaderCache::WaterShaderFlags::Cubemap |
							   (uint32_t)SIE::ShaderCache::WaterShaderFlags::Interior);
				a_vertexDescriptor &= flags;
				a_pixelDescriptor &= flags;
			}
			break;
		case RE::BSShader::Type::Effect:
			{
				auto flags = ~((uint32_t)SIE::ShaderCache::EffectShaderFlags::GrayscaleToColor |
							   (uint32_t)SIE::ShaderCache::EffectShaderFlags::GrayscaleToAlpha |
							   (uint32_t)SIE::ShaderCache::EffectShaderFlags::IgnoreTexAlpha);
				a_vertexDescriptor &= flags;
				a_pixelDescriptor &= flags;

				if (deferred->deferredPass || a_forceDeferred)
					a_pixelDescriptor |= (uint32_t)SIE::ShaderCache::EffectShaderFlags::Deferred;
			}
			break;
		case RE::BSShader::Type::DistantTree:
			{
				if (deferred->deferredPass || a_forceDeferred)
					a_pixelDescriptor |= (uint32_t)SIE::ShaderCache::DistantTreeShaderFlags::Deferred;
			}
			break;
		case RE::BSShader::Type::Sky:
			{
				if (deferred->deferredPass || a_forceDeferred)
					a_pixelDescriptor |= 256;
			}
			break;
		}
	} else if (a_shader.shaderType.get() == RE::BSShader::Type::Utility) {
		a_vertexDescriptor &= ~(uint32_t)SIE::ShaderCache::UtilityShaderFlags::TruePbr;
	}
}

uint32_t State::GetRuntimePixelBits(const RE::BSShader& a_shader)
{
	// Water's pixel stage branches on Wading at runtime; losing the bit skips the wading displacement without any error.
	return a_shader.shaderType.get() == RE::BSShader::Type::Water ? static_cast<uint32_t>(SIE::ShaderCache::WaterShaderFlags::Wading) : 0u;
}

void State::BeginPerfEvent(std::string_view title)
{
#ifdef TRACY_ENABLE
	// Use dynamic source location so Tracy displays the title as the zone name
	// rather than the static function name "BeginPerfEvent".
	const auto srcloc = ___tracy_alloc_srcloc_name(
		static_cast<uint32_t>(__LINE__),
		__FILE__, sizeof(__FILE__) - 1,
		__func__, sizeof(__func__) - 1,
		title.data(), title.size(),
		0);
	const TracyCZoneCtx ctx = ___tracy_emit_zone_begin_alloc(srcloc, true);
	s_tracyPerfZones.push_back(ctx);
#endif
	pPerf->BeginEvent(std::wstring(title.begin(), title.end()).c_str());
	if (NvPerf::RangesActive())
		NvPerf::PushRange(title);
}

void State::EndPerfEvent()
{
#ifdef TRACY_ENABLE
	if (!s_tracyPerfZones.empty()) {
		TracyCZoneEnd(s_tracyPerfZones.back());
		s_tracyPerfZones.pop_back();
	} else {
		logger::warn("EndPerfEvent called without a matching BeginPerfEvent");
	}
#endif
	if (NvPerf::RangesActive())
		NvPerf::PopRange();
	pPerf->EndEvent();
}

void State::SetPerfMarker(std::string_view title)
{
	pPerf->SetMarker(std::wstring(title.begin(), title.end()).c_str());
}

void State::SetAdapterDescription(const std::wstring& description)
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
	adapterDescription = converter.to_bytes(description);
}

void State::UpdateSharedData([[maybe_unused]] bool a_inWorld, [[maybe_unused]] bool a_prepass)
{
	{
		SharedDataCB data{};
		SharedLighting lighting{};

		const auto shaderManager = globals::game::smState;
		const RE::NiTransform& dalcTransform = shaderManager->directionalAmbientTransform;

		auto shadowSceneNode = shaderManager->shadowSceneNode[0];
		auto dirLight = skyrim_cast<RE::NiDirectionalLight*>(shadowSceneNode->GetRuntimeData().sunLight->light.get());

		auto& lightRuntimeData = dirLight->GetLightRuntimeData();
		lighting.directionalLight = dirLight;
		lighting.directional.color = lightRuntimeData.diffuse;
		lighting.directional.intensity = lightRuntimeData.fade;
		if (auto imageSpaceManager = globals::game::imageSpaceManager)
			lighting.directional.intensity *= imageSpaceManager->GetRuntimeData().data.baseData.hdr.sunlightScale;
		lighting.directional.alpha = lighting.directional.intensity;

		const auto& direction = dirLight->GetWorldDirection();
		data.DirLightDirection = { -direction.x, -direction.y, -direction.z, 0.0f };
		data.DirLightDirection.Normalize();

		data.CameraData = Util::GetCameraData();
		data.BufferDim = { (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight, 1.0f / (float)globals::game::graphicsState->screenWidth, 1.0f / (float)globals::game::graphicsState->screenHeight };
		data.Timer = timer;

		auto temporal = Util::GetTemporal();

		data.FrameCount = frameCount * temporal;
		data.FrameCountAlwaysActive = frameCount;
		data.ResetHistory = ShouldResetHistory();

		if (a_inWorld) {
			for (int i = -2; i <= 2; i++) {
				for (int k = -2; k <= 2; k++) {
					int waterTile = (i + 2) + ((k + 2) * 5);
					data.WaterData[waterTile] = Util::TryGetWaterData((float)i * 4096.0f, (float)k * 4096.0f);
				}
			}
		}

		data.WaterSystemHeight = -RE::NI_INFINITY;

		data.InInterior = Util::IsInterior();
		data.HasDirectionalShadows = HasDirectionalShadows();

		if (globals::game::sky)
			data.HideSky = globals::game::sky->flags.any(RE::Sky::Flags::kHideSky);
		else
			data.HideSky = false;

		data.InMapMenu = isMapMenuOpen;

		auto& upscaling = globals::features::upscaling;

		if (upscaling.loaded) {
			auto upscaleMethod = upscaling.GetUpscaleMethod();
			if (temporal && upscaleMethod != Upscaling::UpscaleMethod::kTAA) {
				float2 screenSz{ (float)globals::game::graphicsState->screenWidth, (float)globals::game::graphicsState->screenHeight };
				auto renderSize = Util::ConvertToDynamic(screenSz, true);
				data.MipBias = std::log2f(renderSize.x / screenSz.x);
				if (upscaleMethod == Upscaling::UpscaleMethod::kDLSS)
					data.MipBias -= 1.0f;
			} else {
				data.MipBias = 0;
			}
		} else {
			data.MipBias = 0;
		}

		if (auto sky = globals::game::sky) {
			const auto& skySync = globals::features::skySync;
			auto getMoonColor = [&](const RE::Moon* moon, const Util::ColorSpace::LightColor& baseColor) {
				return Util::Moon::GetLightColor(moon, baseColor, globals::features::skySync.settings.NewMoonIntensity, globals::features::skySync.settings.CrescentMoonIntensity, globals::features::skySync.settings.FullMoonIntensity);
			};

			// Process sun
			if (auto sun = sky->sun; sun && sun->root && sky->root) {
				const auto sunDirection = skySync.GetCelestialDirection(sky, SkySync::Caster::Sun);
				data.SunDirection = { sunDirection.x, sunDirection.y, sunDirection.z, 0.0f };

				if (sun->sunBase) {
					if (const auto prop = skyrim_cast<RE::BSSkyShaderProperty*>(sun->sunBase->GetGeometryRuntimeData().shaderProperty.get())) {
						const auto& sunColor = prop->kBlendColor;
						lighting.sun = { .color = { sunColor.red, sunColor.green, sunColor.blue }, .intensity = sunColor.alpha, .alpha = sunColor.alpha };
					}
				}
			}

			if (auto masser = sky->masser) {
				auto dir = skySync.GetCelestialDirection(sky, SkySync::Caster::Masser);
				data.MasserDirection = { dir.x, dir.y, dir.z, 0.0f };
				if (masser->root && !masser->root->GetFlags().any(RE::NiAVObject::Flag::kHidden))
					lighting.masser = getMoonColor(masser, Util::Moon::MasserBaseColor);
			}

			if (auto secunda = sky->secunda) {
				auto dir = skySync.GetCelestialDirection(sky, SkySync::Caster::Secunda);
				data.SecundaDirection = { dir.x, dir.y, dir.z, 0.0f };
				if (secunda->root && !secunda->root->GetFlags().any(RE::NiAVObject::Flag::kHidden))
					lighting.secunda = getMoonColor(secunda, Util::Moon::SecundaBaseColor);
			}
		}

		// DALC to SH
		const auto& m = dalcTransform.rotate;
		const auto& t = dalcTransform.translate;
		float3 dalcColors[6];
		dalcColors[0] = float3{ m.entry[0][0] + t.x, m.entry[1][0] + t.y, m.entry[2][0] + t.z };     // +X
		dalcColors[1] = float3{ -m.entry[0][0] + t.x, -m.entry[1][0] + t.y, -m.entry[2][0] + t.z };  // -X
		dalcColors[2] = float3{ m.entry[0][1] + t.x, m.entry[1][1] + t.y, m.entry[2][1] + t.z };     // +Y
		dalcColors[3] = float3{ -m.entry[0][1] + t.x, -m.entry[1][1] + t.y, -m.entry[2][1] + t.z };  // -Y
		dalcColors[4] = float3{ m.entry[0][2] + t.x, m.entry[1][2] + t.y, m.entry[2][2] + t.z };     // +Z
		dalcColors[5] = float3{ -m.entry[0][2] + t.x, -m.entry[1][2] + t.y, -m.entry[2][2] + t.z };  // -Z

		SphericalHarmonics::SH2Color dalcSH = SphericalHarmonics::DALCToSH(dalcColors);
		data.AmbientSHR = { dalcSH.r.c0, dalcSH.r.c1[0], dalcSH.r.c1[1], dalcSH.r.c1[2] };
		data.AmbientSHG = { dalcSH.g.c0, dalcSH.g.c1[0], dalcSH.g.c1[1], dalcSH.g.c1[2] };
		data.AmbientSHB = { dalcSH.b.c0, dalcSH.b.c1[0], dalcSH.b.c1[1], dalcSH.b.c1[2] };

		data.HDRData = globals::features::hdrDisplay.GetSharedDataHDR();

		for (auto* feature : Feature::GetFeatureList())
			if (feature->loaded)
				feature->ModifySharedLighting(lighting);
		data.DirLightColor = lighting.directional.GetColor();
		data.SunColor = lighting.sun.GetColor();
		data.MasserColor = lighting.masser.GetColor();
		data.SecundaColor = lighting.secunda.GetColor();
		data.TonemapOwner = static_cast<uint>(GetTonemapOwner());
		sharedDataCB->Update(data);
	}

	{
		auto [data, size] = GetFeatureBufferData(a_inWorld);

		featureDataCB->Update(data, size);
	}

	auto* srv = Util::GetCurrentSceneDepthSRV(true);
	globals::d3d::context->PSSetShaderResources(17, 1, &srv);
}

void State::ClearDisabledFeatures()
{
	disabledFeatures.clear();
}

/** @brief Whether a feature can never be disabled at boot. GetShortName() allocates, so the set is built once. */
static bool IsAlwaysEnabledFeature(const std::string& featureName)
{
	static const auto alwaysEnabled = [] {
		std::unordered_set<std::string> names;
		for (auto* feature : Feature::GetFeatureList())
			if (feature->IsAlwaysEnabled())
				names.insert(feature->GetShortName());
		return names;
	}();
	return alwaysEnabled.contains(featureName);
}

bool State::SetFeatureDisabled(const std::string& featureName, bool isDisabled)
{
	if (IsAlwaysEnabledFeature(featureName)) {
		disabledFeatures.erase(featureName);
		return false;
	}
	bool wasPreviouslyDisabled = disabledFeatures.count(featureName) > 0 ? disabledFeatures[featureName] : false;  // Properly check if it exists
	disabledFeatures[featureName] = isDisabled;

	// Log the change
	if (wasPreviouslyDisabled != isDisabled) {
		logger::info("Set feature '{}' to: {}", featureName, isDisabled ? "Disabled" : "Enabled");
	} else {
		logger::info("Feature '{}' state remains: {}", featureName, isDisabled ? "Disabled" : "Enabled");
	}

	return disabledFeatures[featureName];  // Return the current state instead of the input parameter
}

bool State::IsFeatureDisabled(const std::string& featureName)
{
	if (IsAlwaysEnabledFeature(featureName))
		return false;
	const auto found = disabledFeatures.find(featureName);
	return found != disabledFeatures.end() && found->second;
}

std::unordered_map<std::string, bool>& State::GetDisabledFeatures()
{
	return disabledFeatures;
}

void State::SetFeatureFavorite(const std::string& featureName, bool isFavorite)
{
	if (isFavorite)
		favoriteFeatures[featureName] = true;
	else
		favoriteFeatures.erase(featureName);  // only favorites are stored
}

bool State::IsFeatureFavorite(const std::string& featureName)
{
	const auto found = favoriteFeatures.find(featureName);
	return found != favoriteFeatures.end() && found->second;
}

// --- Utility Method Implementations ---

float State::GetTotalSmoothedDrawCalls() const
{
	return static_cast<float>(smoothDrawCalls[magic_enum::enum_integer(RE::BSShader::Type::Total)]);
}

void State::LoadTheme()
{
	// Load the active preset from SettingsUser.json (already read during State::Load)
	auto presetName = globals::menu->GetSettings().SelectedThemePreset;
	if (presetName.empty()) {
		logger::info("No active theme preset set; skipping preset load");
		return;
	}

	// Ensure default themes exist and theme manager has discovered themes
	globals::menu->CreateDefaultThemes();
	auto themeManager = ThemeManager::GetSingleton();
	if (themeManager && !themeManager->IsDiscovered()) {
		themeManager->DiscoverThemes();
	}

	logger::info("Loading active theme preset: '{}'", presetName);
	if (!globals::menu->LoadThemePreset(presetName)) {
		logger::warn("Failed to load preset '{}', attempting to fall back to 'Default'", presetName);
		if (globals::menu->LoadThemePreset("Default")) {
			globals::menu->GetSettings().SelectedThemePreset = "Default";
			logger::info("Fallback to 'Default' theme succeeded");
		} else {
			logger::warn("Fallback to 'Default' theme failed");
		}
	}
}

bool State::HasDirectionalShadows() const
{
	return !Util::IsInterior() || globals::features::interiorSun.IsActiveInteriorSun();
}
