#include "SceneSettingsManager.h"

#include "Feature.h"
#include "Globals.h"
#include "State.h"
#include "Utils/FileSystem.h"
#include "Utils/Game.h"

#include <filesystem>
#include <fstream>
#include <unordered_set>

// --- Path Resolution ---

std::string SceneSettingsManager::GetSceneTypeName(SceneType type)
{
	switch (type) {
	case SceneType::InteriorOnly:
		return "InteriorOnly";
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

// --- Feature Metadata (static helpers, zero coupling) ---

std::vector<std::string> SceneSettingsManager::GetInteriorRelevantFeatureNames()
{
	// Features that are relevant for interior-only setting overrides.
	// Excludes exterior-only features: terrain, grass, LOD, sky, cloud shadows.
	static const std::unordered_set<std::string> interiorRelevantFeatures = {
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

	auto allNames = Feature::GetLoadedFeatureNames();
	std::vector<std::string> filtered;
	filtered.reserve(allNames.size());
	for (auto& name : allNames) {
		if (interiorRelevantFeatures.contains(name))
			filtered.push_back(std::move(name));
	}
	return filtered;
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

bool SceneSettingsManager::HasActiveOverwrite(SceneType type, const std::string& featureShortName, const std::string& settingKey) const
{
	for (const auto& entry : GetEntries(type)) {
		if (entry.source == EntrySource::Overwrite && !entry.paused &&
			entry.featureShortName == featureShortName && entry.settingKey == settingKey)
			return true;
	}
	return false;
}

void SceneSettingsManager::AddSetting(SceneType type, const std::string& featureShortName, const std::string& settingKey, const json& value)
{
	if (HasEntryFromSource(type, featureShortName, settingKey, EntrySource::User))
		return;

	auto& vec = GetEntriesMut(type);

	SettingEntry entry;
	entry.featureShortName = featureShortName;
	entry.settingKey = settingKey;
	entry.value = value;
	entry.source = EntrySource::User;
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
		auto filepath = GetOverwritesPath(type) / entry.sourceFilename;
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

	// Only apply if no active overwrite covers this key (overwrites take priority)
	if (isCurrentlyApplied && !vec[index].paused && !IsFeaturePaused(vec[index].featureShortName)) {
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
	// Revert interior overrides on main/loading menu (same check as LinearLighting)
	if (isCurrentlyApplied) {
		bool isMainOrLoading = globals::state->isMainMenuOpen || globals::state->isLoadingMenuOpen;
		if (isMainOrLoading) {
			RevertToExteriorSettings();
			isCurrentlyApplied = false;
		}
	}

	if (queuedCellTransition) {
		queuedCellTransition = false;
		OnCellTransition();
	}
}

void SceneSettingsManager::OnCellTransition()
{
	// Match Skylighting's interior detection: sky mode != kFull
	bool interior = true;
	if (auto sky = globals::game::sky)
		interior = sky->mode.get() != RE::Sky::Mode::kFull;

	if (interior && !isCurrentlyApplied) {
		SaveExteriorSettings(SceneType::InteriorOnly);
		ApplySettings(SceneType::InteriorOnly);
		isCurrentlyApplied = true;
	} else if (!interior && isCurrentlyApplied) {
		RevertToExteriorSettings();
		isCurrentlyApplied = false;
	}
}

void SceneSettingsManager::ReapplyIfActive()
{
	if (!isCurrentlyApplied)
		return;

	// Full revert + re-apply so removed/paused entries get exterior values restored
	RevertToExteriorSettings();
	SaveExteriorSettings(SceneType::InteriorOnly);
	ApplySettings(SceneType::InteriorOnly);
}

bool SceneSettingsManager::IsSettingControlled(const std::string& featureShortName, const std::string& settingKey) const
{
	if (!isCurrentlyApplied)
		return false;
	if (IsFeaturePaused(featureShortName))
		return false;

	// Check all scene types for active overrides
	for (const auto& [type, vec] : entries) {
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
	if (!isCurrentlyApplied)
		return false;

	for (const auto& [type, vec] : entries) {
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

void SceneSettingsManager::SaveExteriorSettings(SceneType type)
{
	// Collect which keys per feature need saving (only the keys we'll override)
	std::map<std::string, std::set<std::string>> keysToSave;
	for (const auto& entry : GetEntries(type)) {
		if (IsEntryActive(entry))
			keysToSave[entry.featureShortName].insert(entry.settingKey);
	}

	// Save only the specific keys we'll override, not the entire settings blob
	for (const auto& [shortName, keys] : keysToSave) {
		auto* feature = Feature::FindFeatureByShortName(shortName);
		if (!feature)
			continue;

		json fullSettings;
		feature->SaveSettings(fullSettings);

		// Merge into existing saved settings (don't overwrite keys saved by other scene types)
		json& partial = savedExteriorSettings[shortName];
		if (!partial.is_object())
			partial = json::object();

		for (const auto& key : keys) {
			if (fullSettings.contains(key) && !partial.contains(key))
				partial[key] = fullSettings[key];
		}
	}
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

void SceneSettingsManager::RevertToExteriorSettings()
{
	for (const auto& [shortName, savedKeys] : savedExteriorSettings) {
		auto* feature = Feature::FindFeatureByShortName(shortName);
		if (!feature)
			continue;

		json current;
		feature->SaveSettings(current);

		for (auto& [key, val] : savedKeys.items())
			current[key] = val;

		feature->LoadSettings(current);
	}
	savedExteriorSettings.clear();
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

			if (!Feature::FindFeatureByShortName(entry.featureShortName))
				continue;

			if (!HasEntryFromSource(type, entry.featureShortName, entry.settingKey, EntrySource::User))
				vec.push_back(std::move(entry));
		}

		logger::info("[SceneSettings] Loaded {} {} user settings", data.size(), typeName);
	} catch (const std::exception& e) {
		logger::error("[SceneSettings] Failed to load {} settings: {}", typeName, e.what());
	}
}

void SceneSettingsManager::DiscoverOverwrites(SceneType type)
{
	auto overwritesPath = GetOverwritesPath(type);
	auto typeName = GetSceneTypeName(type);

	logger::info("[SceneSettings] Discovering {} overwrites in: {}", typeName, overwritesPath.string());

	std::error_code ec;
	if (!std::filesystem::exists(overwritesPath, ec)) {
		logger::info("[SceneSettings] Overwrites directory does not exist: {}", overwritesPath.string());
		return;
	}

	auto& vec = GetEntriesMut(type);
	int filesFound = 0;
	int overwritesLoaded = 0;
	for (const auto& dirEntry : std::filesystem::directory_iterator(overwritesPath, ec)) {
		if (ec) {
			logger::error("[SceneSettings] Error iterating {} overwrites directory: {}", typeName, ec.message());
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

			// Resolve feature name: explicit _feature field, or infer from filename (ModName_FeatureName.json)
			std::string featureShortName = data.value("_feature", "");
			if (featureShortName.empty()) {
				auto stem = dirEntry.path().stem().string();
				auto lastUnderscore = stem.rfind('_');
				if (lastUnderscore != std::string::npos) {
					auto candidate = stem.substr(lastUnderscore + 1);
					if (Feature::FindFeatureByShortName(candidate)) {
						featureShortName = candidate;
						logger::info("[SceneSettings] Inferred feature '{}' from filename '{}'", featureShortName, filename);
					}
				}
			}

			if (featureShortName.empty()) {
				logger::warn("[SceneSettings] Skipping overwrite '{}': no _feature field and could not infer feature from filename", filename);
				continue;
			}

			if (!Feature::FindFeatureByShortName(featureShortName)) {
				logger::warn("[SceneSettings] Skipping overwrite '{}': feature '{}' not found", filename, featureShortName);
				continue;
			}

			// Count non-metadata settings — must have exactly one
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

			if (HasEntryFromSource(type, featureShortName, settingKey, EntrySource::Overwrite)) {
				logger::warn("[SceneSettings] Skipping overwrite '{}': duplicate overwrite for {}.{}", filename, featureShortName, settingKey);
				continue;
			}

			SettingEntry entry;
			entry.featureShortName = featureShortName;
			entry.settingKey = settingKey;
			entry.value = settingValue;
			entry.source = EntrySource::Overwrite;
			entry.sourceFilename = filename;

			vec.push_back(std::move(entry));

			overwritesLoaded++;
			logger::info("[SceneSettings] Loaded {} overwrite: {} -> {}.{}", typeName, filename, featureShortName, settingKey);
		} catch (const std::exception& e) {
			logger::error("[SceneSettings] Failed to load {} overwrite '{}': {}", typeName, filename, e.what());
		}
	}

	logger::info("[SceneSettings] {} overwrite discovery complete. Found {} JSON files, loaded {} overwrites", typeName, filesFound, overwritesLoaded);
}

void SceneSettingsManager::LoadAll()
{
	DiscoverOverwrites(SceneType::InteriorOnly);
	LoadUserSettings(SceneType::InteriorOnly);
	// Future: add other scene types here
}
