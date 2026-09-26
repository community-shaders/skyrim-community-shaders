#include "SceneSettingsManager.h"

#include "Globals.h"
#include "SceneSettingsInternal.h"
#include "State.h"
#include "Utils/Game.h"

#include <algorithm>
#include <set>

using namespace SceneSettingsInternal;

// --- Debug Inspection ---

/** @brief Renders a stored setting value as a short debug string. */
static std::string FormatDebugValue(const json& value)
{
	if (value.is_null())
		return "-";
	return value.is_string() ? value.get<std::string>() : value.dump();
}

/** @brief Display name for a form, or "-" when the id is unset. */
static std::string FormatDebugFormName(RE::FormID formId)
{
	return formId ? Util::GetFormDisplayName(formId) : "-";
}

SceneSettingsManager::DebugSnapshot SceneSettingsManager::GetDebugSnapshot() const
{
	DebugSnapshot snapshot;

	snapshot.dataLoaded = dataLoaded;
	snapshot.weatherDataLoaded = weatherDataLoaded;
	snapshot.locationDataLoaded = locationDataLoaded;
	snapshot.gameDataReady = gameDataReady;
	snapshot.resolverSuspended = resolverSuspended;
	snapshot.resolverDirty = resolverDirty;
	snapshot.activeEntryCacheDirty = activeEntryCacheDirty;
	snapshot.hasActiveSceneEntries = hasActiveSceneEntries;
	snapshot.deferredSceneChangesPending = deferredSceneChangesPending;
	snapshot.sceneLayerSuspendDepth = sceneLayerSuspendDepth;

	snapshot.lastInterior = lastResolvedInterior;
	snapshot.lastCellId = lastResolvedCellId;
	snapshot.lastLocationId = lastResolvedLocationId;
	snapshot.lastHour = lastResolvedHour;
	snapshot.lastWeather = { lastResolvedCurrentWeatherId, lastResolvedPreviousWeatherId, lastResolvedWeatherLerp };
	snapshot.blendFactors = blendSnapshot.timeOfDayFactors;

	snapshot.menuOpen = globals::state && globals::state->IsMainOrLoadingMenuOpen();
	snapshot.interior = Util::IsInterior();
	snapshot.gameHour = GetCurrentGameHour();
	snapshot.period = GetCurrentPeriod();
	snapshot.timeOfDayFactors = GetTimeOfDayFactors();

	// Sampled live rather than from the resolver, so an interior still shows what the sky is doing.
	// Observing must not advance the resolver's weather tracking, which an interior would not.
	const auto trackedPreviousWeatherId = cachedPreviousWeatherId;
	snapshot.weather = GetWeatherBlend();
	cachedPreviousWeatherId = trackedPreviousWeatherId;
	snapshot.currentWeatherName = FormatDebugFormName(snapshot.weather.currentWeatherId);
	snapshot.previousWeatherName = FormatDebugFormName(snapshot.weather.previousWeatherId);

	auto* player = RE::PlayerCharacter::GetSingleton();
	auto* cell = player ? player->GetParentCell() : nullptr;
	snapshot.playerReady = player && cell;
	if (cell) {
		snapshot.cellId = cell->GetFormID();
		snapshot.cellName = FormatDebugFormName(snapshot.cellId);
		snapshot.cellEditorId = Util::GetFormEditorID(cell);
	}
	auto* location = player ? player->GetCurrentLocation() : nullptr;
	if (!location && cell)
		location = cell->GetLocation();
	if (location) {
		snapshot.locationId = location->GetFormID();
		snapshot.locationName = FormatDebugFormName(snapshot.locationId);
	}
	snapshot.locationTargets = GetCurrentLocationTargets();

	const auto buildEntries = [&](const std::vector<SettingEntry>& sourceEntries, SceneType type) {
		std::vector<DebugEntry> debugEntries;
		debugEntries.reserve(sourceEntries.size());
		for (const auto& entry : sourceEntries) {
			debugEntries.push_back({
				.feature = entry.featureShortName,
				.path = JoinDisplayParts(entry.settingPath, {}),
				.key = entry.settingKey,
				.value = FormatDebugValue(entry.value),
				.period = entry.period == TimeOfDayPeriod::Count ? std::string() : GetPeriodName(entry.period),
				.transitionSeconds = entry.transitionSeconds,
				.overwrite = entry.source == EntrySource::Overwrite,
				.paused = entry.paused,
				.active = IsEntryActive(entry),
				.resolvable = IsResolvableEntry(entry, type),
			});
		}
		return debugEntries;
	};

	for (auto type : { SceneType::InteriorOnly, SceneType::TimeOfDay }) {
		snapshot.sceneLayers.push_back({
			.name = GetSceneTypeName(type),
			.matchesCurrentScene = type == (snapshot.interior ? SceneType::InteriorOnly : SceneType::TimeOfDay),
			.entries = buildEntries(GetEntries(type), type),
		});
	}

	for (const auto& [weatherId, config] : weatherSceneConfigs) {
		snapshot.weatherLayers.push_back({
			.name = FormatDebugFormName(weatherId),
			.detail = std::format("{:08X}", weatherId),
			.matchesCurrentScene = weatherId == snapshot.weather.currentWeatherId ||
		                           weatherId == snapshot.weather.previousWeatherId,
			.entries = buildEntries(config.entries, SceneType::TimeOfDay),
		});
	}

	std::set<std::string> activeLocationKeys;
	for (const auto& target : snapshot.locationTargets)
		activeLocationKeys.insert(GetLocationConfigKey(target.type, target.formKey));
	for (const auto& [configKey, config] : locationSceneConfigs) {
		snapshot.locationLayers.push_back({
			.name = config.name.empty() ? configKey : config.name,
			.detail = configKey,
			.matchesCurrentScene = activeLocationKeys.contains(configKey),
			.entries = buildEntries(config.entries, SceneType::Location),
		});
	}

	const auto periodValuesFor = [](const PeriodSettingMap& values, const SettingAddress& address) {
		auto it = values.find(address);
		return it != values.end() ? it->second : PeriodValues{};
	};
	// Each group build is revision-gated, but they are loop-invariant, so hoist the lookups too.
	const auto& timeOfDayValues = BuildTimeOfDayValueGroups();
	const auto& currentWeatherValues = BuildWeatherValueGroups(snapshot.weather.currentWeatherId);
	const auto& previousWeatherValues = BuildWeatherValueGroups(snapshot.weather.previousWeatherId);
	for (const auto& [address, value] : appliedSettings) {
		DebugResolvedSetting resolvedSetting{
			.feature = address.featureShortName,
			.path = JoinDisplayParts(address.settingPath, {}),
			.key = address.settingKey,
			.baseline = "-",
			.applied = FormatDebugValue(value),
		};
		if (auto baselineIt = baselineSettings.find(address); baselineIt != baselineSettings.end())
			resolvedSetting.baseline = FormatDebugValue(baselineIt->second);
		resolvedSetting.timeOfDayValues = periodValuesFor(timeOfDayValues, address);
		resolvedSetting.currentWeatherValues = periodValuesFor(currentWeatherValues, address);
		resolvedSetting.previousWeatherValues = periodValuesFor(previousWeatherValues, address);
		snapshot.resolvedSettings.push_back(std::move(resolvedSetting));
	}

	snapshot.transitionTime = GetPauseAwareTime();
	snapshot.lastTransitionTick = lastLocationTransitionTick;
	snapshot.globalTransitionSeconds = locationTransitionSeconds;
	snapshot.transitionBatchesDirty = locationTransitionBatchesDirty;
	snapshot.transitionBatchCount = locationTransitionBatches.size();
	for (const auto& [address, transition] : activeLocationTransitions) {
		const auto elapsed = snapshot.transitionTime - transition.startTime;
		snapshot.locationTransitions.push_back({
			.feature = address.featureShortName,
			.path = JoinDisplayParts(address.settingPath, {}),
			.key = address.settingKey,
			.startValue = transition.startValue,
			.targetValue = transition.targetValue,
			.currentValue = EaseLocationTransition(transition, snapshot.transitionTime),
			.progress = transition.duration > 0.0f ? std::clamp(elapsed / transition.duration, 0.0f, 1.0f) : 1.0f,
			.duration = transition.duration,
			.restoreAtEnd = transition.restoreAtEnd,
		});
	}
	for (const auto& [featureShortName, _] : transitionApplyFailures)
		snapshot.transitionApplyFailures.push_back(featureShortName);

	for (const auto& [featureShortName, _] : applyFailures)
		snapshot.applyFailures.push_back(featureShortName);
	snapshot.restoreFailures.assign(restoreFailureWarnings.begin(), restoreFailureWarnings.end());
	for (const auto& [featureShortName, paused] : featurePauseStates)
		if (paused)
			snapshot.pausedFeatures.push_back(featureShortName);

	return snapshot;
}
