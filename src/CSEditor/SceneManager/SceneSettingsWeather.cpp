#include "SceneSettingsManager.h"

#include "SceneSettingsInternal.h"

#include <algorithm>
#include <cmath>

using namespace SceneSettingsInternal;

RE::FormID SceneSettingsManager::GetEffectivePreviousWeatherId(const RE::Sky* sky, float weatherLerp) const
{
	if (!sky)
		return 0;
	if (weatherLerp >= 1.0f) {
		if (sky->currentWeather)
			cachedPreviousWeatherId = sky->currentWeather->GetFormID();
		return 0;
	}
	if (sky->lastWeather)
		cachedPreviousWeatherId = sky->lastWeather->GetFormID();
	return cachedPreviousWeatherId;
}

// --- Per-Weather Scene Settings ---

SceneSettingsManager::WeatherSceneConfig& SceneSettingsManager::GetWeatherConfigMut(RE::FormID weatherId)
{
	return weatherSceneConfigs[weatherId];
}

bool SceneSettingsManager::HasWeatherConfig(RE::FormID weatherId)
{
	if (!TryEnsureWeatherDataLoaded())
		return false;

	auto it = weatherSceneConfigs.find(weatherId);
	return it != weatherSceneConfigs.end() && std::any_of(it->second.entries.begin(), it->second.entries.end(),
												  [](const auto& entry) { return IsNumericValue(entry.value); });
}

void SceneSettingsManager::PrepareWeatherUserSettingsMutation(RE::FormID weatherId, bool replaceMalformedEntries)
{
	weatherUserSettingsModified = true;
	// Pinning the mode keeps a later preset from flipping which set the user's edits land in.
	if (auto configIt = weatherSceneConfigs.find(weatherId); configIt != weatherSceneConfigs.end())
		configIt->second.userTimeOfDayEnabled = configIt->second.timeOfDayEnabled;
	const auto canonicalSpid = Util::FormIdToSpid(weatherId);
	const auto normalizedSpid = NormalizeLocationFormKey(canonicalSpid);
	PrepareRawUserTarget(
		unresolvedWeatherUserSettings, canonicalSpid,
		[&](const std::string& rawSpid) { return NormalizeLocationFormKey(rawSpid) == normalizedSpid; },
		replaceMalformedEntries);
}

std::optional<float> SceneSettingsManager::ResolveWeatherLowerValue(RE::FormID weatherId,
	const SettingAddress& address, TimeOfDayPeriod period, EntrySource selectedSource)
{
	// A flat entry spans every period, so the time of day it sits on is the one playing now.
	const auto periodIndex = static_cast<int>(period == TimeOfDayPeriod::Count ? GetCurrentPeriod() : period);
	if (periodIndex < 0 || periodIndex >= kPeriodCount)
		return std::nullopt;
	auto baseline = GetBaselineValue(address);
	if (!IsNumericValue(baseline))
		return std::nullopt;
	const auto baselineValue = baseline.get<float>();
	if (!std::isfinite(baselineValue))
		return std::nullopt;

	float lowerValue = GetTimeOfDayPeriodFallbackFloat(baselineValue,
		address.featureShortName, address.settingPath, address.settingKey, periodIndex);
	// Only a user entry has the weather overwrite layer beneath it. A capture deliberately passes
	// the overwrite layer here so it resolves to the value that applies without any mod.
	if (selectedSource != EntrySource::User)
		return lowerValue;

	auto configIt = weatherSceneConfigs.find(weatherId);
	if (configIt == weatherSceneConfigs.end())
		return lowerValue;
	for (const auto& entry : configIt->second.entries) {
		if (entry.source != EntrySource::Overwrite || entry.period != period || !IsEntryActive(entry) ||
			!IsNumericValue(entry.value) ||
			!IsSameSetting(entry, address.featureShortName, address.settingPath, address.settingKey))
			continue;
		const auto value = entry.value.get<float>();
		if (std::isfinite(value))
			lowerValue = value;
	}
	return lowerValue;
}

void SceneSettingsManager::RemoveWeatherSetting(RE::FormID weatherId, size_t index)
{
	if (!TryEnsureWeatherDataLoaded())
		return;

	if (auto it = weatherSceneConfigs.find(weatherId); it != weatherSceneConfigs.end())
		RemovePeriodicEntry(it->second, index, [this, weatherId] { PrepareWeatherUserSettingsMutation(weatherId, false); });
}

void SceneSettingsManager::RemovePeriodicEntry(PeriodicSceneConfig& config, size_t index,
	const std::function<void()>& prepareUserEdit)
{
	if (index >= config.entries.size())
		return;
	assert(config.entries[index].source == EntrySource::User && "a mod's entry is suppressed with a tombstone, never removed");
	config.entries.erase(config.entries.begin() + static_cast<ptrdiff_t>(index));
	prepareUserEdit();
	SaveAllUserSettings();
	BumpEntryPresentationRevision();
	ReapplyIfActive();
}

bool SceneSettingsManager::HasWeatherEntryForPeriod(RE::FormID weatherId, const std::string& featureShortName,
	const std::vector<std::string>& settingPath, const std::string& settingKey, TimeOfDayPeriod period, std::optional<EntrySource> source)
{
	if (!TryEnsureWeatherDataLoaded())
		return false;

	auto it = weatherSceneConfigs.find(weatherId);
	return it != weatherSceneConfigs.end() &&
	       HasEntryForPeriod(it->second.entries, featureShortName, settingPath, settingKey, period, source);
}

// --- Time of Day Mode ---

bool SceneSettingsManager::IsSceneTimeOfDayEnabled(const SceneContextId& context) const
{
	switch (context.type) {
	case SceneContextType::TimeOfDay:
		return true;
	case SceneContextType::Weather:
		{
			auto it = weatherSceneConfigs.find(context.weatherId);
			return it != weatherSceneConfigs.end() && it->second.timeOfDayEnabled;
		}
	case SceneContextType::Location:
		return GetLocationConfig(context.locationType, context.locationFormKey).timeOfDayEnabled;
	default:
		return false;
	}
}

void SceneSettingsManager::SetSceneTimeOfDayEnabled(const SceneContextId& context, bool enabled)
{
	if (IsSceneTimeOfDayEnabled(context) == enabled)
		return;

	PeriodicSceneConfig* config = nullptr;
	if (context.type == SceneContextType::Weather && context.weatherId && TryEnsureWeatherDataLoaded()) {
		PrepareWeatherUserSettingsMutation(context.weatherId, false);
		config = &GetWeatherConfigMut(context.weatherId);
	} else if (context.type == SceneContextType::Location && !context.locationFormKey.empty() &&
			   TryEnsureLocationDataLoaded()) {
		PrepareLocationUserSettingsMutation(context.locationType, context.locationFormKey, false);
		const auto& existing = GetLocationConfig(context.locationType, context.locationFormKey);
		const auto name = existing.name;
		const auto cocCode = existing.cocCode;
		config = &EnsureAuthoredLocationConfig(context.locationType, context.locationFormKey, name, cocCode);
	}
	if (!config)
		return;

	config->userTimeOfDayEnabled = enabled;
	config->timeOfDayEnabled = enabled;
	// The mode picks which saved set resolves, so it changes live values like an entry edit would.
	activeEntryCacheDirty = true;
	BumpEntryPresentationRevision();
	CommitSceneSettingChanges();
}

void SceneSettingsManager::RefreshTimeOfDayModes()
{
	for (auto& [weatherId, config] : weatherSceneConfigs)
		config.RefreshTimeOfDayMode();
	for (auto& [configKey, config] : locationSceneConfigs)
		config.RefreshTimeOfDayMode();
}
