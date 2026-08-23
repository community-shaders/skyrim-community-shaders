#include "State.h"

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <codecvt>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <RE/B/BGSSaveLoadGame.h>
#include <pystring/pystring.h>

#include "Deferred.h"
#include "FeatureIssues.h"
#include "Features/AdaptiveBrightness.h"
#include "Features/CSEditor.h"
#include "Features/CSUtility.h"
#include "Features/CloudShadows.h"
#include "Features/DynamicCubemaps.h"
#include "Features/FoveatedCommon.h"
#include "Features/InteriorSun.h"
#include "Features/LightLimitFix.h"
#include "Features/PerformanceOverlay.h"
#include "Features/TerrainBlending.h"
#include "Features/TerrainHelper.h"
#include "Features/Upscaling.h"
#include "Features/VR.h"
#include "Features/VolumetricShadows.h"
#include "Features/WaterEffects.h"
#include "Features/WeatherPicker.h"
#include "Features/WetnessEffects.h"
#include "Features/Wetterness.h"
#include "Menu.h"
#include "Profiler.h"
#include "SceneSettingsManager.h"
#include "SettingsMigrations.h"
#include "SettingsOverrideManager.h"
#include "SettingsSerialization.h"
#include "ShaderCache.h"
#include "TruePBR.h"
#include "Utils/FileSystem.h"
#include "Utils/SphericalHarmonics.h"
#include "WeatherManager.h"
#include "WeatherVariableRegistry.h"

#ifdef TRACY_ENABLE
static thread_local std::vector<TracyCZoneCtx> s_tracyPerfZones;
#endif

namespace
{
	static constexpr std::array<std::string_view, 0> kForcedDisableAtBootFeatures{};

	std::vector<std::string> SaveUserOverrides(const json& a_settings)
	{
		std::vector<std::string> failedLayers;
		auto overrideManager = SettingsOverrideManager::GetSingleton();
		if (!overrideManager->IsEnabled())
			return failedLayers;

		for (auto* feature : Feature::GetFeatureList()) {
			std::string featureName = "<unknown>";
			try {
				featureName = feature->GetShortName();
				if (!feature->loaded || !overrideManager->HasFeatureOverrides(featureName))
					continue;

				json currentSettings;
				const auto activeVariables =
					WeatherManager::GetSingleton()->GetActiveVariablesForFeature(featureName);
				WeatherVariables::GlobalWeatherRegistry::GetSingleton()
					->SerializeFeatureUserSettings(featureName, activeVariables, [&]() {
						feature->SaveSettings(currentSettings);
					});

				const auto overrideSettings =
					overrideManager->GetMergedOverrideSettings(featureName, json::object());
				if (!overrideManager->SaveUserOverride(featureName, currentSettings, overrideSettings))
					failedLayers.push_back(featureName);
			} catch (const std::exception& e) {
				logger::warn("Failed to save user overrides for {}: {}", featureName, e.what());
				failedLayers.push_back(featureName);
			} catch (...) {
				logger::warn("Failed to save user overrides for {} due to an unknown error", featureName);
				failedLayers.push_back(featureName);
			}
		}

		try {
			const auto globalOverrideSettings =
				overrideManager->GetMergedOverrideSettings("Global", json::object());
			if (!overrideManager->SaveUserOverride("Global", a_settings, globalOverrideSettings))
				failedLayers.emplace_back("Global");
		} catch (const std::exception& e) {
			logger::warn("Failed to save global user overrides: {}", e.what());
			failedLayers.emplace_back("Global");
		} catch (...) {
			logger::warn("Failed to save global user overrides due to an unknown error");
			failedLayers.emplace_back("Global");
		}

		return failedLayers;
	}

	std::string JoinSettingLayerNames(const std::vector<std::string>& a_names)
	{
		std::string result;
		for (const auto& name : a_names) {
			if (!result.empty())
				result += ", ";
			result += name;
		}
		return result;
	}

	void StoreMax(std::atomic_uint32_t& a_target, uint32_t a_value)
	{
		uint32_t current = a_target.load(std::memory_order_acquire);
		while (current < a_value) {
			if (a_target.compare_exchange_weak(current, a_value, std::memory_order_acq_rel, std::memory_order_acquire)) {
				return;
			}
		}
	}

	void ForceDisableAtBootFeature(json& a_disabledFeaturesJson, std::string_view a_featureName)
	{
		const std::string featureKey(a_featureName);
		if (!a_disabledFeaturesJson.value(featureKey, false)) {
			logger::info("Feature '{}' is force-disabled at boot by this build", a_featureName);
		}
		a_disabledFeaturesJson[featureKey] = true;
	}

	void ApplyDefaultDisableAtBootSettings(json& a_disabledFeaturesJson)
	{
		static constexpr std::pair<std::string_view, bool> defaultDisableAtBootSettings[] = {
			{ "UnifiedWater", false },
			{ WetnessEffects::kShortName, false }
		};

		for (const auto& [featureName, isDisabled] : defaultDisableAtBootSettings) {
			if constexpr (WetnessEffects::kForceDisableInAIO) {
				if (featureName == WetnessEffects::kShortName) {
					continue;
				}
			}

			const std::string featureKey(featureName);
			if (!a_disabledFeaturesJson.contains(featureKey)) {
				a_disabledFeaturesJson[featureKey] = isDisabled;
				logger::info("Default boot state for '{}' set to {}", featureName, isDisabled ? "Disabled" : "Enabled");
			}
		}
	}

	bool IsForcedDisableAtBootFeature(std::string_view a_featureName)
	{
		if constexpr (WetnessEffects::kForceDisableInAIO) {
			if (a_featureName == WetnessEffects::kShortName) {
				return true;
			}
		}
		return std::ranges::find(kForcedDisableAtBootFeatures, a_featureName) != std::end(kForcedDisableAtBootFeatures);
	}

	void ApplyForcedDisableAtBootSettings(json& a_disabledFeaturesJson)
	{
		// Build-level kill switches: keep features registered for cache/config handling
		// while preventing load, hooks, resources, prepass, and shader defines.
		for (const auto featureName : kForcedDisableAtBootFeatures) {
			ForceDisableAtBootFeature(a_disabledFeaturesJson, featureName);
		}
		if constexpr (WetnessEffects::kForceDisableInAIO) {
			ForceDisableAtBootFeature(a_disabledFeaturesJson, WetnessEffects::kShortName);
		}
	}

	float2 GetMainRenderTargetSize()
	{
		auto* renderer = globals::game::renderer;
		if (!renderer) {
			return {};
		}

		const auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
		if (!main.texture) {
			return {};
		}

		D3D11_TEXTURE2D_DESC texDesc{};
		main.texture->GetDesc(&texDesc);
		return { static_cast<float>(texDesc.Width), static_cast<float>(texDesc.Height) };
	}

	constexpr double kStateDrawPhaseDiagThresholdUs = 500.0;
	constexpr double kStateDrawPhaseDiagSevereThresholdUs = 3000.0;
	constexpr bool kStateDrawPhaseDiagnosticsEnabled = false;

	uint64_t ReadStateDrawDiagCounterTicks()
	{
		LARGE_INTEGER counter{};
		QueryPerformanceCounter(&counter);
		return static_cast<uint64_t>(counter.QuadPart);
	}

	double ConvertStateDrawDiagTicksToMicroseconds(uint64_t a_ticks)
	{
		static const uint64_t frequency = []() {
			LARGE_INTEGER counterFrequency{};
			QueryPerformanceFrequency(&counterFrequency);
			return static_cast<uint64_t>(std::max<LONGLONG>(counterFrequency.QuadPart, 1));
		}();
		return static_cast<double>(a_ticks) * 1000000.0 / static_cast<double>(frequency);
	}

	uint64_t ConvertStateDrawDiagMicrosecondsToTicks(double a_microseconds)
	{
		static const uint64_t frequency = []() {
			LARGE_INTEGER counterFrequency{};
			QueryPerformanceFrequency(&counterFrequency);
			return static_cast<uint64_t>(std::max<LONGLONG>(counterFrequency.QuadPart, 1));
		}();
		return static_cast<uint64_t>(
			std::max(
				1.0,
				a_microseconds * static_cast<double>(frequency) / 1000000.0));
	}

	std::string FormatStateDrawDiagMenus()
	{
		auto* ui = globals::game::ui;
		if (!ui)
			return "none";

		static constexpr std::array<std::string_view, 12> kMenuNames{ {
			"Main Menu",
			"Loading Menu",
			"MapMenu",
			"Journal Menu",
			"StatsMenu",
			"InventoryMenu",
			"MagicMenu",
			"TweenMenu",
			"Dialogue Menu",
			"BarterMenu",
			"ContainerMenu",
			"Crafting Menu",
		} };

		std::string result;
		for (const auto menuName : kMenuNames) {
			if (!ui->IsMenuOpen(menuName.data()))
				continue;

			if (!result.empty())
				result += "|";
			result += menuName;
		}

		return result.empty() ? "none" : result;
	}

	enum class StateDrawPhase : size_t
	{
		Total,
		SceneSettingsUpdate,
		PerfModeRelatch,
		PostLoadRuntimeReset,
		WeatherUpdateFeatures,
		TerrainShaderHacks,
		CloudShadowShaderHacks,
		TerrainHelperResources,
		TruePBRResources,
		VolumetricShadowResources,
		PermutationCBUpdate,
		ShadowDataCopy,
		DebugOverlay,
		Count
	};

	struct StateDrawPhaseStats
	{
		uint64_t totalTicks = 0;
		uint64_t maxTicks = 0;
		uint32_t calls = 0;
	};

	struct StateDrawPhaseFrameStats
	{
		uint32_t frame = std::numeric_limits<uint32_t>::max();
		std::array<StateDrawPhaseStats, static_cast<size_t>(StateDrawPhase::Count)> phases{};

		void Reset(uint32_t a_frame)
		{
			frame = a_frame;
			phases = {};
		}
	};

	StateDrawPhaseFrameStats g_stateDrawPhaseDiag;

	bool ShouldRecordStateDrawPhaseDiag()
	{
		if constexpr (!kStateDrawPhaseDiagnosticsEnabled) {
			return false;
		} else {
			auto* state = globals::state;
			return globals::game::isVR && state && state->IsDeveloperMode();
		}
	}

	StateDrawPhaseStats& GetStateDrawPhaseStats(StateDrawPhase a_phase)
	{
		return g_stateDrawPhaseDiag.phases[static_cast<size_t>(a_phase)];
	}

	void RecordStateDrawPhase(StateDrawPhase a_phase, uint32_t a_frame, uint64_t a_elapsedTicks)
	{
		if (!a_elapsedTicks)
			return;

		if (g_stateDrawPhaseDiag.frame != a_frame)
			g_stateDrawPhaseDiag.Reset(a_frame);

		auto& stats = GetStateDrawPhaseStats(a_phase);
		stats.totalTicks += a_elapsedTicks;
		stats.maxTicks = std::max(stats.maxTicks, a_elapsedTicks);
		stats.calls++;
	}

	void FlushStateDrawPhaseDiagForNewFrame(uint32_t a_currentFrame)
	{
		if (g_stateDrawPhaseDiag.frame == std::numeric_limits<uint32_t>::max()) {
			g_stateDrawPhaseDiag.Reset(a_currentFrame);
			return;
		}
		if (g_stateDrawPhaseDiag.frame == a_currentFrame)
			return;

		const uint32_t completedFrame = g_stateDrawPhaseDiag.frame;
		const auto& total = GetStateDrawPhaseStats(StateDrawPhase::Total);
		const uint64_t thresholdTicks = ConvertStateDrawDiagMicrosecondsToTicks(kStateDrawPhaseDiagThresholdUs);
		const uint64_t severeThresholdTicks = ConvertStateDrawDiagMicrosecondsToTicks(kStateDrawPhaseDiagSevereThresholdUs);
		const bool shouldLog =
			total.calls > 0 &&
			(total.totalTicks >= thresholdTicks || total.maxTicks >= thresholdTicks);
		if (shouldLog) {
			static uint32_t loggedFrameCount = 0;
			const bool severe = total.totalTicks >= severeThresholdTicks || total.maxTicks >= severeThresholdTicks;
			if (loggedFrameCount < 256 || severe) {
				loggedFrameCount++;
				const auto& scene = GetStateDrawPhaseStats(StateDrawPhase::SceneSettingsUpdate);
				const auto& relatch = GetStateDrawPhaseStats(StateDrawPhase::PerfModeRelatch);
				const auto& postLoad = GetStateDrawPhaseStats(StateDrawPhase::PostLoadRuntimeReset);
				const auto& weather = GetStateDrawPhaseStats(StateDrawPhase::WeatherUpdateFeatures);
				const auto& terrain = GetStateDrawPhaseStats(StateDrawPhase::TerrainShaderHacks);
				const auto& cloud = GetStateDrawPhaseStats(StateDrawPhase::CloudShadowShaderHacks);
				const auto& terrainResources = GetStateDrawPhaseStats(StateDrawPhase::TerrainHelperResources);
				const auto& truePBRResources = GetStateDrawPhaseStats(StateDrawPhase::TruePBRResources);
				const auto& volumetricShadowResources = GetStateDrawPhaseStats(StateDrawPhase::VolumetricShadowResources);
				const auto& permutationUpdate = GetStateDrawPhaseStats(StateDrawPhase::PermutationCBUpdate);
				const auto& shadowCopy = GetStateDrawPhaseStats(StateDrawPhase::ShadowDataCopy);
				const auto& debugOverlay = GetStateDrawPhaseStats(StateDrawPhase::DebugOverlay);
				logger::debug(
					"[CSFramePhase][StateDraw] frame={} menus={} calls={} totalUs={:.2f} maxCallUs={:.2f} sceneUs={:.2f} relatchUs={:.2f} postLoadUs={:.2f} weatherUs={:.2f} terrainUs={:.2f} cloudUs={:.2f} terrainResUs={:.2f} truePBRResUs={:.2f} volShadowResUs={:.2f} permutationUs={:.2f} shadowCopyUs={:.2f} debugUs={:.2f}",
					completedFrame,
					FormatStateDrawDiagMenus(),
					total.calls,
					ConvertStateDrawDiagTicksToMicroseconds(total.totalTicks),
					ConvertStateDrawDiagTicksToMicroseconds(total.maxTicks),
					ConvertStateDrawDiagTicksToMicroseconds(scene.totalTicks),
					ConvertStateDrawDiagTicksToMicroseconds(relatch.totalTicks),
					ConvertStateDrawDiagTicksToMicroseconds(postLoad.totalTicks),
					ConvertStateDrawDiagTicksToMicroseconds(weather.totalTicks),
					ConvertStateDrawDiagTicksToMicroseconds(terrain.totalTicks),
					ConvertStateDrawDiagTicksToMicroseconds(cloud.totalTicks),
					ConvertStateDrawDiagTicksToMicroseconds(terrainResources.totalTicks),
					ConvertStateDrawDiagTicksToMicroseconds(truePBRResources.totalTicks),
					ConvertStateDrawDiagTicksToMicroseconds(volumetricShadowResources.totalTicks),
					ConvertStateDrawDiagTicksToMicroseconds(permutationUpdate.totalTicks),
					ConvertStateDrawDiagTicksToMicroseconds(shadowCopy.totalTicks),
					ConvertStateDrawDiagTicksToMicroseconds(debugOverlay.totalTicks));
			}
		}

		g_stateDrawPhaseDiag.Reset(a_currentFrame);
	}

	struct ScopedStateDrawPhaseTimer
	{
		StateDrawPhase phase;
		uint32_t frame = 0;
		uint64_t startTicks = 0;
		bool active = false;

		ScopedStateDrawPhaseTimer(StateDrawPhase a_phase, uint32_t a_frame, bool a_active) :
			phase(a_phase), frame(a_frame), active(a_active)
		{
			if (active)
				startTicks = ReadStateDrawDiagCounterTicks();
		}

		~ScopedStateDrawPhaseTimer()
		{
			if (!active)
				return;

			const uint64_t endTicks = ReadStateDrawDiagCounterTicks();
			RecordStateDrawPhase(phase, frame, endTicks >= startTicks ? (endTicks - startTicks) : 0);
		}
	};
}

void State::Draw()
{
	ZoneScoped;
	const bool stateDrawDiagActive = ShouldRecordStateDrawPhaseDiag();
	const uint32_t stateDrawDiagFrame = stateDrawDiagActive ? frameCount : 0;
	if (stateDrawDiagActive)
		FlushStateDrawPhaseDiagForNewFrame(stateDrawDiagFrame);
	ScopedStateDrawPhaseTimer stateDrawTotalDiag(StateDrawPhase::Total, stateDrawDiagFrame, stateDrawDiagActive);

	auto shaderCache = globals::shaderCache;
	auto deferred = globals::deferred;
	auto& terrainBlending = globals::features::terrainBlending;
	auto& terrainHelper = globals::features::terrainHelper;
	auto& cloudShadows = globals::features::cloudShadows;
	auto& csEditor = globals::features::csEditor;
	auto& weatherPicker = globals::features::weatherPicker;
	auto& truePBR = globals::features::truePBR;
	auto& volumetricShadows = globals::features::volumetricShadows;
	auto context = globals::d3d::context;

	if (shaderCache->IsEnabled()) {
		// Process deferred cell transitions (interior detection)
		{
			ScopedStateDrawPhaseTimer phaseDiag(StateDrawPhase::SceneSettingsUpdate, stateDrawDiagFrame, stateDrawDiagActive);
			SceneSettingsManager::GetSingleton()->Update();
		}
		{
			ScopedStateDrawPhaseTimer phaseDiag(StateDrawPhase::PerfModeRelatch, stateDrawDiagFrame, stateDrawDiagActive);
			globals::features::upscaling.ApplyPendingPerfModeRenderTargetRecreate("State::Draw");
		}

		if (pendingPostLoadRuntimeReset) {
			ScopedStateDrawPhaseTimer phaseDiag(StateDrawPhase::PostLoadRuntimeReset, stateDrawDiagFrame, stateDrawDiagActive);
			globals::OnDataLoaded();
			WeatherManager::GetSingleton()->ClearCache();
			globals::features::lightLimitFix.Reset();
			globals::features::interiorSun.isInteriorWithSun = false;
			globals::features::wetterness.ResetRuntimeStateAfterGameLoad();
			pendingPostLoadRuntimeReset = false;
			logger::info("Applied deferred post-load runtime reset");
		}

		if (csEditor.loaded || weatherPicker.loaded) {
			ZoneScopedN("WeatherManager::UpdateFeatures");
			ScopedStateDrawPhaseTimer phaseDiag(StateDrawPhase::WeatherUpdateFeatures, stateDrawDiagFrame, stateDrawDiagActive);
			WeatherManager::GetSingleton()->UpdateFeatures();
		}

		if (terrainBlending.loaded && terrainBlending.settings.Enabled) {
			ZoneScopedN("TerrainBlending::TerrainShaderHacks");
			ScopedStateDrawPhaseTimer phaseDiag(StateDrawPhase::TerrainShaderHacks, stateDrawDiagFrame, stateDrawDiagActive);
			terrainBlending.TerrainShaderHacks();
		}

		if (cloudShadows.loaded) {
			ZoneScopedN("CloudShadows::SkyShaderHacks");
			ScopedStateDrawPhaseTimer phaseDiag(StateDrawPhase::CloudShadowShaderHacks, stateDrawDiagFrame, stateDrawDiagActive);
			cloudShadows.SkyShaderHacks();
		}

		if (terrainHelper.loaded) {
			ZoneScopedN("TerrainHelper::SetShaderResouces");
			ScopedStateDrawPhaseTimer phaseDiag(StateDrawPhase::TerrainHelperResources, stateDrawDiagFrame, stateDrawDiagActive);
			terrainHelper.SetShaderResouces(context);
		}

		if (truePBR.loaded) {
			ZoneScopedN("TruePBR::SetShaderResouces");
			ScopedStateDrawPhaseTimer phaseDiag(StateDrawPhase::TruePBRResources, stateDrawDiagFrame, stateDrawDiagActive);
			truePBR.SetShaderResouces(context);
		}

		if (volumetricShadows.loaded) {
			ZoneScopedN("VolumetricShadows::SetShaderResources");
			ScopedStateDrawPhaseTimer phaseDiag(StateDrawPhase::VolumetricShadowResources, stateDrawDiagFrame, stateDrawDiagActive);
			volumetricShadows.SetShaderResources(context);
		}

		if (permutationData != permutationDataPrevious) {
			ScopedStateDrawPhaseTimer phaseDiag(StateDrawPhase::PermutationCBUpdate, stateDrawDiagFrame, stateDrawDiagActive);
			permutationCB->Update(permutationData);
			permutationDataPrevious = permutationData;
		}

		if (currentShader && updateShader) {
			if (currentShader->shaderType.get() == RE::BSShader::Type::Utility) {
				if (currentPixelDescriptor & static_cast<uint32_t>(SIE::ShaderCache::UtilityShaderFlags::RenderShadowmask)) {
					ScopedStateDrawPhaseTimer phaseDiag(StateDrawPhase::ShadowDataCopy, stateDrawDiagFrame, stateDrawDiagActive);
					if (volumetricShadows.loaded)
						volumetricShadows.CopyShadowLightData();
					else
						deferred->CopyShadowData();
				}
			}
		}

		if (globals::menu->overlayVisible && globals::features::performanceOverlay.loaded && globals::features::performanceOverlay.IsOverlayVisible()) {
			ScopedStateDrawPhaseTimer phaseDiag(StateDrawPhase::DebugOverlay, stateDrawDiagFrame, stateDrawDiagActive);
			Debug();
		}

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
		// Per-draw annotations must remain capture-only. Dynamic Tracy zones allocate
		// a source location for every draw and cannot be sustained at this frequency.
		BeginDrawEvent("Draw: CS {}::{:x}::{}", magic_enum::enum_name(currentShader->shaderType.get()), permutationData.PixelShaderDescriptor, currentShader->fxpFilename);
		SetPerfMarker("Defines: {}", SIE::ShaderCache::GetDefinesString(*currentShader, permutationData.PixelShaderDescriptor));
		EndDrawEvent();
	}
}

bool State::IsSaveLoadSafeModeActive() const
{
	return saveLoadSafeModeActive.load(std::memory_order_acquire);
}

bool State::IsEngineSaveLoadActivityActive() const
{
	return engineSaveLoadActivityActive.load(std::memory_order_acquire);
}

bool State::IsPersistentMutationBlocked() const
{
	return persistentMutationBlocked.load(std::memory_order_acquire);
}

void State::BeginSaveLoadSafeMode(uint32_t a_currentFrame)
{
	const uint32_t currentFrame = a_currentFrame != 0 ? a_currentFrame : std::max(frameCount, 1u);
	saveLoadSafeModeStartFrame.store(currentFrame, std::memory_order_release);
	saveLoadSafeModeEndFrame.store(0, std::memory_order_release);
	if (globals::shaderCache)
		globals::shaderCache->SetSaveLoadDiskPersistenceBlocked(true);
	saveLoadSafeModeActive.store(true, std::memory_order_release);
	persistentMutationBlocked.store(true, std::memory_order_release);
}

void State::ExtendSaveLoadSafeMode(uint32_t a_currentFrame, uint32_t a_frameCount)
{
	const uint32_t currentFrame = a_currentFrame != 0 ? a_currentFrame : std::max(frameCount, 1u);
	const uint32_t endFrame = currentFrame + std::max(a_frameCount, 1u);
	if (saveLoadSafeModeStartFrame.load(std::memory_order_acquire) == 0) {
		saveLoadSafeModeStartFrame.store(currentFrame, std::memory_order_release);
	}
	StoreMax(saveLoadSafeModeEndFrame, endFrame);
	if (globals::shaderCache)
		globals::shaderCache->SetSaveLoadDiskPersistenceBlocked(true);
	saveLoadSafeModeActive.store(true, std::memory_order_release);
	persistentMutationBlocked.store(true, std::memory_order_release);
}

void State::BeginPersistentMutationBlock(uint32_t a_currentFrame, uint32_t a_frameCount)
{
	const uint32_t currentFrame = a_currentFrame != 0 ? a_currentFrame : std::max(frameCount, 1u);
	const uint32_t endFrame = currentFrame + std::max(a_frameCount, 1u);
	StoreMax(persistentMutationBlockEndFrame, endFrame);
	persistentMutationBlocked.store(true, std::memory_order_release);
}

void State::ExtendPersistentMutationBlock(uint32_t a_currentFrame, uint32_t a_frameCount)
{
	const uint32_t currentFrame = a_currentFrame != 0 ? a_currentFrame : std::max(frameCount, 1u);
	const uint32_t endFrame = currentFrame + std::max(a_frameCount, 1u);
	StoreMax(persistentMutationBlockEndFrame, endFrame);
	persistentMutationBlocked.store(true, std::memory_order_release);
}

void State::UpdateSaveLoadSafeMode()
{
	const uint32_t currentFrame = std::max(frameCount, 1u);
	bool safeModeActive = saveLoadSafeModeActive.load(std::memory_order_acquire);
	const bool wasSafeModeActive = safeModeActive;

	bool engineSaveLoadActive = false;
	if (auto* saveLoad = RE::BGSSaveLoadGame::GetSingleton()) {
		engineSaveLoadActive =
			saveLoad->GetSaveGameLoading() ||
			saveLoad->GetSaveGameSaving() ||
			saveLoad->GetInitingForms() ||
			saveLoad->GetDeferInitForms() ||
			saveLoad->GetPositioningPlayerCharacter();
	}
	engineSaveLoadActivityActive.store(
		engineSaveLoadActive,
		std::memory_order_release);

	if (engineSaveLoadActive) {
		if (!safeModeActive) {
			if (globals::shaderCache)
				globals::shaderCache->SetSaveLoadDiskPersistenceBlocked(true);
			saveLoadSafeModeStartFrame.store(currentFrame, std::memory_order_release);
		}
		StoreMax(saveLoadSafeModeEndFrame, currentFrame + kSaveLoadSafeModeGraceFrames);
		safeModeActive = true;
		saveLoadSafeModeActive.store(true, std::memory_order_release);
	} else if (safeModeActive) {
		const uint32_t endFrame = saveLoadSafeModeEndFrame.load(std::memory_order_acquire);
		if (endFrame != 0) {
			if (currentFrame >= endFrame) {
				safeModeActive = false;
				saveLoadSafeModeActive.store(false, std::memory_order_release);
				saveLoadSafeModeStartFrame.store(0, std::memory_order_release);
				saveLoadSafeModeEndFrame.store(0, std::memory_order_release);
			}
		} else {
			const uint32_t startFrame = saveLoadSafeModeStartFrame.load(std::memory_order_acquire);
			if (startFrame != 0 && currentFrame - startFrame >= kSaveLoadSafeModeFallbackFrames) {
				logger::warn("Save/load safe mode timed out after {} frames without a completion event", currentFrame - startFrame);
				safeModeActive = false;
				saveLoadSafeModeActive.store(false, std::memory_order_release);
				saveLoadSafeModeStartFrame.store(0, std::memory_order_release);
				saveLoadSafeModeEndFrame.store(0, std::memory_order_release);
			}
		}
	}

	uint32_t mutationBlockEndFrame = persistentMutationBlockEndFrame.load(std::memory_order_acquire);
	if (mutationBlockEndFrame != 0 && currentFrame >= mutationBlockEndFrame) {
		persistentMutationBlockEndFrame.store(0, std::memory_order_release);
		mutationBlockEndFrame = 0;
	}

	const bool mutationGraceActive = mutationBlockEndFrame != 0 && currentFrame < mutationBlockEndFrame;
	persistentMutationBlocked.store(safeModeActive || mutationGraceActive, std::memory_order_release);

	if (wasSafeModeActive && !safeModeActive && globals::shaderCache)
		globals::shaderCache->SetSaveLoadDiskPersistenceBlocked(false);
}

void State::Reset()
{
	globals::profiler->EndFrame(frameCount);
	Feature::ForEachLoadedFeature("Reset", [](Feature* feature) { feature->Reset(); });
	if (!globals::game::ui->GameIsPaused())
		timer += RE::GetSecondsSinceLastFrame();

	// Cache menu open states once per frame to avoid repeated IsMenuOpen calls
	// (each call constructs a BSFixedString, which is expensive at scale).
	if (auto ui = globals::game::ui) {
		isMainMenuOpen = ui->IsMenuOpen(RE::MainMenu::MENU_NAME);
		isLoadingMenuOpen = ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME);
		isMapMenuOpen = ui->IsMenuOpen(RE::MapMenu::MENU_NAME);
	} else {
		isMainMenuOpen = false;
		isLoadingMenuOpen = false;
		isMapMenuOpen = false;
	}

	lastModifiedPixelDescriptor = 0;
	lastModifiedVertexDescriptor = 0;
	lastPixelDescriptor = 0;
	lastVertexDescriptor = 0;
	std::memset(&permutationDataPrevious, 0xFF, sizeof(PermutationCB));
	frameCount++;
	UpdateSaveLoadSafeMode();

	globals::shaderCache->TickActiveShaderCapture(globals::menu && globals::menu->IsEnabled);

	if (auto* imageSpaceManager = RE::ImageSpaceManager::GetSingleton()) {
		GET_INSTANCE_MEMBER(BSImagespaceShaderApplyReflections, imageSpaceManager);

		// Disable reflections being applied to things other than water
		if (BSImagespaceShaderApplyReflections.get()) {
			BSImagespaceShaderApplyReflections->active = false;
		}
	}

	// Disable "improved" snow shader, unsupported
	if (!globals::game::isVR) {
		RE::GetINISetting("bEnableImprovedSnow:Display")->data.b = false;
	}

	activeReflections = false;
}

void State::Setup()
{
	SetupResources();

	// Probe typed UAV load support before features set up their resources, so any
	// gating logic that wants to read the log can run during feature SetupResources.
	CheckTypedUAVLoadSupport();

	Feature::ForEachLoadedFeature("SetupResources", [](Feature* feature) { feature->SetupResources(); });
	globals::deferred->SetupResources();

	// Load per-weather settings after features are setup
	WeatherManager::GetSingleton()->LoadPerWeatherSettingsFromDisk();

	// Load scene-specific settings (Interior Only, etc.)
	SceneSettingsManager::GetSingleton()->LoadAll();
}

void State::SetupRenderTargetResources()
{
	const bool stateResourcesMissing =
		!permutationCB ||
		!sharedDataCB ||
		!featureDataCB;
	const bool d3dDeviceChanged =
		setupResourcesDevice != globals::d3d::device ||
		setupResourcesContext != globals::d3d::context;

	if (stateResourcesMissing || d3dDeviceChanged) {
		Setup();
		return;
	}

	const auto mainRenderTargetSize = GetMainRenderTargetSize();
	if (mainRenderTargetSize.x > 0.0f && mainRenderTargetSize.y > 0.0f) {
		screenSize = mainRenderTargetSize;
	}
	featureLevel = globals::d3d::device->GetFeatureLevel();

	// VR render-scale relatch only needs resources tied to recreated render targets.
	// Keep disk/world discovery and full feature setup on State::Setup().
	Feature::ForEachLoadedFeature("SetupRenderTargetResources", [](Feature* feature) { feature->SetupRenderTargetResources(); });
	globals::deferred->SetupResources();
}

static std::filesystem::path GetConfigPath(State::ConfigMode a_configMode)
{
	switch (a_configMode) {
	case State::ConfigMode::USER:
		return Util::PathHelpers::GetSettingsUserPath();
	case State::ConfigMode::TEST:
		return Util::PathHelpers::GetSettingsTestPath();
	case State::ConfigMode::THEME:
		return Util::PathHelpers::GetSettingsThemePath();
	case State::ConfigMode::DEFAULT:
	default:
		return Util::PathHelpers::GetSettingsDefaultPath();
	}
}

void State::Load(ConfigMode a_configMode, bool a_allowReload)
{
	json settings = json::object();
	bool errorDetected = false;

	const auto configPath = GetConfigPath(a_configMode);
	const auto configFolderPath = configPath.parent_path();
	const auto defaultConfigFilePath = GetConfigPath(ConfigMode::DEFAULT);

	try {
		std::filesystem::create_directories(configFolderPath);
	} catch (const std::filesystem::filesystem_error& e) {
		logger::warn("Error creating directory during Load ({}): {}", configFolderPath.string(), e.what());
	}

	auto tryLoadConfig = [](const std::filesystem::path& a_path, json& o_settings) {
		logger::info("Attempting to open config file: {}", a_path.string());

		std::string errorMessage;
		const auto result = Util::FileHelpers::ReadJsonFile(a_path, o_settings, errorMessage);
		if (result == Util::FileHelpers::JsonFileReadResult::Success && !o_settings.is_object()) {
			logger::warn("Config file must contain a JSON object: {}", a_path.string());
			return Util::FileHelpers::JsonFileReadResult::Error;
		}

		if (result == Util::FileHelpers::JsonFileReadResult::Error)
			logger::warn("Unable to read config file {}: {}", a_path.string(), errorMessage);
		return result;
	};

	auto canonicalizeConfig = [](const std::filesystem::path& a_path, const json& a_settings) {
		std::string errorMessage;
		const auto result = SettingsSerialization::CanonicalizeFile(a_path, a_settings, errorMessage);
		if (result == SettingsSerialization::CanonicalizationResult::Rewritten) {
			logger::info("Reordered settings to match the UI: {}", a_path.string());
		} else if (result == SettingsSerialization::CanonicalizationResult::Error) {
			logger::warn("Could not reorder settings file {}: {}", a_path.string(), errorMessage);
		}
		return result;
	};

	auto generateFallbackSettingsInMemory = [&]() {
		settings = json::object();
		std::fill(enabledClasses, enabledClasses + magic_enum::enum_integer(RE::BSShader::Type::Total) - 1, true);
		try {
			SaveToJson(settings, true);
			return true;
		} catch (const std::exception& e) {
			logger::error("Could not generate fallback settings in memory: {}", e.what());
		} catch (...) {
			logger::error("Could not generate fallback settings in memory due to an unknown error");
		}
		return false;
	};

	// LOADING ORDER: Default -> User -> Overrides -> User Overrides (.user files)

	// Step 1: Always start with default settings
	logger::info("Loading default settings from: {}", defaultConfigFilePath.string());
	const auto defaultResult = tryLoadConfig(defaultConfigFilePath, settings);
	bool loadedDefaultFromDisk = defaultResult == Util::FileHelpers::JsonFileReadResult::Success;
	bool sourceConfigSafeForAutomaticSave = true;
	if (defaultResult == Util::FileHelpers::JsonFileReadResult::NotFound) {
		logger::info("No usable default config ({}), generating a new one", defaultConfigFilePath.string());
		std::fill(enabledClasses, enabledClasses + magic_enum::enum_integer(RE::BSShader::Type::Total) - 1, true);
		Save(ConfigMode::DEFAULT);
		loadedDefaultFromDisk =
			tryLoadConfig(defaultConfigFilePath, settings) == Util::FileHelpers::JsonFileReadResult::Success;
		if (!loadedDefaultFromDisk) {
			sourceConfigSafeForAutomaticSave = false;
			logger::warn("Could not persist default settings; continuing with an in-memory baseline");
			if (!generateFallbackSettingsInMemory())
				return;
		}
	} else if (defaultResult == Util::FileHelpers::JsonFileReadResult::Error) {
		sourceConfigSafeForAutomaticSave = false;
		logger::error("Default config is invalid or unreadable; preserving it and using an in-memory baseline: {}", defaultConfigFilePath.string());
		if (!generateFallbackSettingsInMemory())
			return;
	}
	if (loadedDefaultFromDisk) {
		const auto result = canonicalizeConfig(defaultConfigFilePath, settings);
		if (result == SettingsSerialization::CanonicalizationResult::Error)
			sourceConfigSafeForAutomaticSave = false;
		SettingsMigrations::MigrateAdaptiveBalanceRootLayer(settings);
	}

	// Step 2: Apply user settings on top of defaults.
	if (a_configMode == ConfigMode::USER) {
		json userSettings;
		const auto userResult = tryLoadConfig(configPath, userSettings);
		if (userResult == Util::FileHelpers::JsonFileReadResult::Success) {
			if (canonicalizeConfig(configPath, userSettings) == SettingsSerialization::CanonicalizationResult::Error)
				sourceConfigSafeForAutomaticSave = false;
			const auto adaptiveBalanceIt = userSettings.find(SettingsMigrations::kAdaptiveBalanceSettingsName.data());
			const auto legacyAdaptiveBrightnessIt =
				userSettings.find(SettingsMigrations::kLegacyAdaptiveBrightnessSettingsName.data());
			const bool userDefinedAdaptiveBalance =
				(adaptiveBalanceIt != userSettings.end() && adaptiveBalanceIt->is_object()) ||
				(legacyAdaptiveBrightnessIt != userSettings.end() && legacyAdaptiveBrightnessIt->is_object());
			SettingsMigrations::MigrateAdaptiveBalanceRootLayer(userSettings);
			for (const auto& [key, value] : userSettings.items()) {
				auto existingIt = settings.find(key);
				if (!userDefinedAdaptiveBalance &&
					key == SettingsMigrations::kAdaptiveBalanceSettingsName &&
					existingIt != settings.end() && existingIt->is_object() && value.is_object()) {
					// Migration can synthesize a partial destination section in an
					// otherwise-CSUtility-only user file. Overlay that patch so it does
					// not replace richer Adaptive Balance defaults from Settings.json.
					existingIt->merge_patch(value);
				} else {
					settings[key] = value;
				}
			}
			logger::info("Applied user settings from: {}", configPath.string());
		} else if (userResult == Util::FileHelpers::JsonFileReadResult::NotFound) {
			logger::info("No user config file found at: {}", configPath.string());
		} else {
			sourceConfigSafeForAutomaticSave = false;
		}
	}

	// Step 3: Discover and prepare overrides (applied after user settings, so overrides take priority)
	auto overrideManager = SettingsOverrideManager::GetSingleton();
	size_t overridesDiscovered = overrideManager->DiscoverOverrides();

	// Cleanup stale user override files (where override hash has changed)
	if (overridesDiscovered > 0) {
		logger::info("Discovered {} override files", overridesDiscovered);
		overrideManager->CleanupStaleUserOverrides();

		// Apply global overrides to main settings
		size_t globalOverrides = overrideManager->ApplyGlobalOverrides(settings);
		if (globalOverrides > 0) {
			logger::info("Applied {} global override(s)", globalOverrides);
		}

		// Apply global user overrides on top (if any)
		if (overrideManager->LoadUserOverride("Global", settings)) {
			logger::info("Applied global user override customizations");
		}
	}
	SettingsMigrations::MigrateAdaptiveBalanceRootLayer(settings);

	try {
		// Load core settings (Menu, Advanced, General, Replace Original Shaders)
		logger::info("Loading core settings");
		LoadFromJson(settings, false);
		// Ensure 'Disable at Boot' section exists in the JSON
		if (!settings.contains("Disable at Boot") || !settings["Disable at Boot"].is_object()) {
			// Initialize to an empty object if it doesn't exist
			settings["Disable at Boot"] = json::object();
		}

		json& disabledFeaturesJson = settings["Disable at Boot"];
		ApplyDefaultDisableAtBootSettings(disabledFeaturesJson);
		ApplyForcedDisableAtBootSettings(disabledFeaturesJson);
		logger::info("Loading 'Disable at Boot' settings");

		disabledFeatures.clear();
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
				bool isDisabled = disabledFeatures.contains(featureName) && disabledFeatures[featureName];
				if (!isDisabled) {
					logger::info("Loading Feature: '{}'", featureName);

					// Load base feature settings from the merged default and user config.
					feature->Load(settings);
					if (!feature->loaded) {
						logger::info("Feature '{}' did not finish loading; skipping post-load initialization.", featureName);
						continue;
					}

					// Register weather variables (features opt-in by implementing this)
					feature->RegisterWeatherVariables();

					// Apply feature-specific overrides on top (overrides take priority over user settings)
					if (overridesDiscovered > 0 && overrideManager->HasFeatureOverrides(featureName)) {
						json featureJson;
						feature->SaveSettings(featureJson);  // Get current settings as JSON

						// Apply overrides
						size_t appliedOverrides = overrideManager->ApplyOverrides(featureName, featureJson);
						if (appliedOverrides > 0) {
							logger::info("Applied {} override(s) to {}", appliedOverrides, feature->GetName());
						}

						// Apply user override customizations on top (if any)
						if (overrideManager->LoadUserOverride(featureName, featureJson)) {
							logger::info("Applied user override customizations to {}", feature->GetName());
						}

						// Reload settings with overrides applied
						try {
							feature->LoadSettings(featureJson);
						} catch (...) {
							logger::warn("Invalid override settings for {}, keeping original settings.", feature->GetName());
						}
					}

					// Capture current values as user settings baseline for weather overrides
					WeatherVariables::GlobalWeatherRegistry::GetSingleton()->CaptureFeatureUserSettings(featureName);
				} else {
					logger::info("Feature '{}' is disabled at boot.", featureName);
				}
			} catch (const std::exception& e) {
				const auto displayName = feature->GetDisplayName();
				feature->failedLoadedMessage = feature->failedLoadedMessage.empty() ?
				                                   (displayName + " failed to load. Check CommunityShaders.log") :
				                                   (feature->failedLoadedMessage + "\n" + displayName + " failed to load. Check CommunityShaders.log");
				logger::warn("Error loading setting for feature '{}': {}", feature->GetShortName(), e.what());
			}
		}

		if (globals::features::adaptiveBrightness.loaded && !globals::features::csUtility.loaded) {
			globals::features::adaptiveBrightness.loaded = false;
			globals::features::adaptiveBrightness.failedLoadedMessage =
				"Adaptive Balance requires CS Utility renderer support. Resolve the CS Utility load issue, then restart.";
			logger::warn("Adaptive Balance was disabled because its CSUtility renderer dependency is unavailable");
		}

		WeatherManager::GetSingleton()->NotifyUserSettingsChanged();

		const auto currentVersion = std::string{ Plugin::VERSION_LABEL };
		const auto versionIt = settings.find("Version");
		if (versionIt == settings.end() || !versionIt->is_string() || versionIt->get<std::string>() != currentVersion) {
			const auto loadedVersion = versionIt != settings.end() && versionIt->is_string() ?
			                               versionIt->get<std::string>() :
			                               std::string{ "<missing or invalid>" };
			if (sourceConfigSafeForAutomaticSave) {
				logger::info("Found config for version {}; upgrading to {}", loadedVersion, currentVersion);
				Save(a_configMode);  // Use original config mode
			} else {
				logger::warn(
					"Found config for version {}, but skipped automatic upgrade to {} because one or more source configs are not safe to rewrite",
					loadedVersion,
					currentVersion);
			}
		}

		FeatureIssues::ScanForOrphanedFeatureINIs();

		logger::info("Loading Settings Complete");
	} catch (const json::exception& e) {
		logger::warn("General JSON error accessing settings: {}; preserving the source config", e.what());
		errorDetected = true;
	} catch (const std::exception& e) {
		logger::warn("General error accessing settings: {}; preserving the source config", e.what());
		errorDetected = true;
	}
	if (errorDetected && a_allowReload && a_configMode != ConfigMode::DEFAULT) {
		logger::warn("Loading default settings after the selected config failed");
		Load(ConfigMode::DEFAULT, false);
	}
	if (!errorDetected && globals::menu)
		globals::menu->ResetSettingsDirtyState();
}

void State::SaveToJson(
	nlohmann::json& settings,
	bool a_includeMissingUnloadedFeatures)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	SettingsMigrations::MigrateAdaptiveBalanceRootLayer(settings);
	const auto shaderCache = globals::shaderCache;

	globals::menu->Save(settings["Menu"]);

	json advanced;
	advanced["Dump Shaders"] = shaderCache->IsDump();
	advanced["Log Level"] = logLevel;
	advanced["Shader Defines"] = GetShaderDefinesSnapshot()->canonicalText;
	advanced["Compiler Threads"] = shaderCache->compilationThreadCount;
	advanced["Background Compiler Threads"] = shaderCache->backgroundCompilationThreadCount;
	advanced["Use FileWatcher"] = shaderCache->UseFileWatcher();
	advanced["Frame Annotations"] = frameAnnotations;
	advanced["Refraction Scale"] = refractionScale;
	advanced["PBR Metal Reflection Scale"] = pbrMetalReflectionScale;
	advanced["PBR Metal Highlight Scale"] = pbrMetalHighlightScale;
	advanced["Partial Precision"] = enablePartialPrecision.load(std::memory_order_relaxed);
	settings["Advanced"] = advanced;

	json general;
	general["Enable Shaders"] = shaderCache->IsEnabled();
	general["Enable Disk Cache"] = shaderCache->IsDiskCache();
	general["Skip Unchanged Shaders"] = shaderCache->IsSkipUnchangedShaders();
	general["Enable Async"] = shaderCache->IsAsync();

	settings["General"] = general;

	json originalShaders;
	ForEachShaderTypeWithIndex([&](auto type, int classIndex) {
		originalShaders[magic_enum::enum_name(type)] = enabledClasses[classIndex];
	});
	settings["Replace Original Shaders"] = originalShaders;

	json disabledFeaturesJson;
	for (const auto& [featureName, isDisabled] : disabledFeatures) {
		if (IsForcedDisableAtBootFeature(featureName))
			continue;

		disabledFeaturesJson[featureName] = isDisabled;
	}
	ApplyDefaultDisableAtBootSettings(disabledFeaturesJson);
	settings["Disable at Boot"] = disabledFeaturesJson;

	settings["Version"] = std::string{ Plugin::VERSION_LABEL };

	// Save feature settings.
	auto* weatherRegistry =
		WeatherVariables::GlobalWeatherRegistry::GetSingleton();
	for (auto* feature : Feature::GetFeatureList()) {
		// Preserve the last valid settings for features which were disabled, missing,
		// or incompatible and therefore never loaded into memory.
		const std::string settingsName = feature->GetName();
		if (feature->loaded || (a_includeMissingUnloadedFeatures && !settings.contains(settingsName))) {
			if (feature->loaded) {
				const auto activeVariables =
					WeatherManager::GetSingleton()->GetActiveVariablesForFeature(
						feature->GetShortName());
				weatherRegistry->SerializeFeatureUserSettings(
					feature->GetShortName(), activeVariables, [&]() {
						feature->Save(settings);
					});
			} else {
				feature->Save(settings);
			}
		}
	}
}

void State::LoadFromJson(nlohmann::json& settings, bool a_loadFeatureSettings)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (a_loadFeatureSettings)
		SettingsMigrations::MigrateAdaptiveBalanceRootLayer(settings);
	const auto shaderCache = globals::shaderCache;

	// Load Menu settings
	if (settings.contains("Menu") && settings["Menu"].is_object()) {
		globals::menu->Load(settings["Menu"]);
	}

	if (settings.contains("Advanced") && settings["Advanced"].is_object()) {
		json& advanced = settings["Advanced"];
		const auto maxCompilerThreads = std::max(1, static_cast<int32_t>(std::thread::hardware_concurrency()));
		if (advanced.contains("Dump Shaders") && advanced["Dump Shaders"].is_boolean())
			shaderCache->SetDump(advanced["Dump Shaders"]);
		if (advanced.contains("Log Level") && advanced["Log Level"].is_number_integer()) {
			SetLogLevel(
				magic_enum::enum_cast<spdlog::level::level_enum>(advanced["Log Level"].get<int>())
					.value_or(spdlog::level::info));
		}
		if (advanced.contains("Shader Defines") && advanced["Shader Defines"].is_string())
			SetDefines(advanced["Shader Defines"]);
		if (advanced.contains("Compiler Threads") && advanced["Compiler Threads"].is_number_integer())
			shaderCache->compilationThreadCount = std::clamp(advanced["Compiler Threads"].get<int32_t>(), 1, maxCompilerThreads);
		if (advanced.contains("Background Compiler Threads") && advanced["Background Compiler Threads"].is_number_integer())
			shaderCache->backgroundCompilationThreadCount = std::clamp(advanced["Background Compiler Threads"].get<int32_t>(), 1, maxCompilerThreads);
		if (advanced.contains("Use FileWatcher") && advanced["Use FileWatcher"].is_boolean())
			shaderCache->SetFileWatcher(advanced["Use FileWatcher"]);
		if (advanced.contains("Frame Annotations") && advanced["Frame Annotations"].is_boolean())
			frameAnnotations = advanced["Frame Annotations"];
		if (advanced.contains("Refraction Scale") && advanced["Refraction Scale"].is_number())
			refractionScale = std::clamp(advanced["Refraction Scale"].get<float>(), 0.0f, 2.0f);
		if (advanced.contains("PBR Metal Reflection Scale") && advanced["PBR Metal Reflection Scale"].is_number())
			pbrMetalReflectionScale = std::clamp(advanced["PBR Metal Reflection Scale"].get<float>(), 0.0f, 2.0f);
		if (advanced.contains("PBR Metal Highlight Scale") && advanced["PBR Metal Highlight Scale"].is_number())
			pbrMetalHighlightScale = std::clamp(advanced["PBR Metal Highlight Scale"].get<float>(), 0.0f, 2.0f);
		if (advanced.contains("Partial Precision") && advanced["Partial Precision"].is_boolean())
			enablePartialPrecision.store(advanced["Partial Precision"].get<bool>(), std::memory_order_relaxed);
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

	if (a_loadFeatureSettings) {
		// Load feature settings (only for already-loaded features).
		auto* weatherRegistry =
			WeatherVariables::GlobalWeatherRegistry::GetSingleton();
		for (auto* feature : Feature::GetFeatureList()) {
			if (feature->loaded) {
				feature->Load(settings);
				if (feature->loaded) {
					weatherRegistry->CaptureFeatureUserSettings(
						feature->GetShortName());
				}
			}
		}
		WeatherManager::GetSingleton()->NotifyUserSettingsChanged();
	}
}

bool State::Save(ConfigMode a_configMode)
{
	const auto configPath = GetConfigPath(a_configMode);
	const auto configName = configPath.filename().string();
	const auto reportFailure = [&](std::string a_logMessage, std::string a_userMessage) {
		logger::warn("{}", a_logMessage);
		if (a_configMode == ConfigMode::USER && globals::menu)
			globals::menu->ReportSettingsSaveResult(false, std::move(a_userMessage));
		return false;
	};
	json settings = json::object();

	// Seed every save with its existing JSON so settings belonging to an unavailable
	// feature and unknown forward-compatible top-level sections survive, including in defaults.
	std::string readError;
	json existingSettings;
	const auto readResult = Util::FileHelpers::ReadJsonFile(configPath, existingSettings, readError);
	if (readResult == Util::FileHelpers::JsonFileReadResult::Success) {
		if (existingSettings.is_object()) {
			settings = std::move(existingSettings);
		} else {
			return reportFailure(
				std::format("Refusing to overwrite config which is not a JSON object: {}", configPath.string()),
				std::format(
					"Settings were not saved because the existing {} is not a JSON object. Fix or remove that file, then try again.",
					configName));
		}
	} else if (readResult == Util::FileHelpers::JsonFileReadResult::Error) {
		return reportFailure(
			std::format("Refusing to overwrite unreadable config {}: {}", configPath.string(), readError),
			std::format("Settings were not saved because {} could not be read: {}", configName, readError));
	}

	try {
		SaveToJson(settings, a_configMode == ConfigMode::DEFAULT);
	} catch (const std::exception& e) {
		return reportFailure(
			std::format("Failed to collect settings for {}: {}", configPath.string(), e.what()),
			std::format("Settings were not saved because the active values could not be collected: {}", e.what()));
	} catch (...) {
		return reportFailure(
			std::format("Failed to collect settings for {} due to an unknown error", configPath.string()),
			"Settings were not saved because the active values could not be collected. See CommunityShaders.log.");
	}

	std::string writeError;
	if (!SettingsSerialization::WriteFileAtomic(configPath, settings, writeError)) {
		return reportFailure(
			std::format("Failed to save settings to {}: {}", configPath.string(), writeError),
			std::format("Settings were not saved to {}: {}", configName, writeError));
	}

	if (a_configMode == ConfigMode::USER) {
		std::vector<std::string> postSaveFailures;
		for (auto* feature : Feature::GetFeatureList()) {
			if (!feature->loaded)
				continue;
			try {
				feature->OnSettingsSaved();
			} catch (const std::exception& e) {
				logger::warn("Post-save handling failed for {}: {}", feature->GetName(), e.what());
				postSaveFailures.push_back(feature->GetDisplayName());
			} catch (...) {
				logger::warn("Post-save handling failed for {} due to an unknown error", feature->GetName());
				postSaveFailures.push_back(feature->GetDisplayName());
			}
		}

		const auto overrideFailures = SaveUserOverrides(settings);
		if (!postSaveFailures.empty() || !overrideFailures.empty()) {
			std::string failureDetails;
			if (!overrideFailures.empty())
				failureDetails = std::format("override customizations for {}", JoinSettingLayerNames(overrideFailures));
			if (!postSaveFailures.empty()) {
				if (!failureDetails.empty())
					failureDetails += "; ";
				failureDetails += std::format("post-save handling for {}", JoinSettingLayerNames(postSaveFailures));
			}

			return reportFailure(
				std::format(
					"Settings save incomplete after writing {}: failed {}",
					configPath.string(), failureDetails),
				std::format(
					"{} was written, but {} could not be persisted. Changes remain marked unsaved; see CommunityShaders.log.",
					configName, failureDetails));
		}

		if (globals::menu) {
			globals::menu->ResetSettingsDirtyState();
			globals::menu->ReportSettingsSaveResult(
				true,
				std::format("Settings saved successfully to {}.", configName));
		}
	}

	logger::info("Saved settings to {}", configPath.string());
	return true;
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
	if (globals::game::isVR && a_level > spdlog::level::debug)
		Upscaling::DisableVRMenuPresentationTraceDiagnostics();

	logLevel = a_level;
	spdlog::set_level(logLevel);
	// Debug menu tracing can emit thousands of records per frame. Keep those
	// records enabled, but do not turn every Debug/Trace write into a synchronous
	// disk flush. Info and higher still flush promptly, while the trace session
	// boundaries explicitly flush their buffered diagnostic tail.
	const auto flushLevel = std::max(logLevel, spdlog::level::info);
	spdlog::flush_on(flushLevel);
	logger::info("Log Level set to {} ({})", magic_enum::enum_name(logLevel), magic_enum::enum_integer(logLevel));

	// Testers can enable debug logging after the D3D device was initialized.
	// Install the otherwise dormant trace hooks at that point as well.
	if (globals::game::isVR && IsDeveloperMode() && globals::d3d::context)
		Upscaling::InstallVRMenuPresentationTraceD3DHooks(globals::d3d::context);
}

spdlog::level::level_enum State::GetLogLevel()
{
	return logLevel;
}

void State::SetDefines(std::string a_defines)
{
	ShaderDefinesSnapshot snapshot;
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
		auto name = pystring::strip(token[0]);
		std::string definition;
		if (token.size() == 2) {
			definition = pystring::strip(token[1]);
		}
		if (!snapshot.canonicalText.empty())
			snapshot.canonicalText += ";";
		// Preserve the accepted user text exactly as before (apart from the
		// existing outer whitespace trim). The parsed pair remains normalized
		// for D3D macro ownership, while cache suffixes and saved settings do not
		// change merely because snapshotting became thread-safe.
		snapshot.canonicalText += cleanedDefine;
		snapshot.defines.emplace_back(std::move(name), std::move(definition));
	}

	auto immutableSnapshot =
		std::make_shared<const ShaderDefinesSnapshot>(std::move(snapshot));
	shaderDefinesSnapshot.store(immutableSnapshot, std::memory_order_release);
	shaderDefinesGeneration.fetch_add(1, std::memory_order_release);
	logger::debug("Shader Defines set to {}", immutableSnapshot->canonicalText);
}

std::shared_ptr<const State::ShaderDefinesSnapshot> State::GetShaderDefinesSnapshot() const
{
	return shaderDefinesSnapshot.load(std::memory_order_acquire);
}

bool State::ShaderEnabled(const RE::BSShader::Type a_type)
{
	auto index = magic_enum::enum_integer(a_type) + 1;
	if (index < sizeof(enabledClasses)) {
		return enabledClasses[index];
	}
	return false;
}

bool State::IsShaderEnabled(const RE::BSShader& a_shader)
{
	return ShaderEnabled(a_shader.shaderType.get());
}

bool State::IsDeveloperMode()
{
	return GetLogLevel() <= spdlog::level::debug;
}

void State::ModifyRenderTarget(RE::RENDER_TARGETS::RENDER_TARGET a_target, RE::BSGraphics::RenderTargetProperties* a_properties)
{
	// CDO4-001 phase 2, item 3, first half. Capture BEFORE the adjustment so the
	// A0->A1 edge has both sides, and record the invocation even when nothing
	// changes - NO_PROPERTY_CHANGE_REQUIRED needs a positive record, not an
	// absence. This is a PRE-CREATE property; the created resource is a separate
	// record and only that one can confirm anything.
	const std::uint32_t cdo4BeforeWidth = a_properties ? a_properties->width : 0u;
	const std::uint32_t cdo4BeforeHeight = a_properties ? a_properties->height : 0u;
	const bool cdo4Adjusted = globals::features::upscaling.AdjustVRRenderScaleRenderTargetProperties(a_target, a_properties);

	if (globals::features::upscaling.settings.cdo4Telemetry != 0u && a_properties) {
		CDO4Telemetry::Envelope env{};
		env.eventId = CDO4Telemetry::ReserveEventId();
		env.frame = globals::state ? static_cast<std::uint32_t>(globals::state->frameCount) : 0u;
		env.eye = CDO4Telemetry::kNoEye;
		env.payload = CDO4Telemetry::Payload::RenderTargetProperties;
		logger::info(
			"{} {{\"schema\":{},\"event\":{},\"parent\":0,\"frame\":{},\"cycle\":0,\"eye\":-1,"
			"\"planHash\":\"0000000000000000\",\"generation\":0,\"payload\":\"{}\","
			"\"target\":\"{}\",\"before\":{{\"w\":{},\"h\":{}}},\"after\":{{\"w\":{},\"h\":{}}},"
			"\"adjusted\":{},\"postCreate\":false}}",
			CDO4Telemetry::kPrefix, CDO4Telemetry::kSchemaVersion, env.eventId, env.frame,
			CDO4Telemetry::PayloadName(env.payload),
			magic_enum::enum_name(a_target),
			cdo4BeforeWidth, cdo4BeforeHeight,
			a_properties->width, a_properties->height,
			cdo4Adjusted ? "true" : "false");
		CDO4Telemetry::NoteEmitted();
	}

	if (cdo4Adjusted) {
		// This is the allocation, per target, read from the path that actually
		// rewrites what Skyrim will allocate - not from the resolution plan. That
		// makes it an independent check on the plan rather than a second printing
		// of it, which is why it is worth promoting on its own.
		//
		// Stock emits at debug, reachable only by enabling the whole debug
		// channel. With vrDiagGeometryLog set, emit the identical record at info.
		if (globals::features::upscaling.settings.vrDiagGeometryLog != 0u) {
			logger::info(
				"[Upscaling] Adjusted {} render target properties to {}x{} for VR render scale.",
				magic_enum::enum_name(a_target),
				a_properties->width,
				a_properties->height);
		} else {
			logger::debug(
				"[Upscaling] Adjusted {} render target properties to {}x{} for VR render scale.",
				magic_enum::enum_name(a_target),
				a_properties->width,
				a_properties->height);
		}
	}

	a_properties->supportUnorderedAccess = true;
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
		{ DXGI_FORMAT_R16G16B16A16_FLOAT, "R16G16B16A16_FLOAT", "Dynamic Cubemaps (HDR), Skylighting outProbeArray, Grass Collision (collisionTexture)" },
		{ DXGI_FORMAT_R16G16_UNORM, "R16G16_UNORM", "Terrain Shadows (RWTexShadowHeights)" },
		{ DXGI_FORMAT_R16G16_FLOAT, "R16G16_FLOAT", "VR Stereo Blend (kMOTION_VECTOR reprojection)" },
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
			"Consider disabling: Dynamic Cubemaps, Grass Collision, Terrain Shadows, Skylighting, HDR Display, VR Stereo Optimisations.");
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

	delete permutationCB;
	permutationCB = nullptr;
	delete sharedDataCB;
	sharedDataCB = nullptr;
	delete featureDataCB;
	featureDataCB = nullptr;
	pPerf = nullptr;
#ifdef TRACY_ENABLE
	if (tracyCtx) {
		TracyD3D11Destroy(tracyCtx);
		tracyCtx = nullptr;
	}
#endif

	permutationCB = new ConstantBuffer(ConstantBufferDesc<PermutationCB>());
	sharedDataCB = new ConstantBuffer(ConstantBufferDesc<SharedDataCB>());

	const auto featureDataSize = GetFeatureBufferData(false).second;
	featureDataCB = new ConstantBuffer(ConstantBufferDesc((uint32_t)featureDataSize));

	// Grab main texture to get resolution
	// VR cannot use viewport->screenWidth/Height as it's the desktop preview window's resolution and not HMD
	screenSize = GetMainRenderTargetSize();
	if (globals::d3d::context) {
		globals::d3d::context->QueryInterface(
			__uuidof(REX::W32::ID3DUserDefinedAnnotation),
			reinterpret_cast<void**>(pPerf.ReleaseAndGetAddressOf()));
	}

	if (globals::profiler && globals::d3d::device && globals::d3d::context) {
		globals::profiler->Initialize(globals::d3d::device, globals::d3d::context);
		if (frameAnnotations) {
			globals::profiler->SetPerfEventCallbacks(
				[this](std::string_view a_title) { BeginPerfEvent(a_title); },
				[this](std::string_view) { EndPerfEvent(); });
		} else {
			globals::profiler->SetPerfEventCallbacks({}, {});
		}
	}

	featureLevel = globals::d3d::device->GetFeatureLevel();
	setupResourcesDevice = globals::d3d::device;
	setupResourcesContext = globals::d3d::context;

	tracyCtx = TracyD3D11Context(globals::d3d::device, globals::d3d::context);
#ifdef TRACY_ENABLE
	Feature::SetTracyCtx(tracyCtx);
#endif
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
										(uint32_t)SIE::ShaderCache::LightingShaderFlags::TruePbr);

				a_pixelDescriptor &= ~((uint32_t)SIE::ShaderCache::LightingShaderFlags::AmbientSpecular |
									   (uint32_t)SIE::ShaderCache::LightingShaderFlags::ShadowDir |
									   (uint32_t)SIE::ShaderCache::LightingShaderFlags::DefShadow |
									   (uint32_t)SIE::ShaderCache::LightingShaderFlags::CharacterLight |
									   (uint32_t)SIE::ShaderCache::LightingShaderFlags::BaseObjectIsSnow);
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
		case RE::BSShader::Type::Grass:
			{
				auto technique = a_vertexDescriptor & 0xF;
				auto flags = a_vertexDescriptor & ~0xF;
				if (technique == static_cast<uint32_t>(SIE::ShaderCache::GrassShaderTechniques::TruePbr)) {
					technique = 0;
				}
				a_vertexDescriptor = flags | technique;
			}
			break;
		}
	}
}

namespace
{
	// Annotation labels are ASCII. Reusing the destination avoids a wide-string
	// allocation at every draw while preserving the existing byte-wise widening.
	const wchar_t* WidenAnnotation(std::wstring& a_buffer, std::string_view a_title)
	{
		a_buffer.resize(a_title.size());
		std::copy(a_title.begin(), a_title.end(), a_buffer.begin());
		return a_buffer.c_str();
	}
}

void State::BeginDrawEvent(std::string_view title)
{
	// Keep high-frequency draw annotations available to RenderDoc/PIX without
	// creating a dynamic Tracy source location for every geometry draw.
	if (pPerf.Get()) {
		thread_local std::wstring wideTitle;
		pPerf->BeginEvent(WidenAnnotation(wideTitle, title));
	}
}

void State::EndDrawEvent()
{
	if (pPerf.Get())
		pPerf->EndEvent();
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
	if (pPerf.Get()) {
		thread_local std::wstring wideTitle;
		pPerf->BeginEvent(WidenAnnotation(wideTitle, title));
	}
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
	if (pPerf.Get())
		pPerf->EndEvent();
}

void State::SetPerfMarker(std::string_view title)
{
	if (pPerf.Get()) {
		thread_local std::wstring wideMarker;
		pPerf->SetMarker(WidenAnnotation(wideMarker, title));
	}
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

		const auto shaderManager = globals::game::smState;
		const RE::NiTransform& dalcTransform = shaderManager->directionalAmbientTransform;

		auto shadowSceneNode = shaderManager->shadowSceneNode[0];
		auto dirLight = skyrim_cast<RE::NiDirectionalLight*>(shadowSceneNode->GetRuntimeData().sunLight->light.get());

		auto& lightRuntimeData = dirLight->GetLightRuntimeData();
		data.DirLightColor = { lightRuntimeData.diffuse.red, lightRuntimeData.diffuse.green, lightRuntimeData.diffuse.blue, 1.0f };
		data.DirLightColor *= lightRuntimeData.fade;

		if (const auto* imageSpaceManager = RE::ImageSpaceManager::GetSingleton())
			data.DirLightColor *= imageSpaceManager->GetImageSpaceData().baseData.hdr.sunlightScale;

		const auto& direction = dirLight->GetWorldDirection();
		data.DirLightDirection = { -direction.x, -direction.y, -direction.z, 0.0f };
		data.DirLightDirection.Normalize();

		data.CameraData = Util::GetCameraData();
		data.BufferDim = { screenSize.x, screenSize.y, 1.0f / screenSize.x, 1.0f / screenSize.y };
		data.Timer = timer;

		auto temporal = Util::GetTemporal();

		data.FrameCount = frameCount * temporal;
		data.FrameCountAlwaysActive = frameCount;

		if (a_inWorld) {
			for (int i = -2; i <= 2; i++) {
				for (int k = -2; k <= 2; k++) {
					int waterTile = (i + 2) + ((k + 2) * 5);
					data.WaterData[waterTile] = Util::TryGetWaterData((float)i * 4096.0f, (float)k * 4096.0f);
				}
			}
		}

		// Fallback water height for the VR analytical mask when tile 12 returns the sentinel.
		// Uses player->GetWaterHeight() (reads relevantWaterHeight from LOADED_REF_DATA,
		// then falls back to the cell water height) when it is valid.
		// Covers both interior water (where TES::GetWaterHeight returns -NI_INFINITY) and exterior
		// partial submersion. Stored as eye-0 camera-relative Z to match WaterData[].w.
		data.WaterSystemHeight = -RE::NI_INFINITY;
		if (globals::game::isVR) {
			if (auto player = RE::PlayerCharacter::GetSingleton()) {
				auto waterSystem = globals::game::waterSystem;
				const bool waterSystemInWater = waterSystem &&
				                                (waterSystem->playerUnderwater || waterSystem->partiallyUnderwater);
				float worldHeight = player->GetWaterHeight();
				if (worldHeight <= -RE::NI_INFINITY && waterSystemInWater) {
					worldHeight = waterSystem->underwaterHeight;
				}
				if (worldHeight > -RE::NI_INFINITY) {
					auto eye0Pos = Util::GetEyePosition(0);
					data.WaterSystemHeight = worldHeight - eye0Pos.z;
				}
			}
		}

		data.InInterior = Util::IsInterior();

		if (globals::game::sky)
			data.HideSky = globals::game::sky->flags.any(RE::Sky::Flags::kHideSky);
		else
			data.HideSky = false;

		data.InMapMenu = isMapMenuOpen;

		auto& upscaling = globals::features::upscaling;
		data.MipBias = upscaling.ResolveRuntimeMipBias(temporal);
		data.RefractionScale = refractionScale;
		data.PBRMetalReflectionScale = pbrMetalReflectionScale;
		data.PBRMetalHighlightScale = pbrMetalHighlightScale;
		data.HasDirectionalShadows = HasDirectionalShadows();
		const auto& volumetricShadows = globals::features::volumetricShadows;
		data.VolumetricShadowsEnabled = volumetricShadows.loaded && volumetricShadows.settings.Enabled;

		data.SSSHumanMaleIntensity = sssHumanMaleIntensity;
		data.SSSHumanMaleSaturation = sssHumanMaleSaturation;
		data.SSSHumanMaleBrightness = sssHumanMaleBrightness;
		data.SSSHumanMaleBaseSaturation = sssHumanMaleBaseSaturation;
		data.SSSHumanFemaleIntensity = sssHumanFemaleIntensity;
		data.SSSHumanFemaleSaturation = sssHumanFemaleSaturation;
		data.SSSHumanFemaleBrightness = sssHumanFemaleBrightness;
		data.SSSHumanFemaleBaseSaturation = sssHumanFemaleBaseSaturation;

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

		data.VRFoveationData0 = { FoveatedCommon::kCenterScaleMax, FoveatedCommon::kCenterFeather, 1.0f, FoveatedCommon::GetShaderMode(FoveatedCommon::DetailMode::Off) };
		data.VRFoveationModes = { 0.0f, 0.0f, 0.0f, 0.0f };
		data.VRFoveationCenterOffsets = { 0.0f, 0.0f, 0.0f, 0.0f };
		const auto& vr = globals::features::vr;
		const auto& dynamicCubemaps = globals::features::dynamicCubemaps;
		const auto& waterEffects = globals::features::waterEffects;
		const auto& wetnessEffects = globals::features::wetnessEffects;
		const auto& wetterness = globals::features::wetterness;
		const bool dynamicSSRActive = dynamicCubemaps.IsSSRRuntimeActive();
		const bool waterParallaxActive = vr.settings.EnableWaterParallaxFoveation && waterEffects.loaded;
		const bool wetnessEffectsActive = wetnessEffects.IsRuntimeActive();
		const bool wetternessFoveationActive =
			vr.settings.EnableWetternessFoveation &&
			wetterness.IsRuntimeProcessingActive() &&
			!wetnessEffectsActive;
		const bool anyFoveatedShaderDetailEnabled =
			vr.settings.EnableLightingFoveation ||
			(vr.settings.EnableSSRFoveation && dynamicSSRActive) ||
			waterParallaxActive ||
			wetternessFoveationActive;
		if (globals::game::isVR &&
			vr.loaded &&
			anyFoveatedShaderDetailEnabled &&
			upscaling.loaded) {
			const auto profile = upscaling.GetActiveUpscalingFoveatedProfile();
			if (profile.available) {
				const float centerScale = FoveatedCommon::ClampCenterScale(profile.sharedVisibleScale);
				const bool foveationActive = FoveatedCommon::IsActiveCoverage(centerScale);
				const float disabledFoveationMode = FoveatedCommon::GetShaderMode(FoveatedCommon::DetailMode::Off);
				const float lightingFoveationMode = FoveatedCommon::GetShaderMode(
					FoveatedCommon::GetDetailMode(vr.settings.EnableLightingFoveation, vr.settings.EnableLightingFoveationHardCutoff));
				const float ssrFoveationMode = FoveatedCommon::GetShaderMode(FoveatedCommon::GetDetailMode(
					vr.settings.EnableSSRFoveation && dynamicSSRActive,
					vr.settings.EnableSSRFoveationHardCutoff));
				const float waterParallaxFoveationMode = FoveatedCommon::GetShaderMode(FoveatedCommon::GetDetailMode(
					waterParallaxActive,
					vr.settings.EnableWaterParallaxFoveationHardCutoff));
				const float wetternessFoveationMode = FoveatedCommon::GetShaderMode(FoveatedCommon::GetDetailMode(
					wetternessFoveationActive,
					vr.settings.EnableWetternessFoveationHardCutoff));
				const float centerHorizontalScale = FoveatedCommon::ClampCenterHorizontalScale(profile.centerHorizontalScale);
				const float activeLightingMode = foveationActive ? lightingFoveationMode : disabledFoveationMode;
				data.VRFoveationData0 = { centerScale, FoveatedCommon::kCenterFeather, centerHorizontalScale, activeLightingMode };
				data.VRFoveationModes = {
					foveationActive ? ssrFoveationMode : disabledFoveationMode,
					foveationActive ? waterParallaxFoveationMode : disabledFoveationMode,
					foveationActive ? wetternessFoveationMode : disabledFoveationMode,
					disabledFoveationMode
				};
				data.VRFoveationCenterOffsets = {
					profile.centerOffsets[0].x,
					profile.centerOffsets[0].y,
					profile.centerOffsets[1].x,
					profile.centerOffsets[1].y
				};
			}
		}

		sharedDataCB->Update(data);
	}

	{
		const auto [data, size] = GetFeatureBufferData(a_inWorld);

		featureDataCB->Update(data, size);
	}

	auto* srv = Util::GetCurrentSceneDepthSRV(true);
	globals::d3d::context->PSSetShaderResources(17, 1, &srv);
}

void State::ClearDisabledFeatures()
{
	disabledFeatures.clear();
}

bool State::SetFeatureDisabled(const std::string& featureName, bool isDisabled)
{
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
	return disabledFeatures.contains(featureName) && disabledFeatures[featureName];
}

std::unordered_map<std::string, bool>& State::GetDisabledFeatures()
{
	return disabledFeatures;
}

// --- Utility Method Implementations ---

float State::GetTotalSmoothedDrawCalls() const
{
	return static_cast<float>(smoothDrawCalls[magic_enum::enum_integer(RE::BSShader::Type::Total)]);
}

bool State::HasDirectionalShadows() const
{
	if (!Util::IsInterior())
		return true;

	return globals::features::interiorSun.IsActiveInteriorSun();
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

void State::SaveTheme()
{
	// SelectedThemePreset is now persisted via SettingsUser.json (State::Save)
	// Keep this function as a no-op for backward compatibility and to avoid writing separate theme files.
	logger::info("SaveTheme() no longer writes SettingsTheme.json; SelectedThemePreset is saved with SettingsUser.json");
}
