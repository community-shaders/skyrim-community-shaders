#include "SceneSettingsManager.h"

#include "Feature.h"
#include "Globals.h"
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
	static const char* names[] = { "Dawn", "Sunrise", "Day", "Sunset", "Dusk", "Night" };
	int idx = static_cast<int>(period);
	return (idx >= 0 && idx < kPeriodCount) ? names[idx] : "Unknown";
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
	if (calendar && calendar->gameHour)
		return std::clamp(calendar->gameHour->value, 0.0f, 24.0f);

	auto sky = globals::game::sky;
	return sky ? std::clamp(sky->currentGameHour, 0.0f, 24.0f) : 12.0f;
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
			// Inside this period — check if we're in a transition zone
			float distFromStart = h - start;
			float distFromEnd = end - h;

			if (distFromStart < kTransitionHours) {
				// Blending in from previous period
				float t = distFromStart / kTransitionHours;
				outFactors[i] = t;
				outFactors[(i + kPeriodCount - 1) % kPeriodCount] = 1.0f - t;
			} else if (distFromEnd < kTransitionHours) {
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
	if (HasDuplicateEntry(type, featureShortName, settingKey, EntrySource::User, period))
		return;

	auto& vec = GetEntriesMut(type);

	SettingEntry entry;
	entry.featureShortName = featureShortName;
	entry.settingKey = settingKey;
	entry.value = value;
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
		if (std::filesystem::remove(filepath, ec))
			logger::info("[SceneSettings] Deleted overwrite file: {}", filepath.string());
		else
			logger::error("[SceneSettings] Failed to delete overwrite file: {} ({})", filepath.string(), ec.message());
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
	std::error_code ec;

	auto& vec = GetEntriesMut(type);
	for (const auto& entry : vec) {
		if (entry.source == EntrySource::Overwrite && !entry.sourceFilename.empty())
			std::filesystem::remove(overwritesPath / entry.sourceFilename, ec);
	}

	std::erase_if(vec, [](const SettingEntry& e) {
		return e.source == EntrySource::Overwrite;
	});

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

	vec[index].value = newValue;

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
	// Revert overrides on main/loading menu (same check as LinearLighting)
	bool isMainOrLoading = globals::game::ui &&
	                       (globals::game::ui->IsMenuOpen(RE::MainMenu::MENU_NAME) || globals::game::ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME));

	if (isMainOrLoading) {
		if (isCurrentlyApplied) {
			RevertToExteriorSettings();
			isCurrentlyApplied = false;
		}
		if (isTimeOfDayActive)
			DeactivateTimeOfDay();
		return;
	}

	if (queuedCellTransition) {
		queuedCellTransition = false;
		OnCellTransition();
	}

	// Continuously update time-of-day blended values when exterior
	if (isTimeOfDayActive)
		UpdateTimeOfDay();
}

void SceneSettingsManager::OnCellTransition()
{
	// Match Skylighting's interior detection: sky mode != kFull
	bool interior = true;
	if (auto sky = globals::game::sky)
		interior = sky->mode.get() != RE::Sky::Mode::kFull;

	if (interior) {
		// Entering interior: deactivate TOD first, then apply interior overrides
		if (isTimeOfDayActive)
			DeactivateTimeOfDay();
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

	// Determine if we're in an exterior right now
	bool isExterior = false;
	if (auto sky = globals::game::sky)
		isExterior = sky->mode.get() == RE::Sky::Mode::kFull;

	bool hasEntries = !GetEntries(SceneType::TimeOfDay).empty();

	if (isTimeOfDayActive) {
		if (hasEntries) {
			// Re-blend with updated entries
			RevertTimeOfDayBaseline();
			SaveTimeOfDayBaseline();
			ApplyTimeOfDayBlended();
		} else {
			// All entries removed — deactivate
			DeactivateTimeOfDay();
		}
	} else if (isExterior && hasEntries && !isCurrentlyApplied) {
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
	if (isTimeOfDayActive || GetEntries(SceneType::TimeOfDay).empty())
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

	// Group active entries by feature, using pointers to avoid JSON copies
	struct PeriodRef
	{
		int periodIdx;
		const json* value;
	};
	std::map<std::string, std::map<std::string, std::vector<PeriodRef>>> featureSettings;
	for (const auto& entry : GetEntries(SceneType::TimeOfDay)) {
		if (!IsEntryActive(entry) || entry.period == TimeOfDayPeriod::Count)
			continue;
		featureSettings[entry.featureShortName][entry.settingKey].push_back(
			{ static_cast<int>(entry.period), &entry.value });
	}

	for (auto& [shortName, settingsMap] : featureSettings) {
		// Compute blended values and check which keys actually changed
		std::vector<std::pair<std::string, json>> dirtyKeys;

		for (auto& [key, periodRefs] : settingsMap) {
			// Get baseline value (saved once at activation, never changes)
			const json* baseline = nullptr;
			auto baseIt = savedTimeOfDayBaseline.find(shortName);
			if (baseIt != savedTimeOfDayBaseline.end() && baseIt->second.contains(key))
				baseline = &baseIt->second[key];
			if (!baseline)
				continue;

			auto type = DetectSettingType(*baseline);

			if (type == SettingType::Float) {
				float baseVal = baseline->get<float>();
				if (!std::isfinite(baseVal))
					baseVal = 0.0f;
				float result = 0.0f;
				float coveredFactor = 0.0f;

				for (auto& pr : periodRefs) {
					float f = factors[pr.periodIdx];
					if (f > 0.0f) {
						float periodVal = pr.value->get<float>();
						if (!std::isfinite(periodVal))
							periodVal = 0.0f;
						result += f * periodVal;
						coveredFactor += f;
					}
				}
				result += (1.0f - coveredFactor) * baseVal;

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
				// Non-float: snap to dominant period's value, or baseline if none
				json blendedValue = *baseline;
				for (auto& pr : periodRefs)
					if (static_cast<TimeOfDayPeriod>(pr.periodIdx) == dominant)
						blendedValue = *pr.value;

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
		for (const auto& item : data) {
			if (!item.contains("feature") || !item.contains("setting") || !item.contains("value"))
				continue;

			SettingEntry entry;
			entry.featureShortName = item["feature"].get<std::string>();
			entry.settingKey = item["setting"].get<std::string>();
			entry.value = item["value"];
			entry.paused = item.value("paused", false);
			entry.source = EntrySource::User;

			// Parse period for TimeOfDay entries
			if (type == SceneType::TimeOfDay) {
				if (!item.contains("period")) {
					logger::warn("SceneSettingsManager: TimeOfDay entry for feature '{}' key '{}' is missing 'period' — skipping to avoid ghost entry",
						entry.featureShortName, entry.settingKey);
					continue;
				}
				entry.period = GetPeriodFromName(item["period"].get<std::string>());
				if (entry.period == TimeOfDayPeriod::Count) {
					logger::warn("SceneSettingsManager: TimeOfDay entry for feature '{}' key '{}' has invalid period '{}' — skipping",
						entry.featureShortName, entry.settingKey, item["period"].get<std::string>());
					continue;  // Invalid period name
				}
			}

			if (!Feature::FindFeatureByShortName(entry.featureShortName))
				continue;

			if (HasDuplicateEntry(type, entry.featureShortName, entry.settingKey, EntrySource::User, entry.period))
				continue;
			vec.push_back(std::move(entry));
		}

		logger::info("[SceneSettings] Loaded {} {} user settings", data.size(), typeName);
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

			// Duplicate check
			if (HasDuplicateEntry(type, featureShortName, settingKey, EntrySource::Overwrite, period))
				continue;

			SettingEntry entry;
			entry.featureShortName = featureShortName;
			entry.settingKey = settingKey;
			entry.value = settingValue;
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
}
