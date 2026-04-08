#include "SceneSettingsManager.h"

#include "Feature.h"
#include "Globals.h"
#include "State.h"
#include "Utils/FileSystem.h"
#include "Utils/Game.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <unordered_set>

// --- Path Resolution ---

std::string SceneSettingsManager::GetSceneTypeName(SceneType type)
{
	switch (type) {
	case SceneType::InteriorOnly:
		return "InteriorOnly";
	case SceneType::TimeOfDay:
		return "TimeOfDay";
	default:
		return "Unknown";
	}
}

std::filesystem::path SceneSettingsManager::GetSettingsFilePath(SceneType type)
{
	return Util::PathHelpers::GetSceneSettingsPath() / (GetSceneTypeName(type) + ".json");
}

std::filesystem::path SceneSettingsManager::GetOverwritesPath(SceneType type)
{
	return Util::PathHelpers::GetSceneSettingsPath() / GetSceneTypeName(type);
}

// --- Time of Day Period Helpers ---

const char* SceneSettingsManager::GetPeriodName(TimeOfDayPeriod period)
{
	int idx = static_cast<int>(period);
	return (idx >= 0 && idx < kPeriodCount) ? kPeriodNames[idx] : "Unknown";
}

SceneSettingsManager::TimeOfDayPeriod SceneSettingsManager::GetPeriodFromName(const std::string& name)
{
	for (int i = 0; i < kPeriodCount; ++i) {
		if (name == GetPeriodName(static_cast<TimeOfDayPeriod>(i)))
			return static_cast<TimeOfDayPeriod>(i);
	}
	return TimeOfDayPeriod::Count;
}

float SceneSettingsManager::GetCurrentGameHour()
{
	// Prefer calendar (ground truth), which the Weather Editor slider writes to.
	// sky->currentGameHour may lag when timeScale is 0 (time paused).
	auto calendar = globals::game::calendar ? globals::game::calendar : RE::Calendar::GetSingleton();
	float hour = 12.0f;
	if (calendar && calendar->gameHour)
		hour = calendar->gameHour->value;
	else if (auto sky = globals::game::sky)
		hour = sky->currentGameHour;

	// Normalize into [0, 24) so midnight is 0 and never 24.
	hour = std::clamp(hour, 0.0f, 24.0f);
	if (hour >= 24.0f)
		hour = 0.0f;
	return hour;
}

void SceneSettingsManager::GetTimeOfDayFactors(float outFactors[kPeriodCount])
{
	for (int i = 0; i < kPeriodCount; ++i)
		outFactors[i] = 0.0f;

	float hour = GetCurrentGameHour();

	// Normalize to [0, 24) — Night wraps, so also check hour + 24 for pre-dawn hours
	for (int i = 0; i < kPeriodCount; ++i) {
		float start = kPeriodHours[i][0];
		float end = kPeriodHours[i][1];
		float h = (end > 24.0f && hour < start) ? hour + 24.0f : hour;

		if (h >= start && h < end) {
			// Inside this period — check if we're in the blend-out zone near the end.
			float distFromEnd = end - h;

			if (distFromEnd < kTransitionHours) {
				// Blending out to next period
				float t = distFromEnd / kTransitionHours;
				outFactors[i] = t;
				outFactors[(i + 1) % kPeriodCount] = 1.0f - t;
			} else {
				outFactors[i] = 1.0f;
			}
			return;
		}
	}

	// Fallback: noon = Day
	outFactors[static_cast<int>(TimeOfDayPeriod::Day)] = 1.0f;
}

SceneSettingsManager::TimeOfDayPeriod SceneSettingsManager::GetDominantPeriod()
{
	float factors[kPeriodCount];
	GetTimeOfDayFactors(factors);

	int best = 0;
	for (int i = 1; i < kPeriodCount; ++i)
		if (factors[i] > factors[best])
			best = i;
	return static_cast<TimeOfDayPeriod>(best);
}

SceneSettingsManager::TimeOfDayPeriod SceneSettingsManager::GetCurrentPeriod()
{
	float hour = GetCurrentGameHour();
	for (int i = 0; i < kPeriodCount; ++i) {
		float start = kPeriodHours[i][0];
		float end = kPeriodHours[i][1];
		float h = (end > 24.0f && hour < start) ? hour + 24.0f : hour;
		if (h >= start && h < end)
			return static_cast<TimeOfDayPeriod>(i);
	}
	return TimeOfDayPeriod::Day;
}

// --- Feature Metadata (static helpers, zero coupling) ---

static std::vector<std::string> FilterFeatureNames(const std::unordered_set<std::string>& whitelist)
{
	auto allNames = Feature::GetLoadedFeatureNames();
	std::vector<std::string> filtered;
	filtered.reserve(allNames.size());
	for (auto& name : allNames)
		if (whitelist.contains(name))
			filtered.push_back(std::move(name));
	return filtered;
}

std::vector<std::string> SceneSettingsManager::GetInteriorRelevantFeatureNames()
{
	static const std::unordered_set<std::string> whitelist = {
		"ScreenSpaceGI",
		"ScreenSpaceShadows",
		"SubsurfaceScattering",
		"LinearLighting",
		"ImageBasedLighting",
		"PostProcessing",
		"ScreenSpacePointLightShadows",
		"ScreenSpaceRayTracing",
		"VanillaFresnel",
	};
	return FilterFeatureNames(whitelist);
}

std::vector<std::string> SceneSettingsManager::GetExteriorRelevantFeatureNames()
{
	// NOTE: ScreenSpaceGI excluded — its LoadSettings() unconditionally triggers
	// synchronous recompilation of 6 compute shaders, causing massive lag.
	static const std::unordered_set<std::string> whitelist = {
		"CloudShadows",
		"ExponentialHeightFog",
		"GrassLighting",
		"ImageBasedLighting",
		"LinearLighting",
		"Skylighting",
		"SubsurfaceScattering",
		"TerrainShadows",
		"WetnessEffects",
	};
	return FilterFeatureNames(whitelist);
}

std::string SceneSettingsManager::GetFeatureDisplayName(const std::string& featureShortName)
{
	auto* feature = Feature::FindFeatureByShortName(featureShortName);
	return feature ? feature->GetName() : featureShortName;
}

std::vector<std::string> SceneSettingsManager::GetFeatureSettingKeys(const std::string& featureShortName)
{
	std::vector<std::string> keys;
	auto* feature = Feature::FindFeatureByShortName(featureShortName);
	if (!feature)
		return keys;

	json settings;
	feature->SaveSettings(settings);
	if (!settings.is_object())
		return keys;

	for (auto& [key, _] : settings.items())
		keys.push_back(key);

	std::sort(keys.begin(), keys.end());
	return keys;
}

json SceneSettingsManager::GetFeatureSettingValue(const std::string& featureShortName, const std::string& settingKey)
{
	auto* feature = Feature::FindFeatureByShortName(featureShortName);
	if (!feature)
		return {};

	json settings;
	feature->SaveSettings(settings);
	if (settings.is_object() && settings.contains(settingKey))
		return settings[settingKey];

	return {};
}

SceneSettingsManager::SettingType SceneSettingsManager::DetectSettingType(const json& value)
{
	if (value.is_boolean())
		return SettingType::Boolean;
	if (value.is_number_integer())
		return SettingType::Integer;
	if (value.is_number_float())
		return SettingType::Float;
	if (value.is_string())
		return SettingType::String;
	return SettingType::Unknown;
}

std::vector<std::string> SceneSettingsManager::GetTransitionableSettingKeys(const std::string& featureShortName)
{
	auto* feature = Feature::FindFeatureByShortName(featureShortName);
	if (!feature)
		return {};

	json settings;
	feature->SaveSettings(settings);
	if (!settings.is_object())
		return {};

	std::vector<std::string> keys;
	for (auto& [key, val] : settings.items()) {
		if (DetectSettingType(val) == SettingType::Float)
			keys.push_back(key);
	}
	std::sort(keys.begin(), keys.end());
	return keys;
}

// --- Generic Entry Management ---

std::vector<SceneSettingsManager::SettingEntry>& SceneSettingsManager::GetEntriesMut(SceneType type)
{
	return entries[type];
}

const std::vector<SceneSettingsManager::SettingEntry>& SceneSettingsManager::GetEntries(SceneType type) const
{
	static const std::vector<SettingEntry> empty;
	auto it = entries.find(type);
	return (it != entries.end()) ? it->second : empty;
}

bool SceneSettingsManager::IsEntryActive(const SettingEntry& entry) const
{
	return !entry.paused && !IsFeaturePaused(entry.featureShortName);
}

bool SceneSettingsManager::HasActiveEntries(SceneType type) const
{
	for (const auto& entry : GetEntries(type)) {
		if (IsEntryActive(entry))
			return true;
	}
	return false;
}

bool SceneSettingsManager::HasEntryFromSource(SceneType type, const std::string& featureShortName, const std::string& settingKey, EntrySource source) const
{
	for (const auto& entry : GetEntries(type)) {
		if (entry.source == source && entry.featureShortName == featureShortName && entry.settingKey == settingKey)
			return true;
	}
	return false;
}

bool SceneSettingsManager::HasEntryForPeriod(const std::string& featureShortName, const std::string& settingKey,
	TimeOfDayPeriod period, EntrySource source) const
{
	for (const auto& entry : GetEntries(SceneType::TimeOfDay)) {
		if (entry.source == source && entry.period == period &&
			entry.featureShortName == featureShortName && entry.settingKey == settingKey)
			return true;
	}
	return false;
}

bool SceneSettingsManager::HasActiveOverwrite(SceneType type, const std::string& featureShortName, const std::string& settingKey) const
{
	for (const auto& entry : GetEntries(type)) {
		if (entry.source == EntrySource::Overwrite && !entry.paused &&
			entry.featureShortName == featureShortName && entry.settingKey == settingKey)
			return true;
	}
	return false;
}

bool SceneSettingsManager::HasDuplicateEntry(SceneType type, const std::string& featureShortName,
	const std::string& settingKey, EntrySource source, TimeOfDayPeriod period) const
{
	if (type == SceneType::TimeOfDay)
		return HasEntryForPeriod(featureShortName, settingKey, period, source);
	return HasEntryFromSource(type, featureShortName, settingKey, source);
}

void SceneSettingsManager::AddSetting(SceneType type, const std::string& featureShortName, const std::string& settingKey, const json& value,
	TimeOfDayPeriod period)
{
	if (type == SceneType::TimeOfDay) {
		// Reject invalid period values (Count is the sentinel, not a real period)
		if (period == TimeOfDayPeriod::Count || static_cast<int>(period) < 0 || static_cast<int>(period) >= kPeriodCount) {
			logger::warn("[SceneSettings] Rejecting TOD setting with invalid period: {}.{}", featureShortName, settingKey);
			return;
		}

		// TOD only supports float settings (smooth interpolation)
		if (DetectSettingType(value) != SettingType::Float) {
			logger::warn("[SceneSettings] Rejecting non-float TOD setting: {}.{}", featureShortName, settingKey);
			return;
		}

		// Reject non-finite values (NaN/Inf) to prevent unstable blending
		if (!std::isfinite(value.get<float>())) {
			logger::warn("[SceneSettings] Rejecting non-finite TOD value for {}.{}", featureShortName, settingKey);
			return;
		}
	}

	if (HasDuplicateEntry(type, featureShortName, settingKey, EntrySource::User, period))
		return;

	auto& vec = GetEntriesMut(type);

	SettingEntry entry;
	entry.featureShortName = featureShortName;
	entry.settingKey = settingKey;
	entry.value = value;
	entry.originalValue = value;
	entry.source = EntrySource::User;
	entry.period = period;
	vec.push_back(std::move(entry));
	SaveUserSettings(type);
	ReapplyIfActive();
}

void SceneSettingsManager::RemoveSetting(SceneType type, size_t index)
{
	auto& vec = GetEntriesMut(type);
	if (index >= vec.size())
		return;

	auto& entry = vec[index];
	if (entry.source == EntrySource::Overwrite && !entry.sourceFilename.empty()) {
		// For TimeOfDay overwrites, files are in period subfolders
		auto basePath = GetOverwritesPath(type);
		auto filepath = (type == SceneType::TimeOfDay && entry.period != TimeOfDayPeriod::Count) ? basePath / GetPeriodName(entry.period) / entry.sourceFilename : basePath / entry.sourceFilename;
		std::error_code ec;
		bool removed = std::filesystem::remove(filepath, ec);
		if (removed) {
			logger::info("[SceneSettings] Deleted overwrite file: {}", filepath.string());
		} else if (ec && ec.value() != 0) {
			// Real I/O error — keep in-memory entry so the overwrite stays active
			logger::error("[SceneSettings] Failed to delete overwrite file: {} ({}) — keeping entry", filepath.string(), ec.message());
			return;
		}
		// ec.value()==0 && !removed means file didn't exist — safe to drop entry
	}

	logger::info("[SceneSettings] Removed {} entry: {}.{} (source={})", GetSceneTypeName(type),
		entry.featureShortName, entry.settingKey,
		entry.source == EntrySource::Overwrite ? "overwrite" : "user");

	vec.erase(vec.begin() + static_cast<ptrdiff_t>(index));
	SaveUserSettings(type);
	ReapplyIfActive();
}

void SceneSettingsManager::TogglePauseEntry(SceneType type, size_t index)
{
	auto& vec = GetEntriesMut(type);
	if (index < vec.size()) {
		vec[index].paused = !vec[index].paused;
		ReapplyIfActive();
	}
}

void SceneSettingsManager::RevertEntryToDefault(SceneType type, size_t index)
{
	auto& vec = GetEntriesMut(type);
	if (index >= vec.size())
		return;
	auto& entry = vec[index];
	if (!entry.originalValue.is_null()) {
		entry.value = entry.originalValue;
		SaveUserSettings(type);
		ReapplyIfActive();
	}
}

bool SceneSettingsManager::HasOverwriteEntries(SceneType type) const
{
	for (const auto& entry : GetEntries(type))
		if (entry.source == EntrySource::Overwrite)
			return true;
	return false;
}

void SceneSettingsManager::SetAllOverwritesPaused(SceneType type, bool paused)
{
	allOverwritesPausedMap[type] = paused;
	for (auto& entry : GetEntriesMut(type))
		if (entry.source == EntrySource::Overwrite)
			entry.paused = paused;
	ReapplyIfActive();
}

bool SceneSettingsManager::AreAllOverwritesPaused(SceneType type) const
{
	auto it = allOverwritesPausedMap.find(type);
	return it != allOverwritesPausedMap.end() && it->second;
}

void SceneSettingsManager::DeleteAllOverwrites(SceneType type)
{
	auto overwritesPath = GetOverwritesPath(type);

	auto& vec = GetEntriesMut(type);

	// Track which overwrite entries had their files successfully removed (or already absent).
	// Entries whose disk delete fails are kept in memory so they stay visible for retry.
	std::vector<bool> shouldErase(vec.size(), false);
	for (size_t i = 0; i < vec.size(); ++i) {
		const auto& entry = vec[i];
		if (entry.source != EntrySource::Overwrite)
			continue;
		if (entry.sourceFilename.empty()) {
			// No backing file — safe to drop
			shouldErase[i] = true;
			continue;
		}
		auto filepath = (type == SceneType::TimeOfDay && entry.period != TimeOfDayPeriod::Count) ? overwritesPath / GetPeriodName(entry.period) / entry.sourceFilename : overwritesPath / entry.sourceFilename;
		std::error_code ec;
		bool removed = std::filesystem::remove(filepath, ec);
		if (removed || (!ec || ec.value() == 0)) {
			// File deleted or already absent — mark for in-memory removal
			shouldErase[i] = true;
		} else {
			logger::error("[SceneSettings] Failed to delete overwrite file: {} ({}) — keeping entry", filepath.string(), ec.message());
		}
	}

	// Erase only entries whose backing files were successfully cleaned up
	// (iterate in reverse to preserve index validity)
	for (size_t i = vec.size(); i-- > 0;) {
		if (shouldErase[i])
			vec.erase(vec.begin() + static_cast<ptrdiff_t>(i));
	}

	allOverwritesPausedMap[type] = false;
	ReapplyIfActive();
}

void SceneSettingsManager::SetAllUserPaused(SceneType type, bool paused)
{
	allUserPausedMap[type] = paused;
	for (auto& entry : GetEntriesMut(type))
		if (entry.source == EntrySource::User)
			entry.paused = paused;
	ReapplyIfActive();
}

bool SceneSettingsManager::AreAllUserPaused(SceneType type) const
{
	auto it = allUserPausedMap.find(type);
	return it != allUserPausedMap.end() && it->second;
}

void SceneSettingsManager::DeleteAllUserSettings(SceneType type)
{
	auto& vec = GetEntriesMut(type);
	std::erase_if(vec, [](const SettingEntry& e) {
		return e.source == EntrySource::User;
	});

	allUserPausedMap[type] = false;
	SaveUserSettings(type);
	ReapplyIfActive();
}

void SceneSettingsManager::UpdateEntryValue(SceneType type, size_t index, const json& newValue, bool deferSave)
{
	auto& vec = GetEntriesMut(type);
	if (index >= vec.size())
		return;

	// Enforce float-only invariant for TimeOfDay entries
	if (type == SceneType::TimeOfDay) {
		if (!newValue.is_number()) {
			logger::warn("[SceneSettings] UpdateEntryValue: rejecting non-numeric TOD value for {}.{}",
				vec[index].featureShortName, vec[index].settingKey);
			return;
		}
		// Normalize to float so DetectSettingType sees Float, not Integer
		float floatVal = newValue.get<float>();
		if (!std::isfinite(floatVal)) {
			logger::warn("[SceneSettings] UpdateEntryValue: rejecting non-finite TOD value ({}) for {}.{}",
				floatVal, vec[index].featureShortName, vec[index].settingKey);
			return;
		}
		vec[index].value = floatVal;
	} else {
		vec[index].value = newValue;
	}

	if (!deferSave && vec[index].source == EntrySource::User)
		SaveUserSettings(type);

	// For TimeOfDay, recompute blended values; for others, apply directly
	if (type == SceneType::TimeOfDay) {
		if (isTimeOfDayActive) {
			// Reset the hour throttle so a user edit (e.g. slider drag) is
			// applied immediately rather than waiting for the game clock to advance.
			lastBlendedHour = -1.0f;
			ApplyTimeOfDayBlended();
		}
	} else if (isCurrentlyApplied && !vec[index].paused && !IsFeaturePaused(vec[index].featureShortName)) {
		if (vec[index].source == EntrySource::Overwrite ||
			!HasActiveOverwrite(type, vec[index].featureShortName, vec[index].settingKey))
			ApplySettingToFeature(vec[index]);
	}
}

// --- Event Handler ---

RE::BSEventNotifyControl SceneSettingsManager::MenuOpenCloseEventHandler::ProcessEvent(
	const RE::MenuOpenCloseEvent* a_event,
	RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	if (a_event && a_event->menuName == RE::LoadingMenu::MENU_NAME && !a_event->opening) {
		// Defer cell transition to next frame — cell data isn't available yet
		// when this event fires. Same pattern as Skylighting::queuedResetSkylighting.
		GetSingleton()->queuedCellTransition = true;
	}

	return RE::BSEventNotifyControl::kContinue;
}

// --- Scene Application ---

void SceneSettingsManager::Update()
{
	// Revert all overrides on main/loading menu to prevent stale state
	bool isMainOrLoading = globals::state->isMainMenuOpen || globals::state->isLoadingMenuOpen;
	if (isMainOrLoading) {
		if (isCurrentlyApplied) {
			RevertToExteriorSettings();
			isCurrentlyApplied = false;
		}
		if (isTimeOfDayActive)
			DeactivateTimeOfDay();
		if (isWeatherSceneActive)
			DeactivateWeatherScene();
		return;
	}

	if (queuedCellTransition) {
		queuedCellTransition = false;
		OnCellTransition();
	}

	// Continuously update time-of-day blended values when exterior
	if (isTimeOfDayActive)
		UpdateTimeOfDay();

	// Layer per-weather scene overrides on top of global TOD
	if (!isCurrentlyApplied)
		UpdateWeatherScene();
}

void SceneSettingsManager::OnCellTransition()
{
	// Use cell-based check; sky mode is unreliable for mods (DIAL, DWS) that enable kUseSkyLighting in interiors
	bool interior = Util::IsInterior();

	if (interior) {
		// Entering interior: deactivate TOD and weather scene, then apply interior overrides
		if (isTimeOfDayActive)
			DeactivateTimeOfDay();
		if (isWeatherSceneActive)
			DeactivateWeatherScene();
		if (!isCurrentlyApplied) {
			SaveExteriorSettings(SceneType::InteriorOnly);
			ApplySettings(SceneType::InteriorOnly);
			isCurrentlyApplied = true;
		}
	} else {
		// Entering exterior: revert interior overrides, then activate TOD
		if (isCurrentlyApplied) {
			RevertToExteriorSettings();
			isCurrentlyApplied = false;
		}
		if (!isTimeOfDayActive)
			ActivateTimeOfDay();
	}
}

void SceneSettingsManager::ReapplyIfActive()
{
	if (isCurrentlyApplied) {
		RevertToExteriorSettings();
		SaveExteriorSettings(SceneType::InteriorOnly);
		ApplySettings(SceneType::InteriorOnly);
	}

	// Use cell-based check consistent with OnCellTransition (DIAL/DWS compatible)
	bool isExterior = !Util::IsInterior();

	bool hasActiveEntries = HasActiveEntries(SceneType::TimeOfDay);

	if (isTimeOfDayActive) {
		if (hasActiveEntries) {
			// Re-blend with updated entries
			RevertTimeOfDayBaseline();
			SaveTimeOfDayBaseline();
			ApplyTimeOfDayBlended();
		} else {
			// All entries removed — deactivate
			DeactivateTimeOfDay();
		}
	} else if (isExterior && hasActiveEntries && !isCurrentlyApplied) {
		// User added first TOD entry while already in an exterior — activate now
		ActivateTimeOfDay();
	}
}

bool SceneSettingsManager::IsSettingControlled(const std::string& featureShortName, const std::string& settingKey) const
{
	if (!isCurrentlyApplied && !isTimeOfDayActive)
		return false;
	if (IsFeaturePaused(featureShortName))
		return false;

	// Check all scene types for active overrides
	for (const auto& [type, vec] : entries) {
		// Skip inactive scene types
		if (type == SceneType::InteriorOnly && !isCurrentlyApplied)
			continue;
		if (type == SceneType::TimeOfDay && !isTimeOfDayActive)
			continue;
		for (const auto& entry : vec) {
			if (entry.paused)
				continue;
			if (entry.featureShortName == featureShortName && entry.settingKey == settingKey)
				return true;
		}
	}
	return false;
}

bool SceneSettingsManager::HasActiveSettingsForFeature(const std::string& featureShortName) const
{
	if (!isCurrentlyApplied && !isTimeOfDayActive)
		return false;

	for (const auto& [type, vec] : entries) {
		// Only report entries from scene types that are currently active.
		// InteriorOnly entries should not show as active when in an exterior,
		// and TimeOfDay entries should not show as active when in an interior.
		if (type == SceneType::InteriorOnly && !isCurrentlyApplied)
			continue;
		if (type == SceneType::TimeOfDay && !isTimeOfDayActive)
			continue;

		for (const auto& entry : vec) {
			if (!entry.paused && entry.featureShortName == featureShortName)
				return true;
		}
	}
	return false;
}

bool SceneSettingsManager::IsFeaturePaused(const std::string& featureShortName) const
{
	auto it = featurePauseStates.find(featureShortName);
	return it != featurePauseStates.end() && it->second;
}

void SceneSettingsManager::SetFeaturePaused(const std::string& featureShortName, bool paused)
{
	featurePauseStates[featureShortName] = paused;
	ReapplyIfActive();
}

// --- Apply / Revert ---

void SceneSettingsManager::SavePartialBaseline(SceneType type, std::map<std::string, json>& outBaseline)
{
	std::map<std::string, std::set<std::string>> keysToSave;
	for (const auto& entry : GetEntries(type))
		if (IsEntryActive(entry))
			keysToSave[entry.featureShortName].insert(entry.settingKey);

	for (const auto& [shortName, keys] : keysToSave) {
		auto* feature = Feature::FindFeatureByShortName(shortName);
		if (!feature)
			continue;

		json fullSettings;
		feature->SaveSettings(fullSettings);

		json& partial = outBaseline[shortName];
		if (!partial.is_object())
			partial = json::object();

		for (const auto& key : keys)
			if (fullSettings.contains(key) && !partial.contains(key))
				partial[key] = fullSettings[key];
	}
}

void SceneSettingsManager::SaveExteriorSettings(SceneType type)
{
	SavePartialBaseline(type, savedExteriorSettings);
}

void SceneSettingsManager::ApplySettings(SceneType type)
{
	// Apply user entries first, then overwrites — overwrites win via last-write-wins
	for (const auto& entry : GetEntries(type)) {
		if (entry.source != EntrySource::User || !IsEntryActive(entry))
			continue;
		ApplySettingToFeature(entry);
	}
	for (const auto& entry : GetEntries(type)) {
		if (entry.source != EntrySource::Overwrite || !IsEntryActive(entry))
			continue;
		ApplySettingToFeature(entry);
	}
}

void SceneSettingsManager::RevertFromBaseline(std::map<std::string, json>& baseline)
{
	for (const auto& [shortName, savedKeys] : baseline) {
		auto* feature = Feature::FindFeatureByShortName(shortName);
		if (!feature)
			continue;

		json current;
		feature->SaveSettings(current);
		for (auto& [key, val] : savedKeys.items())
			current[key] = val;
		feature->LoadSettings(current);
	}
	baseline.clear();
}

void SceneSettingsManager::RevertToExteriorSettings()
{
	RevertFromBaseline(savedExteriorSettings);
}

void SceneSettingsManager::ApplySettingToFeature(const SettingEntry& entry)
{
	auto* feature = Feature::FindFeatureByShortName(entry.featureShortName);
	if (!feature)
		return;

	json settings;
	feature->SaveSettings(settings);

	if (!settings.is_object())
		return;

	if (!settings.contains(entry.settingKey)) {
		logger::warn("[SceneSettings] Setting '{}' not found in feature '{}', skipping", entry.settingKey, entry.featureShortName);
		return;
	}

	settings[entry.settingKey] = entry.value;
	feature->LoadSettings(settings);

	// Round-trip verification: check if the feature clamped the value
	json verify;
	feature->SaveSettings(verify);
	if (verify.contains(entry.settingKey) && verify[entry.settingKey] != entry.value) {
		logger::warn("[SceneSettings] Feature '{}' clamped setting '{}' from {} to {}",
			entry.featureShortName, entry.settingKey,
			entry.value.dump(), verify[entry.settingKey].dump());
	}
}

// --- Time of Day ---

void SceneSettingsManager::ActivateTimeOfDay()
{
	if (isTimeOfDayActive || !HasActiveEntries(SceneType::TimeOfDay))
		return;
	// TOD and InteriorOnly are mutually exclusive — don't activate TOD while
	// interior overrides are applied, as they write to the same feature values.
	if (isCurrentlyApplied) {
		logger::debug("[SceneSettings] Skipping TOD activation — interior overrides are active");
		return;
	}
	SaveTimeOfDayBaseline();
	isTimeOfDayActive = true;
	lastDominantPeriod = GetDominantPeriod();
	ApplyTimeOfDayBlended();
	logger::info("[SceneSettings] Time of Day activated");
}

void SceneSettingsManager::DeactivateTimeOfDay()
{
	if (!isTimeOfDayActive)
		return;
	RevertTimeOfDayBaseline();
	isTimeOfDayActive = false;
	lastDominantPeriod = TimeOfDayPeriod::Count;
	logger::info("[SceneSettings] Time of Day deactivated");
}

void SceneSettingsManager::SaveTimeOfDayBaseline()
{
	SavePartialBaseline(SceneType::TimeOfDay, savedTimeOfDayBaseline);
}

void SceneSettingsManager::RevertTimeOfDayBaseline()
{
	RevertFromBaseline(savedTimeOfDayBaseline);
	lastAppliedTODFloats.clear();
	lastAppliedTODOther.clear();
	lastBlendedHour = -1.0f;
}

void SceneSettingsManager::UpdateTimeOfDay()
{
	if (GetEntries(SceneType::TimeOfDay).empty()) {
		if (isTimeOfDayActive)
			DeactivateTimeOfDay();
		return;
	}
	// Safety: if interior overrides are somehow active while TOD is running,
	// deactivate TOD to prevent conflicting writes to the same feature values.
	if (isCurrentlyApplied) {
		logger::warn("[SceneSettings] TOD was active while interior overrides applied — deactivating TOD");
		DeactivateTimeOfDay();
		return;
	}
	ApplyTimeOfDayBlended();
}

void SceneSettingsManager::ApplyTimeOfDayBlended()
{
	// Throttle: skip the expensive map rebuild + blend when the game hour
	// hasn't moved enough to produce a visible change.  On a hot per-frame
	// path this avoids thousands of string-keyed map operations per second.
	float currentHour = GetCurrentGameHour();
	if (lastBlendedHour >= 0.0f && std::abs(currentHour - lastBlendedHour) < kHourUpdateThreshold)
		return;
	lastBlendedHour = currentHour;

	float factors[kPeriodCount];
	GetTimeOfDayFactors(factors);

	// Inline dominant period computation to avoid a second GetTimeOfDayFactors call
	int bestIdx = 0;
	for (int i = 1; i < kPeriodCount; ++i)
		if (factors[i] > factors[bestIdx])
			bestIdx = i;
	auto dominant = static_cast<TimeOfDayPeriod>(bestIdx);

	// Group active entries by feature, using pointers to avoid JSON copies.
	struct PeriodSlot
	{
		const json* value = nullptr;
		EntrySource source = EntrySource::User;
	};
	// featureShortName -> settingKey -> periodIdx -> resolved slot
	std::map<std::string, std::map<std::string, std::map<int, PeriodSlot>>> collapsedSettings;
	for (const auto& entry : GetEntries(SceneType::TimeOfDay)) {
		if (!IsEntryActive(entry) || entry.period == TimeOfDayPeriod::Count)
			continue;
		int pIdx = static_cast<int>(entry.period);
		auto& slot = collapsedSettings[entry.featureShortName][entry.settingKey][pIdx];
		// First write always wins; Overwrite always supersedes User.
		if (!slot.value || (entry.source == EntrySource::Overwrite && slot.source != EntrySource::Overwrite)) {
			slot.value = &entry.value;
			slot.source = entry.source;
		}
	}

	// Build the final PeriodRef vectors from the collapsed map
	std::map<std::string, std::map<std::string, std::vector<PeriodRef>>> featureSettings;
	for (auto& [shortName, keyMap] : collapsedSettings) {
		for (auto& [key, periodMap] : keyMap) {
			auto& refs = featureSettings[shortName][key];
			refs.reserve(periodMap.size());
			for (auto& [pIdx, slot] : periodMap)
				refs.push_back({ pIdx, slot.value });
		}
	}

	for (auto& [shortName, settingsMap] : featureSettings) {
		std::vector<std::pair<std::string, json>> dirtyKeys;

		for (auto& [key, periodRefs] : settingsMap) {
			const json* baseline = FindTODBaseline(shortName, key);
			if (!baseline)
				continue;

			if (DetectSettingType(*baseline) == SettingType::Float) {
				if (!baseline->is_number()) {
					logger::warn("SceneSettingsManager: TOD baseline for '{}' key '{}' is not numeric — skipping", shortName, key);
					continue;
				}
				float baseVal = baseline->get<float>();
				if (!std::isfinite(baseVal))
					baseVal = 0.0f;

				float result = BlendFloatForPeriods(baseVal, periodRefs, factors, shortName, key);

				// Epsilon comparison — skip if the float barely changed.
				// Use find() first to avoid default-inserting 0.0f, which would
				// cause the first apply to be skipped when result ≈ 0.
				auto& featureFloats = lastAppliedTODFloats[shortName];
				auto floatIt = featureFloats.find(key);
				if (floatIt != featureFloats.end() && std::abs(floatIt->second - result) < kBlendEpsilon)
					continue;
				featureFloats[key] = result;
				dirtyKeys.emplace_back(key, result);
			} else {
				json blendedValue = SnapNonFloatToDominant(*baseline, periodRefs, dominant, shortName, key);

				// Exact comparison for non-float (bools, ints snap — rarely change)
				auto& cachedOther = lastAppliedTODOther[shortName][key];
				if (cachedOther == blendedValue)
					continue;
				cachedOther = blendedValue;
				dirtyKeys.emplace_back(key, std::move(blendedValue));
			}
		}

		if (dirtyKeys.empty())
			continue;

		// Get FRESH settings from the feature (cheap to_json, keeps non-TOD keys current)
		auto* feature = Feature::FindFeatureByShortName(shortName);
		if (!feature)
			continue;

		json current;
		feature->SaveSettings(current);

		// Patch only our TOD-controlled keys into the fresh blob
		for (auto& [k, v] : dirtyKeys)
			current[k] = std::move(v);

		// Single LoadSettings with up-to-date non-TOD values intact
		feature->LoadSettings(current);
	}

	lastDominantPeriod = dominant;
}

const json* SceneSettingsManager::FindTODBaseline(const std::string& shortName, const std::string& key) const
{
	auto baseIt = savedTimeOfDayBaseline.find(shortName);
	if (baseIt != savedTimeOfDayBaseline.end() && baseIt->second.contains(key))
		return &baseIt->second[key];
	return nullptr;
}

float SceneSettingsManager::BlendFloatForPeriods(float baseVal, const std::vector<PeriodRef>& periodRefs,
	const float* factors, const std::string& shortName, const std::string& key) const
{
	float result = 0.0f;
	float coveredFactor = 0.0f;

	for (auto& pr : periodRefs) {
		float f = factors[pr.periodIdx];
		if (f > 0.0f) {
			if (!pr.value->is_number()) {
				logger::warn("SceneSettingsManager: TOD period value for '{}' key '{}' is not numeric — falling back to baseline for this period", shortName, key);
				continue;  // Don't add to coveredFactor — baseline fills in via (1 - coveredFactor) * baseVal
			}
			float periodVal = pr.value->get<float>();
			if (!std::isfinite(periodVal))
				periodVal = 0.0f;
			result += f * periodVal;
			coveredFactor += f;
		}
	}

	return result + (1.0f - coveredFactor) * baseVal;
}

json SceneSettingsManager::SnapNonFloatToDominant(const json& baseline, const std::vector<PeriodRef>& periodRefs,
	TimeOfDayPeriod dominant, const std::string& shortName, const std::string& key) const
{
	json blendedValue = baseline;
	for (auto& pr : periodRefs) {
		if (static_cast<TimeOfDayPeriod>(pr.periodIdx) == dominant) {
			if (pr.value->type() == baseline.type()) {
				blendedValue = *pr.value;
			} else {
				logger::warn("SceneSettingsManager: TOD period value for '{}' key '{}' has type '{}' but baseline expects '{}' — using baseline",
					shortName, key, pr.value->type_name(), baseline.type_name());
			}
		}
	}
	return blendedValue;
}

// --- Persistence ---

void SceneSettingsManager::SaveUserSettings(SceneType type)
{
	auto path = GetSettingsFilePath(type);
	Util::FileHelpers::EnsureDirectoryExists(path.parent_path());

	auto& vec = GetEntries(type);
	json data = json::array();
	for (const auto& entry : vec) {
		if (entry.source != EntrySource::User)
			continue;

		json item;
		item["feature"] = entry.featureShortName;
		item["setting"] = entry.settingKey;
		item["value"] = entry.value;
		item["originalValue"] = entry.originalValue;
		item["paused"] = entry.paused;
		if (type == SceneType::TimeOfDay && entry.period != TimeOfDayPeriod::Count)
			item["period"] = GetPeriodName(entry.period);
		data.push_back(std::move(item));
	}

	auto typeName = GetSceneTypeName(type);
	try {
		std::ofstream file(path);
		if (file.is_open()) {
			file << data.dump(2);
			if (file.fail())
				logger::error("[SceneSettings] Write error saving {} settings (disk full or permissions issue)", typeName);
			else
				logger::info("[SceneSettings] Saved {} {} user settings", data.size(), typeName);
		}
	} catch (const std::exception& e) {
		logger::error("[SceneSettings] Failed to save {} settings: {}", typeName, e.what());
	}
}

void SceneSettingsManager::LoadUserSettings(SceneType type)
{
	auto path = GetSettingsFilePath(type);
	auto typeName = GetSceneTypeName(type);

	std::error_code ec;
	if (!std::filesystem::exists(path, ec))
		return;

	try {
		std::ifstream file(path);
		if (!file.is_open())
			return;

		json data = json::parse(file);
		if (!data.is_array())
			return;

		auto& vec = GetEntriesMut(type);
		int loadedCount = 0;
		for (const auto& item : data) {
			if (!item.contains("feature") || !item.contains("setting") || !item.contains("value"))
				continue;

			// Validate field types before extracting — get<std::string>() throws on wrong type
			if (!item["feature"].is_string() || !item["setting"].is_string()) {
				logger::warn("SceneSettingsManager: Skipping {} entry with non-string feature/setting field", typeName);
				continue;
			}

			SettingEntry entry;
			entry.featureShortName = item["feature"].get<std::string>();
			entry.settingKey = item["setting"].get<std::string>();
			entry.value = item["value"];
			entry.originalValue = item.value("originalValue", entry.value);
			if (item.contains("paused") && !item["paused"].is_boolean()) {
				logger::warn("SceneSettingsManager: '{}' entry {}.{} has non-boolean 'paused' (type: {}) — defaulting to false",
					typeName, entry.featureShortName, entry.settingKey, item["paused"].type_name());
			}
			entry.paused = (item.contains("paused") && item["paused"].is_boolean()) ? item["paused"].get<bool>() : false;
			entry.source = EntrySource::User;

			// Parse period for TimeOfDay entries
			if (type == SceneType::TimeOfDay) {
				if (!item.contains("period")) {
					logger::warn("SceneSettingsManager: TimeOfDay entry for feature '{}' key '{}' is missing 'period' — skipping to avoid ghost entry",
						entry.featureShortName, entry.settingKey);
					continue;
				}
				if (!item["period"].is_string()) {
					logger::warn("SceneSettingsManager: TimeOfDay entry for feature '{}' key '{}' has non-string 'period' (type: {}) — skipping",
						entry.featureShortName, entry.settingKey, item["period"].type_name());
					continue;
				}
				entry.period = GetPeriodFromName(item["period"].get<std::string>());
				if (entry.period == TimeOfDayPeriod::Count) {
					logger::warn("SceneSettingsManager: TimeOfDay entry for feature '{}' key '{}' has invalid period '{}' — skipping",
						entry.featureShortName, entry.settingKey, item["period"].get<std::string>());
					continue;  // Invalid period name
				}

				// TOD only supports float settings — use DetectSettingType to match AddSetting/DiscoverOverwrites
				if (DetectSettingType(entry.value) != SettingType::Float) {
					logger::warn("SceneSettingsManager: TimeOfDay entry for feature '{}' key '{}' has non-float value (type: {}) — skipping",
						entry.featureShortName, entry.settingKey, entry.value.type_name());
					continue;
				}
				if (!std::isfinite(entry.value.get<float>())) {
					logger::warn("SceneSettingsManager: TimeOfDay entry for feature '{}' key '{}' has non-finite value — skipping",
						entry.featureShortName, entry.settingKey);
					continue;
				}
			}

			if (!Feature::FindFeatureByShortName(entry.featureShortName))
				continue;

			if (HasDuplicateEntry(type, entry.featureShortName, entry.settingKey, EntrySource::User, entry.period))
				continue;
			vec.push_back(std::move(entry));
			loadedCount++;
		}

		logger::info("[SceneSettings] Loaded {} {} user settings", loadedCount, typeName);
	} catch (const std::exception& e) {
		logger::error("[SceneSettings] Failed to load {} settings: {}", typeName, e.what());
	}
}

void SceneSettingsManager::DiscoverOverwrites(SceneType type)
{
	// TimeOfDay has period subfolders; delegate to a shared loader
	if (type == SceneType::TimeOfDay) {
		auto basePath = GetOverwritesPath(type);
		for (int i = 0; i < kPeriodCount; ++i) {
			auto period = static_cast<TimeOfDayPeriod>(i);
			auto periodPath = basePath / GetPeriodName(period);
			DiscoverOverwritesInDir(type, periodPath, period);
		}
		return;
	}

	DiscoverOverwritesInDir(type, GetOverwritesPath(type));
}

void SceneSettingsManager::DiscoverOverwritesInDir(SceneType type, const std::filesystem::path& dir, TimeOfDayPeriod period)
{
	auto typeName = GetSceneTypeName(type);

	std::error_code ec;
	if (!std::filesystem::exists(dir, ec))
		return;

	logger::info("[SceneSettings] Discovering {} overwrites in: {}", typeName, dir.string());

	auto& vec = GetEntriesMut(type);
	int filesFound = 0, overwritesLoaded = 0;

	for (const auto& dirEntry : std::filesystem::directory_iterator(dir, ec)) {
		if (ec) {
			logger::error("[SceneSettings] Error iterating {} overwrites: {}", typeName, ec.message());
			break;
		}
		if (!dirEntry.is_regular_file() || dirEntry.path().extension() != ".json")
			continue;

		auto filename = dirEntry.path().filename().string();
		filesFound++;

		try {
			if (dirEntry.file_size() > MAX_OVERWRITE_FILE_SIZE) {
				logger::warn("[SceneSettings] Skipping overwrite '{}': file too large", filename);
				continue;
			}

			std::ifstream file(dirEntry.path());
			if (!file.is_open()) {
				logger::warn("[SceneSettings] Skipping overwrite '{}': could not open file", filename);
				continue;
			}

			json data = json::parse(file);

			// Resolve feature name: explicit _feature field, or infer from filename
			std::string featureShortName = data.value("_feature", "");
			if (featureShortName.empty()) {
				auto stem = dirEntry.path().stem().string();
				auto lastUnderscore = stem.rfind('_');
				if (lastUnderscore != std::string::npos) {
					auto candidate = stem.substr(lastUnderscore + 1);
					if (Feature::FindFeatureByShortName(candidate))
						featureShortName = candidate;
				}
			}

			if (featureShortName.empty() || !Feature::FindFeatureByShortName(featureShortName)) {
				logger::warn("[SceneSettings] Skipping overwrite '{}': feature not resolved", filename);
				continue;
			}

			// Exactly one non-metadata setting per file
			int settingCount = 0;
			std::string settingKey;
			json settingValue;
			for (auto& [key, val] : data.items()) {
				if (!key.starts_with("_")) {
					settingCount++;
					if (settingCount == 1) {
						settingKey = key;
						settingValue = val;
					}
				}
			}

			if (settingCount != 1) {
				logger::warn("[SceneSettings] Skipping overwrite '{}': expected 1 setting, found {}", filename, settingCount);
				continue;
			}

			// TOD only supports float settings (smooth interpolation)
			if (type == SceneType::TimeOfDay && DetectSettingType(settingValue) != SettingType::Float) {
				logger::warn("[SceneSettings] Skipping overwrite '{}': non-float setting '{}' not allowed in Time of Day", filename, settingKey);
				continue;
			}
			if (type == SceneType::TimeOfDay && !std::isfinite(settingValue.get<float>())) {
				logger::warn("[SceneSettings] Skipping overwrite '{}': non-finite value for setting '{}'", filename, settingKey);
				continue;
			}

			// Duplicate check
			if (HasDuplicateEntry(type, featureShortName, settingKey, EntrySource::Overwrite, period))
				continue;

			SettingEntry entry;
			entry.featureShortName = featureShortName;
			entry.settingKey = settingKey;
			entry.value = settingValue;
			entry.originalValue = settingValue;
			entry.source = EntrySource::Overwrite;
			entry.sourceFilename = filename;
			entry.period = period;
			vec.push_back(std::move(entry));

			overwritesLoaded++;
			logger::info("[SceneSettings] Loaded {} overwrite: {} -> {}.{}", typeName, filename, featureShortName, settingKey);
		} catch (const std::exception& e) {
			logger::error("[SceneSettings] Failed to load {} overwrite '{}': {}", typeName, filename, e.what());
		}
	}

	if (filesFound > 0)
		logger::info("[SceneSettings] {} overwrite scan: {} files, {} loaded", typeName, filesFound, overwritesLoaded);
}

void SceneSettingsManager::LoadAll()
{
	DiscoverOverwrites(SceneType::InteriorOnly);
	LoadUserSettings(SceneType::InteriorOnly);
	DiscoverOverwrites(SceneType::TimeOfDay);
	LoadUserSettings(SceneType::TimeOfDay);
	DiscoverAllWeatherSceneSettings();
}

// --- Per-Weather Scene Settings ---

const SceneSettingsManager::WeatherSceneConfig SceneSettingsManager::kEmptyWeatherConfig{};

const SceneSettingsManager::WeatherSceneConfig& SceneSettingsManager::GetWeatherConfig(RE::FormID weatherId) const
{
	auto it = weatherSceneConfigs.find(weatherId);
	return (it != weatherSceneConfigs.end()) ? it->second : kEmptyWeatherConfig;
}

SceneSettingsManager::WeatherSceneConfig& SceneSettingsManager::GetWeatherConfigMut(RE::FormID weatherId)
{
	return weatherSceneConfigs[weatherId];
}

bool SceneSettingsManager::HasWeatherConfig(RE::FormID weatherId) const
{
	auto it = weatherSceneConfigs.find(weatherId);
	return it != weatherSceneConfigs.end() && !it->second.entries.empty();
}

bool SceneSettingsManager::IsWeatherTimeOfDay(RE::FormID weatherId) const
{
	auto it = weatherSceneConfigs.find(weatherId);
	return it != weatherSceneConfigs.end() && it->second.useTimeOfDay;
}

void SceneSettingsManager::SetWeatherTimeOfDay(RE::FormID weatherId, bool useTod)
{
	auto& config = GetWeatherConfigMut(weatherId);
	if (config.useTimeOfDay == useTod)
		return;
	config.useTimeOfDay = useTod;
	// Clear entries when switching modes — period semantics differ
	config.entries.clear();
	SaveWeatherSceneSettings(weatherId);
	logger::info("[SceneSettings] Weather {:08X} TOD mode set to {}", weatherId, useTod);
}

void SceneSettingsManager::AddWeatherSetting(RE::FormID weatherId, const std::string& featureShortName,
	const std::string& settingKey, const json& value, TimeOfDayPeriod period)
{
	auto& config = GetWeatherConfigMut(weatherId);

	if (config.useTimeOfDay) {
		if (period == TimeOfDayPeriod::Count || static_cast<int>(period) < 0 || static_cast<int>(period) >= kPeriodCount)
			return;
		if (DetectSettingType(value) != SettingType::Float)
			return;
		if (!std::isfinite(value.get<float>()))
			return;
		if (HasWeatherEntryForPeriod(weatherId, featureShortName, settingKey, period))
			return;
	} else {
		if (HasWeatherEntry(weatherId, featureShortName, settingKey))
			return;
		period = TimeOfDayPeriod::Count;
	}

	SettingEntry entry;
	entry.featureShortName = featureShortName;
	entry.settingKey = settingKey;
	entry.value = value;
	entry.originalValue = value;
	entry.source = EntrySource::User;
	entry.period = period;
	config.entries.push_back(std::move(entry));
	SaveWeatherSceneSettings(weatherId);
}

void SceneSettingsManager::RemoveWeatherSetting(RE::FormID weatherId, size_t index)
{
	auto it = weatherSceneConfigs.find(weatherId);
	if (it == weatherSceneConfigs.end() || index >= it->second.entries.size())
		return;
	it->second.entries.erase(it->second.entries.begin() + static_cast<ptrdiff_t>(index));
	SaveWeatherSceneSettings(weatherId);
}

void SceneSettingsManager::TogglePauseWeatherEntry(RE::FormID weatherId, size_t index)
{
	auto it = weatherSceneConfigs.find(weatherId);
	if (it == weatherSceneConfigs.end() || index >= it->second.entries.size())
		return;
	it->second.entries[index].paused = !it->second.entries[index].paused;
	SaveWeatherSceneSettings(weatherId);
}

void SceneSettingsManager::UpdateWeatherEntryValue(RE::FormID weatherId, size_t index, const json& newValue, bool deferSave)
{
	auto it = weatherSceneConfigs.find(weatherId);
	if (it == weatherSceneConfigs.end() || index >= it->second.entries.size())
		return;
	it->second.entries[index].value = newValue;
	if (!deferSave)
		SaveWeatherSceneSettings(weatherId);
}

void SceneSettingsManager::RevertWeatherEntryToDefault(RE::FormID weatherId, size_t index)
{
	auto it = weatherSceneConfigs.find(weatherId);
	if (it == weatherSceneConfigs.end() || index >= it->second.entries.size())
		return;
	auto& entry = it->second.entries[index];
	entry.value = entry.originalValue;
	SaveWeatherSceneSettings(weatherId);
}

void SceneSettingsManager::DeleteAllWeatherSettings(RE::FormID weatherId)
{
	auto it = weatherSceneConfigs.find(weatherId);
	if (it != weatherSceneConfigs.end()) {
		it->second.entries.clear();
		SaveWeatherSceneSettings(weatherId);
	}
}

bool SceneSettingsManager::HasWeatherEntry(RE::FormID weatherId, const std::string& featureShortName,
	const std::string& settingKey) const
{
	auto it = weatherSceneConfigs.find(weatherId);
	if (it == weatherSceneConfigs.end())
		return false;
	for (const auto& e : it->second.entries)
		if (e.featureShortName == featureShortName && e.settingKey == settingKey)
			return true;
	return false;
}

bool SceneSettingsManager::HasWeatherEntryForPeriod(RE::FormID weatherId, const std::string& featureShortName,
	const std::string& settingKey, TimeOfDayPeriod period) const
{
	auto it = weatherSceneConfigs.find(weatherId);
	if (it == weatherSceneConfigs.end())
		return false;
	for (const auto& e : it->second.entries)
		if (e.featureShortName == featureShortName && e.settingKey == settingKey && e.period == period)
			return true;
	return false;
}

// --- Per-Weather Persistence ---

std::filesystem::path SceneSettingsManager::GetWeatherSceneDir()
{
	return Util::PathHelpers::GetSceneSettingsPath() / "Weather";
}

std::filesystem::path SceneSettingsManager::GetWeatherScenePath(RE::FormID weatherId)
{
	return GetWeatherSceneDir() / (GetWeatherFormKey(weatherId) + ".json");
}

std::string SceneSettingsManager::GetWeatherFormKey(RE::FormID weatherId)
{
	return std::format("{:08X}", weatherId);
}

void SceneSettingsManager::SaveWeatherSceneSettings(RE::FormID weatherId)
{
	auto path = GetWeatherScenePath(weatherId);
	Util::FileHelpers::EnsureDirectoryExists(path.parent_path());

	auto it = weatherSceneConfigs.find(weatherId);
	if (it == weatherSceneConfigs.end() || it->second.entries.empty()) {
		// Remove file if no entries
		std::error_code ec;
		std::filesystem::remove(path, ec);
		return;
	}

	auto& config = it->second;
	json data;
	data["useTimeOfDay"] = config.useTimeOfDay;

	json entriesArr = json::array();
	for (const auto& entry : config.entries) {
		json item;
		item["feature"] = entry.featureShortName;
		item["setting"] = entry.settingKey;
		item["value"] = entry.value;
		item["originalValue"] = entry.originalValue;
		item["paused"] = entry.paused;
		if (config.useTimeOfDay && entry.period != TimeOfDayPeriod::Count)
			item["period"] = GetPeriodName(entry.period);
		entriesArr.push_back(std::move(item));
	}
	data["entries"] = std::move(entriesArr);

	try {
		std::ofstream file(path);
		if (file.is_open())
			file << data.dump(2);
	} catch (const std::exception& e) {
		logger::error("[SceneSettings] Failed to save weather scene {:08X}: {}", weatherId, e.what());
	}
}

void SceneSettingsManager::LoadWeatherSceneSettings(RE::FormID weatherId)
{
	auto path = GetWeatherScenePath(weatherId);
	std::error_code ec;
	if (!std::filesystem::exists(path, ec))
		return;

	try {
		std::ifstream file(path);
		if (!file.is_open())
			return;

		json data = json::parse(file);
		auto& config = GetWeatherConfigMut(weatherId);
		config.useTimeOfDay = data.value("useTimeOfDay", false);
		config.entries.clear();

		if (!data.contains("entries") || !data["entries"].is_array())
			return;

		for (const auto& item : data["entries"]) {
			if (!item.contains("feature") || !item.contains("setting") || !item.contains("value"))
				continue;
			if (!item["feature"].is_string() || !item["setting"].is_string())
				continue;

			SettingEntry entry;
			entry.featureShortName = item["feature"].get<std::string>();
			entry.settingKey = item["setting"].get<std::string>();
			entry.value = item["value"];
			entry.originalValue = item.value("originalValue", entry.value);
			entry.paused = item.value("paused", false);
			entry.source = EntrySource::User;

			if (config.useTimeOfDay) {
				if (!item.contains("period") || !item["period"].is_string())
					continue;
				entry.period = GetPeriodFromName(item["period"].get<std::string>());
				if (entry.period == TimeOfDayPeriod::Count)
					continue;
				if (DetectSettingType(entry.value) != SettingType::Float)
					continue;
			}

			if (!Feature::FindFeatureByShortName(entry.featureShortName))
				continue;

			config.entries.push_back(std::move(entry));
		}

		if (!config.entries.empty())
			logger::info("[SceneSettings] Loaded {} weather scene entries for {:08X}", config.entries.size(), weatherId);
	} catch (const std::exception& e) {
		logger::error("[SceneSettings] Failed to load weather scene {:08X}: {}", weatherId, e.what());
	}
}

void SceneSettingsManager::DiscoverAllWeatherSceneSettings()
{
	auto dir = GetWeatherSceneDir();
	std::error_code ec;
	if (!std::filesystem::exists(dir, ec))
		return;

	for (const auto& dirEntry : std::filesystem::directory_iterator(dir, ec)) {
		if (!dirEntry.is_regular_file() || dirEntry.path().extension() != ".json")
			continue;
		auto stem = dirEntry.path().stem().string();
		RE::FormID id = 0;
		try {
			id = static_cast<RE::FormID>(std::stoul(stem, nullptr, 16));
		} catch (...) {
			continue;
		}
		if (id != 0)
			LoadWeatherSceneSettings(id);
	}
}

// --- Per-Weather Application ---

void SceneSettingsManager::SaveWeatherBaseline()
{
	// Save baseline for all features affected by any weather scene settings
	std::unordered_set<std::string> affectedFeatures;
	for (const auto& [id, config] : weatherSceneConfigs)
		for (const auto& e : config.entries)
			if (!e.paused)
				affectedFeatures.insert(e.featureShortName);

	savedWeatherBaseline.clear();
	for (const auto& name : affectedFeatures) {
		auto* feature = Feature::FindFeatureByShortName(name);
		if (feature)
			feature->SaveSettings(savedWeatherBaseline[name]);
	}
}

void SceneSettingsManager::RevertWeatherBaseline()
{
	RevertFromBaseline(savedWeatherBaseline);
	lastAppliedWeatherFloats.clear();
	lastCurrentWeatherId = 0;
	lastLastWeatherId = 0;
	lastWeatherLerp = -1.0f;
}

void SceneSettingsManager::ActivateWeatherScene()
{
	if (isWeatherSceneActive)
		return;
	// Don't activate while interior overrides are active
	if (isCurrentlyApplied)
		return;
	SaveWeatherBaseline();
	isWeatherSceneActive = true;
	logger::info("[SceneSettings] Weather scene activated");
}

void SceneSettingsManager::DeactivateWeatherScene()
{
	if (!isWeatherSceneActive)
		return;
	RevertWeatherBaseline();
	isWeatherSceneActive = false;
	logger::info("[SceneSettings] Weather scene deactivated");
}

bool SceneSettingsManager::ComputeWeatherBlendedFloat(const std::string& shortName, const std::string& key,
	RE::FormID currentId, RE::FormID lastId, float weatherLerp,
	[[maybe_unused]] float gameHour, float& outValue)
{
	// Helper: resolve a weather's value for a setting (handles flat and TOD modes)
	auto resolveWeatherValue = [&](RE::FormID wId, float& result) -> bool {
		auto wit = weatherSceneConfigs.find(wId);
		if (wit == weatherSceneConfigs.end())
			return false;
		auto& config = wit->second;

		// Flat mode: find a single active entry
		if (!config.useTimeOfDay) {
			for (const auto& e : config.entries) {
				if (e.paused || e.featureShortName != shortName || e.settingKey != key)
					continue;
				if (e.value.is_number()) {
					result = e.value.get<float>();
					return true;
				}
			}
			return false;
		}

		// TOD mode: blend across periods
		float factors[kPeriodCount];
		GetTimeOfDayFactors(factors);

		std::vector<PeriodRef> refs;
		for (const auto& e : config.entries) {
			if (e.paused || e.featureShortName != shortName || e.settingKey != key)
				continue;
			if (e.period == TimeOfDayPeriod::Count || !e.value.is_number())
				continue;
			refs.push_back({ static_cast<int>(e.period), &e.value });
		}
		if (refs.empty())
			return false;

		// Baseline is current feature value if no TOD baseline
		auto baseIt = savedWeatherBaseline.find(shortName);
		float baseVal = 0.0f;
		if (baseIt != savedWeatherBaseline.end() && baseIt->second.contains(key) && baseIt->second[key].is_number())
			baseVal = baseIt->second[key].get<float>();

		result = BlendFloatForPeriods(baseVal, refs, factors, shortName, key);
		return true;
	};

	float currentVal = 0.0f, lastVal = 0.0f;
	bool hasCurrent = (currentId != 0) && resolveWeatherValue(currentId, currentVal);
	bool hasLast = (lastId != 0) && resolveWeatherValue(lastId, lastVal);

	if (!hasCurrent && !hasLast)
		return false;

	if (hasCurrent && hasLast) {
		outValue = lastVal + (currentVal - lastVal) * weatherLerp;
	} else if (hasCurrent) {
		// Blend from baseline to current weather value as lerp approaches 1
		auto baseIt = savedWeatherBaseline.find(shortName);
		float baseVal = 0.0f;
		if (baseIt != savedWeatherBaseline.end() && baseIt->second.contains(key) && baseIt->second[key].is_number())
			baseVal = baseIt->second[key].get<float>();
		outValue = baseVal + (currentVal - baseVal) * weatherLerp;
	} else {
		// Blend from last weather value back to baseline as lerp approaches 1
		auto baseIt = savedWeatherBaseline.find(shortName);
		float baseVal = 0.0f;
		if (baseIt != savedWeatherBaseline.end() && baseIt->second.contains(key) && baseIt->second[key].is_number())
			baseVal = baseIt->second[key].get<float>();
		outValue = lastVal + (baseVal - lastVal) * weatherLerp;
	}
	return true;
}

void SceneSettingsManager::UpdateWeatherScene()
{
	auto sky = RE::Sky::GetSingleton();
	if (!sky || !sky->currentWeather)
		return;

	RE::FormID currentId = sky->currentWeather->GetFormID();
	RE::FormID lastId = sky->lastWeather ? sky->lastWeather->GetFormID() : 0;
	float weatherLerp = sky->currentWeatherPct;

	// Check if either weather has scene settings
	bool anyWeatherConfig = HasWeatherConfig(currentId) || (lastId != 0 && HasWeatherConfig(lastId));

	if (!anyWeatherConfig) {
		if (isWeatherSceneActive)
			DeactivateWeatherScene();
		return;
	}

	if (!isWeatherSceneActive) {
		ActivateWeatherScene();
		if (!isWeatherSceneActive)
			return;
	}

	// Skip if nothing changed (same weathers, same lerp within epsilon)
	if (currentId == lastCurrentWeatherId && lastId == lastLastWeatherId &&
		std::abs(weatherLerp - lastWeatherLerp) < kBlendEpsilon)
		return;
	lastCurrentWeatherId = currentId;
	lastLastWeatherId = lastId;
	lastWeatherLerp = weatherLerp;

	float gameHour = GetCurrentGameHour();

	// Collect all feature+key pairs from both weathers
	std::map<std::string, std::set<std::string>> affectedKeys;
	auto collectKeys = [&](RE::FormID wId) {
		auto wit = weatherSceneConfigs.find(wId);
		if (wit == weatherSceneConfigs.end())
			return;
		for (const auto& e : wit->second.entries)
			if (!e.paused)
				affectedKeys[e.featureShortName].insert(e.settingKey);
	};
	collectKeys(currentId);
	if (lastId != 0)
		collectKeys(lastId);

	// Blend and apply
	for (auto& [shortName, keys] : affectedKeys) {
		auto* feature = Feature::FindFeatureByShortName(shortName);
		if (!feature)
			continue;

		json current;
		feature->SaveSettings(current);
		bool dirty = false;

		for (auto& key : keys) {
			float blended = 0.0f;
			if (!ComputeWeatherBlendedFloat(shortName, key, currentId, lastId, weatherLerp, gameHour, blended))
				continue;

			auto& cache = lastAppliedWeatherFloats[shortName];
			auto cacheIt = cache.find(key);
			if (cacheIt != cache.end() && std::abs(cacheIt->second - blended) < kBlendEpsilon)
				continue;
			cache[key] = blended;
			current[key] = blended;
			dirty = true;
		}

		if (dirty)
			feature->LoadSettings(current);
	}
}
