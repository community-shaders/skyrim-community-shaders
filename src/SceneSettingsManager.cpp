#include "SceneSettingsManager.h"

#include "Feature.h"
#include "Globals.h"
#include "State.h"
#include "Utils/FileSystem.h"
#include "Utils/Game.h"

#include <algorithm>
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

std::filesystem::path SceneSettingsManager::GetUserSettingsFilePath()
{
	return Util::PathHelpers::GetSceneSettingsPath() / "SceneManager.json";
}

std::filesystem::path SceneSettingsManager::GetOverwritesPath(SceneType type)
{
	return Util::PathHelpers::GetSceneSettingsPath() / GetSceneTypeName(type);
}

std::filesystem::path SceneSettingsManager::GetWeatherOverwritesDir()
{
	return Util::PathHelpers::GetSceneSettingsPath() / "Weather";
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

static const std::unordered_set<std::string>& GetInteriorWhitelist()
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
	return whitelist;
}

static const std::unordered_set<std::string>& GetExteriorWhitelist()
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
		"WetnessEffects",
	};
	return whitelist;
}

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

bool SceneSettingsManager::IsFeatureAllowedForType(SceneType type, const std::string& featureShortName)
{
	switch (type) {
	case SceneType::InteriorOnly:
		return GetInteriorWhitelist().contains(featureShortName);
	case SceneType::TimeOfDay:
		return GetExteriorWhitelist().contains(featureShortName);
	default:
		return true;
	}
}

std::vector<std::string> SceneSettingsManager::GetInteriorRelevantFeatureNames()
{
	return FilterFeatureNames(GetInteriorWhitelist());
}

std::vector<std::string> SceneSettingsManager::GetExteriorRelevantFeatureNames()
{
	return FilterFeatureNames(GetExteriorWhitelist());
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
	if (value.is_number_integer()) {
		int v = value.get<int>();
		return (v == 0 || v == 1) ? SettingType::Boolean : SettingType::Integer;
	}
	if (value.is_number_float())
		return SettingType::Float;
	if (value.is_string())
		return SettingType::String;
	return SettingType::Unknown;
}

// Only float values are transitionable (excludes integer toggles/combos like EnableIBL, DALCMode)
static bool IsNumericValue(const json& value)
{
	return value.is_number_float();
}

// Normalize an integer JSON value to float (e.g. json(1) -> json(1.0f))
static json NormalizeToFloat(const json& value)
{
	if (value.is_number_integer())
		return json(value.get<float>());
	return value;
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
		if (IsNumericValue(val))
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

		// TOD only supports numeric settings (smooth interpolation)
		if (!IsNumericValue(value)) {
			logger::warn("[SceneSettings] Rejecting non-numeric TOD setting: {}.{}", featureShortName, settingKey);
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
	entry.value = (type == SceneType::TimeOfDay) ? NormalizeToFloat(value) : value;
	entry.originalValue = entry.value;
	entry.source = EntrySource::User;
	entry.period = period;
	vec.push_back(std::move(entry));
	SaveAllUserSettings();
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
	SaveAllUserSettings();
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
		SaveAllUserSettings();
		ReapplyIfActive();
	}
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
	SaveAllUserSettings();
	ReapplyIfActive();
}

/// Write a single setting as an overwrite JSON file; creates parent dirs as needed.
static bool WriteOverwriteFile(const std::filesystem::path& path,
	const std::string& featureShortName, const std::string& key, const json& value)
{
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);
	if (ec) {
		logger::error("[SceneSettings] WriteOverwriteFile: failed to create dirs for '{}': {}", path.string(), ec.message());
		return false;
	}
	std::ofstream f(path);
	if (!f.is_open()) {
		logger::error("[SceneSettings] WriteOverwriteFile: could not open '{}' for writing", path.string());
		return false;
	}
	f << json{ { "_feature", featureShortName }, { key, value } }.dump(2);
	return true;
}

void SceneSettingsManager::ExportUserSettingsToOverwrites(SceneType type, const std::vector<size_t>& indices)
{
	auto& vec = GetEntriesMut(type);
	auto basePath = GetOverwritesPath(type);
	for (auto idx : indices) {
		if (idx >= vec.size() || vec[idx].source != EntrySource::User)
			continue;
		auto& e = vec[idx];
		auto filename = std::format("{}_{}.json", e.settingKey, e.featureShortName);
		auto dir = (type == SceneType::TimeOfDay && e.period != TimeOfDayPeriod::Count) ? basePath / GetPeriodName(e.period) : basePath;
		WriteOverwriteFile(dir / filename, e.featureShortName, e.settingKey, e.value);
	}
}

void SceneSettingsManager::ExportWeatherUserSettingsToOverwrites(RE::FormID weatherId, const std::vector<size_t>& indices)
{
	auto& vec = GetWeatherConfigMut(weatherId).entries;
	auto baseDir = GetWeatherOverwritesDir() / Util::FormIdToSpid(weatherId);
	for (auto idx : indices) {
		if (idx >= vec.size() || vec[idx].source != EntrySource::User)
			continue;
		auto& e = vec[idx];
		auto filename = std::format("{}_{}.json", e.settingKey, e.featureShortName);
		auto dir = (e.period != TimeOfDayPeriod::Count) ? baseDir / GetPeriodName(e.period) : baseDir;
		WriteOverwriteFile(dir / filename, e.featureShortName, e.settingKey, e.value);
	}
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
		SaveAllUserSettings();

	// For TimeOfDay, recompute blended values; for others, apply directly
	if (type == SceneType::TimeOfDay) {
		if (isTimeOfDayActive) {
			// Reset the hour throttle so a user edit (e.g. slider drag) is
			// applied immediately rather than waiting for the game clock to advance.
			lastBlendedHour = -1.0f;
			ApplyTimeOfDayBlended();
			if (isWeatherSceneActive) {
				lastAppliedWeatherFloats.clear();
				UpdateWeatherScene();
			}
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

	if (isWeatherSceneActive) {
		// Revert and force-reapply so pause state changes take effect immediately
		RevertWeatherBaseline();
		if (isTimeOfDayActive)
			UpdateTimeOfDay();
		SaveWeatherBaseline();
		UpdateWeatherScene();
	}
}

bool SceneSettingsManager::HasActiveSettingsForFeature(const std::string& featureShortName) const
{
	if (!isCurrentlyApplied && !isTimeOfDayActive && !isWeatherSceneActive)
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

	// Check weather scene settings
	if (isWeatherSceneActive) {
		for (RE::FormID wId : { lastCurrentWeatherId, lastLastWeatherId }) {
			if (wId == 0)
				continue;
			auto wit = weatherSceneConfigs.find(wId);
			if (wit == weatherSceneConfigs.end())
				continue;
			for (const auto& e : wit->second.entries)
				if (!e.paused && e.featureShortName == featureShortName)
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

void SceneSettingsManager::SaveBaselineForKeys(const std::map<std::string, std::set<std::string>>& keysToSave,
	std::map<std::string, json>& outBaseline)
{
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

void SceneSettingsManager::SavePartialBaseline(SceneType type, std::map<std::string, json>& outBaseline)
{
	std::map<std::string, std::set<std::string>> keysToSave;
	for (const auto& entry : GetEntries(type))
		if (IsEntryActive(entry))
			keysToSave[entry.featureShortName].insert(entry.settingKey);
	SaveBaselineForKeys(keysToSave, outBaseline);
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

			if (IsNumericValue(*baseline)) {
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
				if (IsActiveWeatherSetting(shortName, key))
					continue;
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

float SceneSettingsManager::BlendFloatForWeatherPeriods(float baseVal, const std::vector<PeriodRef>& periodRefs,
	const float* factors, const std::string& shortName, const std::string& key) const
{
	float result = 0.0f;

	for (int i = 0; i < kPeriodCount; ++i) {
		float f = factors[i];
		if (f <= 0.0f)
			continue;

		const json* periodValue = nullptr;
		for (auto& pr : periodRefs) {
			if (pr.periodIdx == i) {
				periodValue = pr.value;
				break;
			}
		}

		float resolved = GetTimeOfDayPeriodFallbackFloat(baseVal, shortName, key, i);
		if (periodValue) {
			if (periodValue->is_number()) {
				float value = periodValue->get<float>();
				resolved = std::isfinite(value) ? value : 0.0f;
			} else {
				logger::warn("SceneSettingsManager: weather TOD period value for '{}' key '{}' is not numeric", shortName, key);
			}
		}
		result += f * resolved;
	}

	return result;
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

// --- Unified Persistence ---

static json EntryToJson(const SceneSettingsManager::SettingEntry& entry)
{
	json item;
	item["feature"] = entry.featureShortName;
	item["setting"] = entry.settingKey;
	item["value"] = entry.value;
	item["originalValue"] = entry.originalValue;
	item["paused"] = entry.paused;
	if (entry.period != SceneSettingsManager::TimeOfDayPeriod::Count)
		item["period"] = SceneSettingsManager::GetPeriodName(entry.period);
	return item;
}

static json UserEntriesToArray(const std::vector<SceneSettingsManager::SettingEntry>& entries)
{
	json arr = json::array();
	for (const auto& entry : entries)
		if (entry.source == SceneSettingsManager::EntrySource::User)
			arr.push_back(EntryToJson(entry));
	return arr;
}

void SceneSettingsManager::SaveAllUserSettings()
{
	auto path = GetUserSettingsFilePath();
	Util::FileHelpers::EnsureDirectoryExists(path.parent_path());

	json data;
	data["interiorOnly"] = UserEntriesToArray(GetEntries(SceneType::InteriorOnly));
	data["timeOfDay"] = UserEntriesToArray(GetEntries(SceneType::TimeOfDay));

	// Weather entries (keyed by SPID)
	{
		json weatherObj = json::object();
		for (const auto& [weatherId, config] : weatherSceneConfigs) {
			bool hasUserEntries = std::any_of(config.entries.begin(), config.entries.end(),
				[](const SettingEntry& e) { return e.source == EntrySource::User; });

			auto showIt = weatherShowTimeOfDay_.find(weatherId);
			bool hasShowPref = showIt != weatherShowTimeOfDay_.end() && showIt->second;

			if (!hasUserEntries && !hasShowPref)
				continue;

			auto spid = Util::FormIdToSpid(weatherId);
			json weatherEntry = json::object();
			if (hasShowPref)
				weatherEntry["showTimeOfDay"] = true;
			weatherEntry["entries"] = UserEntriesToArray(config.entries);
			weatherObj[spid] = std::move(weatherEntry);
		}
		data["weather"] = std::move(weatherObj);
	}

	try {
		std::ofstream file(path);
		if (file.is_open()) {
			file << data.dump(2);
			if (file.fail())
				logger::error("[SceneSettings] Write error saving SceneManager.json (disk full or permissions issue)");
			else
				logger::info("[SceneSettings] Saved SceneManager.json");
		}
	} catch (const std::exception& e) {
		logger::error("[SceneSettings] Failed to save SceneManager.json: {}", e.what());
	}
}

static bool LoadEntryFromJson(const nlohmann::json& item, SceneSettingsManager::SettingEntry& entry,
	bool requirePeriod, const char* typeName)
{
	using SSM = SceneSettingsManager;

	if (!item.contains("feature") || !item.contains("setting") || !item.contains("value")) {
		logger::warn("[SceneSettings] {} entry missing feature/setting/value fields", typeName);
		return false;
	}
	if (!item["feature"].is_string() || !item["setting"].is_string()) {
		logger::warn("[SceneSettings] {} entry feature/setting not strings", typeName);
		return false;
	}

	entry.featureShortName = item["feature"].get<std::string>();
	entry.settingKey = item["setting"].get<std::string>();
	entry.value = item["value"];
	entry.originalValue = item.value("originalValue", entry.value);
	entry.paused = (item.contains("paused") && item["paused"].is_boolean()) ? item["paused"].get<bool>() : false;
	entry.source = SSM::EntrySource::User;

	if (requirePeriod) {
		if (!item.contains("period") || !item["period"].is_string()) {
			logger::warn("[SceneSettings] {} entry {}.{} missing period — skipping", typeName, entry.featureShortName, entry.settingKey);
			return false;
		}
		entry.period = SSM::GetPeriodFromName(item["period"].get<std::string>());
		if (entry.period == SSM::TimeOfDayPeriod::Count) {
			logger::warn("[SceneSettings] {} entry {}.{} has invalid period '{}' — skipping", typeName, entry.featureShortName, entry.settingKey, item["period"].get<std::string>());
			return false;
		}
		if (!entry.value.is_number()) {
			logger::warn("[SceneSettings] {} entry {}.{} value is not a number (type={}) — skipping", typeName, entry.featureShortName, entry.settingKey, entry.value.type_name());
			return false;
		}
		// Normalize integer-valued floats from JSON roundtrip (e.g. 1 -> 1.0)
		if (entry.value.is_number_integer())
			entry.value = json(entry.value.get<float>());
		if (entry.originalValue.is_number_integer())
			entry.originalValue = json(entry.originalValue.get<float>());
		if (!std::isfinite(entry.value.get<float>())) {
			logger::warn("[SceneSettings] {} entry {}.{} has non-finite value — skipping", typeName, entry.featureShortName, entry.settingKey);
			return false;
		}
	}

	if (!Feature::FindFeatureByShortName(entry.featureShortName)) {
		logger::warn("[SceneSettings] {} entry {}.{} — feature '{}' not found/loaded", typeName, entry.featureShortName, entry.settingKey, entry.featureShortName);
		return false;
	}

	return true;
}

void SceneSettingsManager::LoadAllUserSettings()
{
	auto path = GetUserSettingsFilePath();
	logger::info("[SceneSettings] Loading user settings from: {}", path.string());
	std::error_code ec;
	if (!std::filesystem::exists(path, ec)) {
		logger::info("[SceneSettings] SceneManager.json not found at {}", path.string());
		return;
	}

	try {
		std::ifstream file(path);
		if (!file.is_open())
			return;

		json data = json::parse(file);

		// Interior Only
		if (data.contains("interiorOnly") && data["interiorOnly"].is_array()) {
			auto& vec = GetEntriesMut(SceneType::InteriorOnly);
			int loaded = 0;
			for (const auto& item : data["interiorOnly"]) {
				SettingEntry entry;
				if (!LoadEntryFromJson(item, entry, false, "InteriorOnly"))
					continue;
				if (HasDuplicateEntry(SceneType::InteriorOnly, entry.featureShortName, entry.settingKey, EntrySource::User, entry.period))
					continue;
				vec.push_back(std::move(entry));
				loaded++;
			}
			if (loaded > 0)
				logger::info("[SceneSettings] Loaded {} InteriorOnly user settings", loaded);
		}

		// Time of Day
		if (data.contains("timeOfDay") && data["timeOfDay"].is_array()) {
			auto& vec = GetEntriesMut(SceneType::TimeOfDay);
			int loaded = 0;
			for (const auto& item : data["timeOfDay"]) {
				SettingEntry entry;
				if (!LoadEntryFromJson(item, entry, true, "TimeOfDay"))
					continue;
				if (HasDuplicateEntry(SceneType::TimeOfDay, entry.featureShortName, entry.settingKey, EntrySource::User, entry.period))
					continue;
				vec.push_back(std::move(entry));
				loaded++;
			}
			if (loaded > 0)
				logger::info("[SceneSettings] Loaded {} TimeOfDay user settings", loaded);
		}

		// Weather is loaded separately in LoadWeatherUserSettings() after kDataLoaded

		logger::info("[SceneSettings] Loaded SceneManager.json (non-weather)");
	} catch (const std::exception& e) {
		logger::error("[SceneSettings] Failed to load SceneManager.json: {}", e.what());
	}
}

void SceneSettingsManager::LoadWeatherUserSettings()
{
	auto path = GetUserSettingsFilePath();
	std::error_code ec;
	if (!std::filesystem::exists(path, ec))
		return;

	try {
		std::ifstream file(path);
		if (!file.is_open())
			return;

		json data = json::parse(file);

		// Weather
		if (data.contains("weather") && data["weather"].is_object()) {
			logger::info("[SceneSettings] Weather section found with {} entries", data["weather"].size());
			for (auto& [spidKey, weatherData] : data["weather"].items()) {
				logger::info("[SceneSettings] Processing weather SPID '{}'", spidKey);
				RE::FormID weatherId = Util::SpidToFormId(spidKey);
				if (weatherId == 0) {
					logger::warn("[SceneSettings] Weather SPID '{}' could not be resolved — skipping", spidKey);
					continue;
				}
				logger::info("[SceneSettings] Resolved SPID '{}' to FormID 0x{:X}", spidKey, weatherId);

				// Load showTimeOfDay preference
				if (weatherData.contains("showTimeOfDay") && weatherData["showTimeOfDay"].is_boolean())
					weatherShowTimeOfDay_[weatherId] = weatherData["showTimeOfDay"].get<bool>();

				if (!weatherData.contains("entries") || !weatherData["entries"].is_array())
					continue;

				auto& config = GetWeatherConfigMut(weatherId);
				int loaded = 0;
				for (const auto& item : weatherData["entries"]) {
					SettingEntry entry;
					if (!LoadEntryFromJson(item, entry, true, "Weather"))
						continue;
					if (HasWeatherEntryForPeriod(weatherId, entry.featureShortName, entry.settingKey, entry.period, EntrySource::User))
						continue;
					config.entries.push_back(std::move(entry));
					loaded++;
				}
				if (loaded > 0)
					logger::info("[SceneSettings] Loaded {} weather entries for {}", loaded, spidKey);
			}
		}

		logger::info("[SceneSettings] Loaded weather user settings");
	} catch (const std::exception& e) {
		logger::error("[SceneSettings] Failed to load weather user settings: {}", e.what());
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

/// Parse a single overwrite JSON file into a SettingEntry.
/// Returns true on success; fills outEntry with the parsed data.
/// When requireNumeric is true, non-numeric or non-finite values are rejected
/// and the value is normalized to float.
static bool ParseOverwriteFile(const std::filesystem::path& filePath,
	SceneSettingsManager::SceneType allowedType, bool requireNumeric,
	SceneSettingsManager::SettingEntry& outEntry)
{
	using SSM = SceneSettingsManager;

	auto filename = filePath.filename().string();

	if (std::filesystem::file_size(filePath) > 1024 * 1024)
		return false;

	std::ifstream file(filePath);
	if (!file.is_open())
		return false;

	json data = json::parse(file);

	// Resolve feature name: explicit _feature field, or infer from filename stem
	std::string featureShortName = data.value("_feature", "");
	if (featureShortName.empty()) {
		auto stem = filePath.stem().string();
		auto lastUnderscore = stem.rfind('_');
		if (lastUnderscore != std::string::npos) {
			auto candidate = stem.substr(lastUnderscore + 1);
			if (Feature::FindFeatureByShortName(candidate))
				featureShortName = candidate;
		}
	}

	if (featureShortName.empty() || !Feature::FindFeatureByShortName(featureShortName))
		return false;

	if (!SSM::IsFeatureAllowedForType(allowedType, featureShortName))
		return false;

	auto* featurePtr = Feature::FindFeatureByShortName(featureShortName);

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
	if (settingCount != 1)
		return false;

	// Validate key exists in feature
	{
		json featureSettings;
		featurePtr->SaveSettings(featureSettings);
		if (!featureSettings.is_object() || !featureSettings.contains(settingKey))
			return false;
	}

	if (requireNumeric) {
		if (!IsNumericValue(settingValue) || !std::isfinite(settingValue.get<float>()))
			return false;
	}

	outEntry.featureShortName = featureShortName;
	outEntry.settingKey = settingKey;
	outEntry.value = requireNumeric ? NormalizeToFloat(settingValue) : settingValue;
	outEntry.originalValue = outEntry.value;
	outEntry.source = SSM::EntrySource::Overwrite;
	outEntry.sourceFilename = filename;
	return true;
}

void SceneSettingsManager::DiscoverOverwritesInDir(SceneType type, const std::filesystem::path& dir, TimeOfDayPeriod period)
{
	auto typeName = GetSceneTypeName(type);

	std::error_code ec;
	if (!std::filesystem::exists(dir, ec))
		return;

	logger::info("[SceneSettings] Discovering {} overwrites in: {}", typeName, dir.string());

	bool requireNumeric = (type == SceneType::TimeOfDay);
	auto& vec = GetEntriesMut(type);
	int filesFound = 0, overwritesLoaded = 0;

	for (const auto& dirEntry : std::filesystem::directory_iterator(dir, ec)) {
		if (ec) {
			logger::error("[SceneSettings] Error iterating {} overwrites: {}", typeName, ec.message());
			break;
		}
		if (!dirEntry.is_regular_file() || dirEntry.path().extension() != ".json")
			continue;

		filesFound++;
		try {
			SettingEntry entry;
			if (!ParseOverwriteFile(dirEntry.path(), type, requireNumeric, entry))
				continue;
			if (HasDuplicateEntry(type, entry.featureShortName, entry.settingKey, EntrySource::Overwrite, period))
				continue;
			entry.period = period;
			vec.push_back(std::move(entry));
			overwritesLoaded++;
		} catch (const std::exception& e) {
			logger::error("[SceneSettings] Failed to load {} overwrite '{}': {}", typeName, dirEntry.path().filename().string(), e.what());
		}
	}

	if (filesFound > 0)
		logger::info("[SceneSettings] {} overwrite scan: {} files, {} loaded", typeName, filesFound, overwritesLoaded);
}

void SceneSettingsManager::LoadAll()
{
	DiscoverOverwrites(SceneType::InteriorOnly);
	DiscoverOverwrites(SceneType::TimeOfDay);
	LoadAllUserSettings();
}

void SceneSettingsManager::LoadWeatherData()
{
	DiscoverWeatherOverwrites();
	LoadWeatherUserSettings();
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

void SceneSettingsManager::AddWeatherSetting(RE::FormID weatherId, const std::string& featureShortName,
	const std::string& settingKey, const json& value, TimeOfDayPeriod period)
{
	// All weather entries are per-period
	if (period == TimeOfDayPeriod::Count || static_cast<int>(period) < 0 || static_cast<int>(period) >= kPeriodCount)
		return;
	if (!IsNumericValue(value))
		return;
	if (!std::isfinite(value.get<float>()))
		return;
	if (HasWeatherEntryForPeriod(weatherId, featureShortName, settingKey, period, EntrySource::User))
		return;

	auto& config = GetWeatherConfigMut(weatherId);

	SettingEntry entry;
	entry.featureShortName = featureShortName;
	entry.settingKey = settingKey;
	entry.value = NormalizeToFloat(value);
	entry.originalValue = NormalizeToFloat(value);
	entry.source = EntrySource::User;
	entry.period = period;
	config.entries.push_back(std::move(entry));
	SaveAllUserSettings();
}

void SceneSettingsManager::RemoveWeatherSetting(RE::FormID weatherId, size_t index)
{
	auto it = weatherSceneConfigs.find(weatherId);
	if (it == weatherSceneConfigs.end() || index >= it->second.entries.size())
		return;
	it->second.entries.erase(it->second.entries.begin() + static_cast<ptrdiff_t>(index));
	SaveAllUserSettings();
}

void SceneSettingsManager::TogglePauseWeatherEntry(RE::FormID weatherId, size_t index)
{
	auto it = weatherSceneConfigs.find(weatherId);
	if (it == weatherSceneConfigs.end() || index >= it->second.entries.size())
		return;
	it->second.entries[index].paused = !it->second.entries[index].paused;
	SaveAllUserSettings();
}

void SceneSettingsManager::UpdateWeatherEntryValue(RE::FormID weatherId, size_t index, const json& newValue, bool deferSave)
{
	auto it = weatherSceneConfigs.find(weatherId);
	if (it == weatherSceneConfigs.end() || index >= it->second.entries.size())
		return;
	it->second.entries[index].value = NormalizeToFloat(newValue);
	if (!deferSave)
		SaveAllUserSettings();
}

void SceneSettingsManager::RevertWeatherEntryToDefault(RE::FormID weatherId, size_t index)
{
	auto it = weatherSceneConfigs.find(weatherId);
	if (it == weatherSceneConfigs.end() || index >= it->second.entries.size())
		return;
	auto& entry = it->second.entries[index];
	entry.value = entry.originalValue;
	SaveAllUserSettings();
}

void SceneSettingsManager::DeleteAllWeatherSettings(RE::FormID weatherId)
{
	auto it = weatherSceneConfigs.find(weatherId);
	if (it != weatherSceneConfigs.end()) {
		it->second.entries.clear();
		SaveAllUserSettings();
	}
}

bool SceneSettingsManager::HasWeatherEntryForPeriod(RE::FormID weatherId, const std::string& featureShortName,
	const std::string& settingKey, TimeOfDayPeriod period, std::optional<EntrySource> source) const
{
	auto it = weatherSceneConfigs.find(weatherId);
	if (it == weatherSceneConfigs.end())
		return false;
	for (const auto& e : it->second.entries)
		if (e.featureShortName == featureShortName && e.settingKey == settingKey && e.period == period &&
			(!source || e.source == *source))
			return true;
	return false;
}

// --- Per-Weather Persistence ---

bool SceneSettingsManager::IsWeatherShowTimeOfDay(RE::FormID weatherId) const
{
	auto it = weatherShowTimeOfDay_.find(weatherId);
	return it != weatherShowTimeOfDay_.end() && it->second;
}

void SceneSettingsManager::SetWeatherShowTimeOfDay(RE::FormID weatherId, bool show)
{
	weatherShowTimeOfDay_[weatherId] = show;
	SaveAllUserSettings();
}

void SceneSettingsManager::DiscoverWeatherOverwrites()
{
	auto baseDir = GetWeatherOverwritesDir();
	std::error_code ec;
	if (!std::filesystem::exists(baseDir, ec))
		return;

	logger::info("[SceneSettings] Discovering weather overwrites in: {}", baseDir.string());

	for (const auto& dirEntry : std::filesystem::directory_iterator(baseDir, ec)) {
		if (!dirEntry.is_directory())
			continue;

		auto folderName = dirEntry.path().filename().string();
		RE::FormID weatherId = Util::SpidToFormId(folderName);
		if (weatherId == 0) {
			logger::warn("[SceneSettings] Weather overwrite folder '{}' could not be resolved — skipping", folderName);
			continue;
		}

		DiscoverWeatherOverwritesForSpid(weatherId, dirEntry.path());
	}
}

void SceneSettingsManager::DiscoverWeatherOverwritesForSpid(RE::FormID weatherId, const std::filesystem::path& weatherDir)
{
	auto& config = GetWeatherConfigMut(weatherId);

	// Scan period subfolders (TOD entries)
	for (int i = 0; i < kPeriodCount; ++i) {
		auto period = static_cast<TimeOfDayPeriod>(i);
		auto periodDir = weatherDir / GetPeriodName(period);
		std::error_code ec;
		if (!std::filesystem::exists(periodDir, ec))
			continue;

		for (const auto& fileEntry : std::filesystem::directory_iterator(periodDir, ec)) {
			if (!fileEntry.is_regular_file() || fileEntry.path().extension() != ".json")
				continue;
			try {
				SettingEntry entry;
				if (!ParseOverwriteFile(fileEntry.path(), SceneType::TimeOfDay, true, entry))
					continue;
				if (HasWeatherEntryForPeriod(weatherId, entry.featureShortName, entry.settingKey, period))
					continue;
				entry.period = period;
				config.entries.push_back(std::move(entry));
			} catch (const std::exception& e) {
				logger::error("[SceneSettings] Failed to load weather overwrite '{}': {}", fileEntry.path().filename().string(), e.what());
			}
		}
	}

	// Scan files directly in the weather folder (flat → copy to all periods)
	{
		std::error_code ec;
		for (const auto& fileEntry : std::filesystem::directory_iterator(weatherDir, ec)) {
			if (!fileEntry.is_regular_file() || fileEntry.path().extension() != ".json")
				continue;
			try {
				SettingEntry parsed;
				if (!ParseOverwriteFile(fileEntry.path(), SceneType::TimeOfDay, true, parsed))
					continue;
				for (int p = 0; p < kPeriodCount; ++p) {
					auto period = static_cast<TimeOfDayPeriod>(p);
					if (HasWeatherEntryForPeriod(weatherId, parsed.featureShortName, parsed.settingKey, period))
						continue;
					SettingEntry entry = parsed;
					entry.period = period;
					config.entries.push_back(std::move(entry));
				}
			} catch (const std::exception& e) {
				logger::error("[SceneSettings] Failed to load weather overwrite '{}': {}", fileEntry.path().filename().string(), e.what());
			}
		}
	}
}

// --- Per-Weather Application ---

void SceneSettingsManager::SaveWeatherBaseline()
{
	std::map<std::string, std::set<std::string>> keysToSave;
	for (const auto& [id, config] : weatherSceneConfigs)
		for (const auto& e : config.entries)
			if (!e.paused)
				keysToSave[e.featureShortName].insert(e.settingKey);

	savedWeatherBaseline.clear();
	SaveBaselineForKeys(keysToSave, savedWeatherBaseline);
}

void SceneSettingsManager::RevertWeatherBaseline()
{
	// Invalidate TOD cache for weather-managed features: weather revert may restore a stale value
	// that predates TOD modifications, so TOD must force-reapply to correct it.
	for (const auto& [name, _] : savedWeatherBaseline) {
		lastAppliedTODFloats.erase(name);
		lastAppliedTODOther.erase(name);
	}
	lastBlendedHour = -1.0f;

	RevertFromBaseline(savedWeatherBaseline);
	lastAppliedWeatherFloats.clear();
	lastCurrentWeatherId = 0;
	lastLastWeatherId = 0;
	lastWeatherLerp = -1.0f;
	lastBlendedWeatherHour = -1.0f;
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
	// Immediately re-run TOD so it corrects keys in this frame, not the next
	if (isTimeOfDayActive)
		UpdateTimeOfDay();
	logger::info("[SceneSettings] Weather scene deactivated");
}

bool SceneSettingsManager::IsActiveWeatherSetting(const std::string& shortName, const std::string& key) const
{
	auto hasEntry = [&](RE::FormID weatherId) {
		auto it = weatherSceneConfigs.find(weatherId);
		if (it == weatherSceneConfigs.end())
			return false;
		return std::any_of(it->second.entries.begin(), it->second.entries.end(), [&](const auto& entry) {
			return !entry.paused && entry.featureShortName == shortName && entry.settingKey == key;
		});
	};

	auto sky = RE::Sky::GetSingleton();
	if (!sky || !sky->currentWeather)
		return false;

	return hasEntry(sky->currentWeather->GetFormID()) ||
	       (sky->lastWeather && hasEntry(sky->lastWeather->GetFormID()));
}

float SceneSettingsManager::GetTimeOfDayPeriodFallbackFloat(float baseVal, const std::string& shortName,
	const std::string& key, int periodIdx) const
{
	const json* value = nullptr;
	EntrySource source = EntrySource::User;
	auto period = static_cast<TimeOfDayPeriod>(periodIdx);

	for (const auto& entry : GetEntries(SceneType::TimeOfDay)) {
		if (!IsEntryActive(entry) || entry.period != period ||
			entry.featureShortName != shortName || entry.settingKey != key)
			continue;
		if (!value || (entry.source == EntrySource::Overwrite && source != EntrySource::Overwrite)) {
			value = &entry.value;
			source = entry.source;
		}
	}

	if (!value)
		return baseVal;
	if (!value->is_number()) {
		logger::warn("SceneSettingsManager: TOD fallback value for '{}' key '{}' is not numeric", shortName, key);
		return baseVal;
	}

	float result = value->get<float>();
	return std::isfinite(result) ? result : baseVal;
}

bool SceneSettingsManager::ComputeWeatherBlendedFloat(const std::string& shortName, const std::string& key,
	RE::FormID currentId, RE::FormID lastId, float weatherLerp, float& outValue)
{
	float factors[kPeriodCount];
	GetTimeOfDayFactors(factors);

	auto getBaseValue = [&]() {
		float baseVal = 0.0f;
		auto baseIt = savedWeatherBaseline.find(shortName);
		if (baseIt != savedWeatherBaseline.end() && baseIt->second.contains(key) && baseIt->second[key].is_number())
			baseVal = baseIt->second[key].get<float>();
		return baseVal;
	};

	// Helper: resolve a weather's value for a setting (always per-period / TOD blend)
	auto resolveWeatherValue = [&](RE::FormID wId, float& result) -> bool {
		auto wit = weatherSceneConfigs.find(wId);
		if (wit == weatherSceneConfigs.end())
			return false;

		struct PeriodSlot
		{
			const json* value = nullptr;
			EntrySource source = EntrySource::User;
		};
		std::array<PeriodSlot, kPeriodCount> slots{};

		for (const auto& e : wit->second.entries) {
			if (e.paused || e.featureShortName != shortName || e.settingKey != key)
				continue;
			if (e.period == TimeOfDayPeriod::Count || !e.value.is_number())
				continue;
			auto& slot = slots[static_cast<int>(e.period)];
			if (!slot.value || (e.source == EntrySource::Overwrite && slot.source != EntrySource::Overwrite)) {
				slot.value = &e.value;
				slot.source = e.source;
			}
		}

		std::vector<PeriodRef> refs;
		for (int i = 0; i < kPeriodCount; ++i)
			if (slots[i].value)
				refs.push_back({ i, slots[i].value });
		if (refs.empty())
			return false;

		result = BlendFloatForWeatherPeriods(getBaseValue(), refs, factors, shortName, key);
		return true;
	};

	float currentVal = 0.0f, lastVal = 0.0f;
	bool hasCurrent = (currentId != 0) && resolveWeatherValue(currentId, currentVal);
	bool hasLast = (lastId != 0) && resolveWeatherValue(lastId, lastVal);

	if (!hasCurrent && !hasLast)
		return false;

	if (hasCurrent && hasLast) {
		outValue = lastVal + (currentVal - lastVal) * weatherLerp;
	} else {
		float baseVal = getBaseValue();
		if (hasCurrent)
			outValue = baseVal + (currentVal - baseVal) * weatherLerp;
		else
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

	float gameHour = GetCurrentGameHour();

	// Skip if nothing changed (same weathers, same lerp, same game hour within epsilon).
	// The game hour check is critical: weather TOD blending depends on GetTimeOfDayFactors(),
	// so we must re-blend when the hour advances even if the weather transition is stable.
	if (currentId == lastCurrentWeatherId && lastId == lastLastWeatherId &&
		std::abs(weatherLerp - lastWeatherLerp) < kBlendEpsilon &&
		std::abs(gameHour - lastBlendedWeatherHour) < kHourUpdateThreshold)
		return;
	lastCurrentWeatherId = currentId;
	lastLastWeatherId = lastId;
	lastWeatherLerp = weatherLerp;
	lastBlendedWeatherHour = gameHour;

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
		if (IsFeaturePaused(shortName)) {
			lastAppliedWeatherFloats.erase(shortName);  // evict so unpause forces immediate reapply
			continue;
		}

		auto* feature = Feature::FindFeatureByShortName(shortName);
		if (!feature)
			continue;

		json current;
		feature->SaveSettings(current);
		bool dirty = false;

		for (auto& key : keys) {
			float blended = 0.0f;
			if (!ComputeWeatherBlendedFloat(shortName, key, currentId, lastId, weatherLerp, blended))
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
