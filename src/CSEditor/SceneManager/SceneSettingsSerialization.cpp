#include "SceneSettingsManager.h"

#include "SceneSettingsInternal.h"

#include <cmath>
#include <filesystem>
#include <fstream>

using namespace SceneSettingsInternal;

// --- Unified Persistence ---

static json EntryToJson(const SceneSettingsManager::SettingEntry& entry)
{
	json item = entry.serializedTemplate.is_object() ? entry.serializedTemplate : json::object();
	item["feature"] = entry.featureShortName;
	if (!entry.settingPath.empty())
		item["path"] = entry.settingPath;
	else
		item.erase("path");
	item["setting"] = entry.settingKey;
	item["value"] = entry.value;
	item["originalValue"] = entry.originalValue;
	item["paused"] = entry.paused;
	// Only our own status is ours to erase: an unrecognised one belongs to another implementation
	// round and rides through on the serialized template.
	if (entry.deleted)
		item[kStatusKey] = kStatusDeleted;
	else if (item.value(kStatusKey, std::string{}) == kStatusDeleted)
		item.erase(kStatusKey);
	if (entry.period != SceneSettingsManager::TimeOfDayPeriod::Count)
		item["period"] = SceneSettingsManager::GetPeriodName(entry.period);
	else
		item.erase("period");
	if (entry.transitionSeconds)
		item["transitionSeconds"] = *entry.transitionSeconds;
	else if (!entry.retainSerializedTransition)
		item.erase("transitionSeconds");
	return item;
}

static json UserEntriesToArray(const std::vector<SceneSettingsManager::SettingEntry>& entries, bool transitionOnly = false)
{
	json arr = json::array();
	for (const auto& entry : entries)
		if (entry.source == SceneSettingsManager::EntrySource::User &&
			(!transitionOnly || IsNumericValue(entry.value)))
			arr.push_back(EntryToJson(entry));
	return arr;
}

static void AppendRawEntries(json& arr, const std::vector<json>& rawEntries)
{
	if (!arr.is_array())
		arr = json::array();
	for (const auto& raw : rawEntries)
		arr.push_back(raw);
}

/** @brief Writes serialized entries into a keyed section, keeping whatever it already listed.
 *  Entries this build could not resolve stay in the document rather than being written over. */
static void MergeSectionEntries(json& section, json userEntries)
{
	if (auto entriesIt = section.find("entries"); entriesIt != section.end() && entriesIt->is_array())
		for (const auto& rawEntry : *entriesIt)
			userEntries.push_back(rawEntry);
	section["entries"] = std::move(userEntries);
}

/** @brief Persists the user's own time-of-day choice; a derived or mod-picked mode is not theirs to save. */
static void WriteTimeOfDayMode(json& section, const SceneSettingsManager::PeriodicSceneConfig& config)
{
	if (config.userTimeOfDayEnabled)
		section[kTimeOfDayEnabledKey] = *config.userTimeOfDayEnabled;
}

/** @brief Reads the user's time-of-day choice, consuming it from the preserved document copy. */
static void ReadTimeOfDayMode(const json& section, json& preserved, SceneSettingsManager::PeriodicSceneConfig& config,
	std::string_view context)
{
	if (auto modeIt = section.find(kTimeOfDayEnabledKey); modeIt != section.end()) {
		if (!modeIt->is_boolean()) {
			logger::warn("[SceneSettings] {} {} is not boolean - preserving", context, kTimeOfDayEnabledKey);
			return;
		}
		config.userTimeOfDayEnabled = modeIt->get<bool>();
		preserved.erase(kTimeOfDayEnabledKey);
		return;
	}
	// The legacy view toggle only ever showed per-period data that the entries imply anyway, so only
	// an opt-in carries over; an opt-out is left for the entries to decide.
	if (auto legacyIt = section.find(kLegacyShowTimeOfDayKey); legacyIt != section.end() && legacyIt->is_boolean()) {
		if (legacyIt->get<bool>())
			config.userTimeOfDayEnabled = true;
		preserved.erase(kLegacyShowTimeOfDayKey);
	}
}

static bool ShouldSerializeUserSection(const json& data, std::string_view key, bool expectObject, bool modified)
{
	auto it = data.find(std::string(key));
	return modified || it == data.end() || (expectObject ? it->is_object() : it->is_array());
}

void SceneSettingsManager::SaveAllUserSettings()
{
	if (!userSettingsDocumentLoaded)
		LoadAllUserSettings();
	const bool weatherLoaded = TryEnsureWeatherDataLoaded();
	const bool locationLoaded = TryEnsureLocationDataLoaded();
	if (!userSettingsDocumentWritable || !preservedUserSettingsRoot.is_object()) {
		if (!userSettingsWriteBlockedWarning) {
			logger::error("[SceneSettings] Refusing to overwrite SceneManager.json because its existing document is invalid");
			userSettingsWriteBlockedWarning = true;
		}
		deferredSceneChangesPending = ++deferredSaveFailures < kMaxDeferredSaveRetries;
		deferredSceneChangesDeadline = std::chrono::steady_clock::now() + kDeferredSaveRetryDelay;
		return;
	}

	auto path = GetUserSettingsFilePath();
	json data = preservedUserSettingsRoot;
	if (userTimeOfDayTransitionHours)
		data[kTimeOfDayTransitionHoursKey] = *userTimeOfDayTransitionHours;
	else
		data.erase(kTimeOfDayTransitionHoursKey);
	if (ShouldSerializeUserSection(data, "interiorOnly", false, interiorUserSettingsModified)) {
		data["interiorOnly"] = UserEntriesToArray(GetEntries(SceneType::InteriorOnly));
		AppendRawEntries(data["interiorOnly"], unresolvedUserEntries[SceneType::InteriorOnly]);
	}
	if (ShouldSerializeUserSection(data, "timeOfDay", false, timeOfDayUserSettingsModified)) {
		data["timeOfDay"] = UserEntriesToArray(GetEntries(SceneType::TimeOfDay), true);
		AppendRawEntries(data["timeOfDay"], unresolvedUserEntries[SceneType::TimeOfDay]);
	}

	// Weather entries (keyed by SPID)
	if (weatherLoaded && ShouldSerializeUserSection(data, "weather", true, weatherUserSettingsModified)) {
		json weatherObj = unresolvedWeatherUserSettings.is_object() ?
		                      unresolvedWeatherUserSettings : json::object();
		for (const auto& [weatherId, config] : weatherSceneConfigs) {
			if (weatherId == 0)
				continue;
			const auto spid = Util::FormIdToSpid(weatherId);
			auto userEntries = UserEntriesToArray(config.entries, true);
			const bool hasModeChoice = config.userTimeOfDayEnabled.has_value();

			auto rawIt = weatherObj.find(spid);
			const bool hasRaw = rawIt != weatherObj.end();
			if (userEntries.empty() && !hasModeChoice && !hasRaw)
				continue;
			if (hasRaw && !rawIt->is_object()) {
				if (userEntries.empty() && !hasModeChoice)
					continue;
				*rawIt = json::object();
			}

			json weatherEntry = hasRaw ? *rawIt : json::object();
			// A weather with no entries of its own leaves whatever the document listed untouched.
			if (!userEntries.empty())
				MergeSectionEntries(weatherEntry, std::move(userEntries));
			WriteTimeOfDayMode(weatherEntry, config);
			weatherObj[spid] = std::move(weatherEntry);
		}
		data["weather"] = std::move(weatherObj);
	}

	if (locationLoaded && ShouldSerializeUserSection(data, "location", true, locationUserSettingsModified)) {
		json locationObj = unresolvedLocationUserSettings.is_object() ?
		                       unresolvedLocationUserSettings : json::object();
		if (locationTransitionModified)
			locationObj["transitionSeconds"] = locationTransitionSeconds;
		for (const auto& [_, config] : locationSceneConfigs) {
			auto userEntries = UserEntriesToArray(config.entries);
			// A target the user took on is worth remembering even before it has a single setting.
			if (userEntries.empty() && !config.userAuthored)
				continue;
			const auto* sectionName = GetLocationSectionName(config.type);
			auto& section = locationObj[sectionName];
			if (!section.is_object())
				section = json::object();
			auto& rawConfig = section[config.formKey];
			json locationEntry = rawConfig.is_object() ? rawConfig : json::object();
			MergeSectionEntries(locationEntry, std::move(userEntries));
			locationEntry["type"] = GetLocationTargetTypeName(config.type);
			locationEntry["name"] = config.name;
			locationEntry["editorId"] = config.editorId;
			locationEntry["coc"] = config.cocCode;
			WriteTimeOfDayMode(locationEntry, config);
			rawConfig = std::move(locationEntry);
		}
		data["location"] = std::move(locationObj);
	}

	const bool saved = WriteJsonAtomically(path, data, kOverwriteJsonIndent, "SceneManager.json");
	if (saved) {
		preservedUserSettingsRoot = data;
		if (locationLoaded && locationTransitionModified) {
			if (!unresolvedLocationUserSettings.is_object())
				unresolvedLocationUserSettings = json::object();
			unresolvedLocationUserSettings["transitionSeconds"] = locationTransitionSeconds;
		}
		interiorUserSettingsModified = false;
		timeOfDayUserSettingsModified = false;
		weatherUserSettingsModified = false;
		locationUserSettingsModified = false;
		locationTransitionModified = false;
		userSettingsWriteBlockedWarning = false;
		deferredSaveFailures = 0;
		logger::info("[SceneSettings] Saved SceneManager.json");
		deferredSceneChangesPending = false;
		return;
	}

	if (++deferredSaveFailures >= kMaxDeferredSaveRetries) {
		logger::error("[SceneSettings] Giving up on SceneManager.json after {} attempts; changes stay in memory",
			deferredSaveFailures);
		deferredSceneChangesPending = false;
		return;
	}
	deferredSceneChangesPending = true;
	deferredSceneChangesDeadline = std::chrono::steady_clock::now() + kDeferredSaveRetryDelay;
}

/** @brief How an entry section treats the `period` field. */
enum class PeriodField
{
	Ignored,   ///< The layer is aperiodic.
	Required,  ///< Every entry names its period.
	Optional,  ///< Absent means the flat set.
};

static bool LoadEntryFromJson(const nlohmann::json& item, SceneSettingsManager::SettingEntry& entry,
	PeriodField periodField, const char* typeName,
	std::optional<SceneSettingsManager::SceneType> allowedSceneType = std::nullopt,
	bool requireNumericValue = false, FeatureSettingsCache* featureSettingsCache = nullptr)
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
	entry.settingPath.clear();
	if (auto it = item.find("path"); it != item.end()) {
		if (!it->is_array()) {
			logger::warn("[SceneSettings] {} entry path is not an array", typeName);
			return false;
		}
		for (const auto& part : *it) {
			if (!part.is_string()) {
				logger::warn("[SceneSettings] {} entry path contains a non-string component", typeName);
				return false;
			}
			entry.settingPath.push_back(part.get<std::string>());
		}
	}
	entry.settingKey = item["setting"].get<std::string>();
	entry.value = item["value"];
	entry.originalValue = item.value("originalValue", entry.value);
	entry.serializedTemplate = item.is_object() ? item : json::object();
	if (auto pausedIt = item.find("paused"); pausedIt != item.end() && !pausedIt->is_boolean()) {
		logger::warn("[SceneSettings] {} entry paused field is not boolean", typeName);
		return false;
	}
	entry.paused = item.value("paused", false);
	entry.deleted = false;
	if (auto statusIt = item.find(kStatusKey); statusIt != item.end()) {
		if (!statusIt->is_string()) {
			logger::warn("[SceneSettings] {} entry status field is not a string", typeName);
			return false;
		}
		entry.deleted = statusIt->get<std::string>() == kStatusDeleted;
	}
	entry.source = SSM::EntrySource::User;

	entry.period = SSM::TimeOfDayPeriod::Count;
	if (auto periodIt = item.find("period"); periodField != PeriodField::Ignored &&
											 (periodIt != item.end() || periodField == PeriodField::Required)) {
		if (periodIt == item.end() || !periodIt->is_string()) {
			logger::warn("[SceneSettings] {} entry {}.{} missing period - skipping", typeName, entry.featureShortName, entry.settingKey);
			return false;
		}
		entry.period = SSM::GetPeriodFromName(periodIt->get<std::string>());
		if (entry.period == SSM::TimeOfDayPeriod::Count) {
			logger::warn("[SceneSettings] {} entry {}.{} has invalid period '{}' - skipping", typeName, entry.featureShortName, entry.settingKey, periodIt->get<std::string>());
			return false;
		}
	}
	const bool periodic = entry.period != SSM::TimeOfDayPeriod::Count;

	const auto flatSceneType = allowedSceneType.value_or(
		periodField == PeriodField::Ignored ? SSM::SceneType::InteriorOnly : SSM::SceneType::TimeOfDay);
	// An unusable transition costs the entry its blend, never its value: the setting still has to be
	// honored, and the raw field still has to survive the round trip.
	if (auto transitionIt = item.find("transitionSeconds"); transitionIt != item.end()) {
		const auto seconds = transitionIt->is_number() ? transitionIt->get<float>() : 0.0f;
		if (flatSceneType != SSM::SceneType::Location || !transitionIt->is_number()) {
			logger::warn("[SceneSettings] {} entry transitionSeconds is not valid for this scene type; applying it without a transition", typeName);
			entry.retainSerializedTransition = true;
		} else if (!std::isfinite(seconds) || seconds < 0.0f || seconds > SSM::kMaxLocationTransitionSeconds) {
			logger::warn("[SceneSettings] {} entry transitionSeconds is outside 0..{}; applying it without a transition",
				typeName, SSM::kMaxLocationTransitionSeconds);
			entry.retainSerializedTransition = true;
		} else {
			entry.transitionSeconds = seconds;
		}
	}
	if (!SSM::IsFeatureAllowedForType(flatSceneType, entry.featureShortName)) {
		logger::warn("[SceneSettings] {} entry feature '{}' is not allowed for this scene type", typeName, entry.featureShortName);
		return false;
	}

	// Per-period entries always blend as floats, so they carry the same requirement as float-only scenes.
	const bool requireNumeric = periodic || requireNumericValue;
	WidenParsedIntegerToFloat(entry.featureShortName, entry.settingPath, entry.settingKey, entry.value);
	WidenParsedIntegerToFloat(entry.featureShortName, entry.settingPath, entry.settingKey, entry.originalValue);
	if (requireNumeric && (!IsNumericValue(entry.value) || !IsNumericValue(entry.originalValue) ||
		!std::isfinite(entry.value.get<float>()))) {
		logger::warn("[SceneSettings] {} entry {} is not a finite float setting - skipping",
			typeName, GetSettingLogName(entry.featureShortName, entry.settingPath, entry.settingKey));
		return false;
	}

	if (!ValidateSceneSettingEntry(typeName, flatSceneType, entry.featureShortName, entry.settingPath,
			entry.settingKey, entry.value, requireNumeric, featureSettingsCache) ||
		!ValidateSceneSettingEntry(typeName, flatSceneType, entry.featureShortName, entry.settingPath,
			entry.settingKey, entry.originalValue, requireNumeric, featureSettingsCache))
		return false;
	if (entry.transitionSeconds &&
		(!IsNumericValue(entry.value) || !FindAllowedCatalogSetting(
			entry.featureShortName, entry.settingPath, entry.settingKey, true))) {
		logger::warn("[SceneSettings] {} entry {} has a transition on a discrete setting; applying it instantly",
			typeName, GetSettingLogName(entry.featureShortName, entry.settingPath, entry.settingKey));
		entry.transitionSeconds.reset();
		entry.retainSerializedTransition = true;
	}

	entry.displayName = GetSceneSettingDisplayName(entry.featureShortName, entry.settingPath, entry.settingKey);
	return true;
}

void SceneSettingsManager::LoadAllUserSettings()
{
	auto path = GetUserSettingsFilePath();
	logger::info("[SceneSettings] Loading user settings from: {}", path.string());
	for (auto type : { SceneType::InteriorOnly, SceneType::TimeOfDay })
		std::erase_if(entries[type], [](const SettingEntry& entry) { return entry.source == EntrySource::User; });
	unresolvedUserEntries[SceneType::InteriorOnly].clear();
	unresolvedUserEntries[SceneType::TimeOfDay].clear();
	BumpEntryPresentationRevision();
	interiorUserSettingsModified = false;
	timeOfDayUserSettingsModified = false;
	userTimeOfDayTransitionHours.reset();
	RefreshTimeOfDayTransitionHours();
	std::error_code ec;
	if (!std::filesystem::exists(path, ec)) {
		userSettingsDocumentLoaded = true;
		userSettingsDocumentWritable = !ec;
		preservedUserSettingsRoot = json::object();
		if (ec)
			logger::error("[SceneSettings] Could not inspect SceneManager.json: {}", ec.message());
		else
			logger::info("[SceneSettings] SceneManager.json not found at {}", path.string());
		return;
	}

	try {
		std::ifstream file(path);
		if (!file.is_open()) {
			userSettingsDocumentLoaded = true;
			userSettingsDocumentWritable = false;
			logger::error("[SceneSettings] Could not open SceneManager.json for reading");
			return;
		}

		json data = json::parse(file, nullptr, false);
		userSettingsDocumentLoaded = true;
		preservedUserSettingsRoot = data;
		if (!data.is_object()) {
			userSettingsDocumentWritable = false;
			logger::error("[SceneSettings] SceneManager.json must contain a valid JSON object; automatic saves are blocked");
			return;
		}
		userSettingsDocumentWritable = true;
		FeatureSettingsCache featureSettingsCache;

		userTimeOfDayTransitionHours = ReadTimeOfDayTransitionHours(data, "SceneManager.json");
		RefreshTimeOfDayTransitionHours();

		struct EntryListSection
		{
			const char* key;
			const char* typeName;
			SceneType type;
			PeriodField periodField;
		};
		for (const auto& [sectionKey, typeName, sceneType, periodField] : {
				 EntryListSection{ "interiorOnly", "InteriorOnly", SceneType::InteriorOnly, PeriodField::Ignored },
				 EntryListSection{ "timeOfDay", "TimeOfDay", SceneType::TimeOfDay, PeriodField::Required },
			 }) {
			auto sectionIt = data.find(sectionKey);
			if (sectionIt == data.end())
				continue;
			if (!sectionIt->is_array()) {
				logger::warn("[SceneSettings] Preserving non-array {} section", sectionKey);
				continue;
			}

			auto& vec = GetEntriesMut(sceneType);
			auto& unresolved = unresolvedUserEntries[sceneType];
			int loaded = 0;
			for (const auto& item : *sectionIt) {
				SettingEntry entry;
				if (!LoadEntryFromJson(item, entry, periodField, typeName, std::nullopt, false,
						&featureSettingsCache) ||
					HasDuplicateEntry(sceneType, entry.featureShortName, entry.settingPath,
						entry.settingKey, EntrySource::User, entry.period)) {
					unresolved.push_back(item);
					continue;
				}
				vec.push_back(std::move(entry));
				loaded++;
			}
			if (loaded > 0)
				logger::info("[SceneSettings] Loaded {} {} user settings", loaded, typeName);
		}

		// Weather and location are loaded lazily once game data is available.

		logger::info("[SceneSettings] Loaded SceneManager.json (non-weather)");
	} catch (const std::exception& e) {
		userSettingsDocumentLoaded = true;
		userSettingsDocumentWritable = false;
		logger::error("[SceneSettings] Failed to load SceneManager.json: {}", e.what());
	}
}

void SceneSettingsManager::LoadLocationUserSettings(const json& data)
{
	for (auto& [_, config] : locationSceneConfigs) {
		std::erase_if(config.entries, [](const SettingEntry& entry) { return entry.source == EntrySource::User; });
		config.userTimeOfDayEnabled.reset();
	}
	unresolvedLocationUserSettings = json::object();
	locationUserSettingsModified = false;
	locationTransitionSeconds = kDefaultLocationTransitionSeconds;
	locationTransitionModified = false;
	auto locationIt = data.find("location");
	if (locationIt == data.end())
		return;
	if (!locationIt->is_object()) {
		logger::warn("[SceneSettings] Preserving non-object location section");
		return;
	}
	unresolvedLocationUserSettings = *locationIt;
	if (auto transitionIt = locationIt->find("transitionSeconds"); transitionIt != locationIt->end()) {
		if (!transitionIt->is_number())
			logger::warn("[SceneSettings] Location transitionSeconds must be numeric; preserving it");
		else if (const auto seconds = transitionIt->get<float>();
			std::isfinite(seconds) && seconds >= 0.0f && seconds <= kMaxLocationTransitionSeconds)
			locationTransitionSeconds = seconds;
		else
			logger::warn("[SceneSettings] Location transitionSeconds is outside 0..{}; preserving it",
				kMaxLocationTransitionSeconds);
	}
	FeatureSettingsCache featureSettingsCache;

	// A legacy section only feeds the canonical one: whatever loads moves there on the next save.
	const auto loadSection = [&](const char* sectionName, LocationTargetType type, std::string_view expectedType,
								 bool legacySection) {
		auto sectionIt = locationIt->find(sectionName);
		if (sectionIt == locationIt->end() || !sectionIt->is_object())
			return;
		json preservedSection = json::object();

		for (const auto& [formKey, rawConfig] : sectionIt->items()) {
			if (!IsSafeLocationFormKey(formKey)) {
				if (!formKey.empty())
					logger::warn("[SceneSettings] Ignoring location config with unusable form key '{}'", formKey);
				preservedSection[formKey] = rawConfig;
				continue;
			}
			const auto canonicalFormKey = CanonicalizeResolvedLocationFormKey(formKey);
			if (!rawConfig.is_object()) {
				preservedSection[formKey] = rawConfig;
				continue;
			}
			const auto configContext = std::format("Location config '{}'", formKey);
			std::string name;
			std::string persistedType;
			std::string cocCode;
			std::string editorId;
			persistedType = expectedType;
			if (!ReadOptionalStringField(rawConfig, "name", name, configContext) ||
				!ReadOptionalStringField(rawConfig, "type", persistedType, configContext) ||
				!ReadOptionalStringField(rawConfig, "editorId", editorId, configContext) ||
				!ReadOptionalStringField(rawConfig, "coc", cocCode, configContext)) {
				preservedSection[formKey] = rawConfig;
				continue;
			}
			if (persistedType != expectedType) {
				preservedSection[formKey] = rawConfig;
				continue;
			}
			// The canonical section's metadata outranks a legacy copy of the same target.
			if (legacySection && IsLocationTargetAuthored(type, canonicalFormKey)) {
				name.clear();
				cocCode.clear();
				editorId.clear();
			}
			// Presence in the user document is what makes a target theirs, entries or not.
			auto& config = EnsureAuthoredLocationConfig(type, canonicalFormKey, name, cocCode, editorId);
			auto preservedConfig = rawConfig;
			ReadTimeOfDayMode(rawConfig, preservedConfig, config, configContext);
			auto entriesIt = rawConfig.find("entries");
			if (entriesIt == rawConfig.end()) {
				if (!legacySection)
					preservedSection[formKey] = std::move(preservedConfig);
				continue;
			}
			if (!entriesIt->is_array()) {
				preservedSection[formKey] = std::move(preservedConfig);
				continue;
			}
			preservedConfig["entries"] = json::array();
			bool hasValidEntry = false;

			for (const auto& item : *entriesIt) {
				SettingEntry entry;
				if (!LoadEntryFromJson(item, entry, PeriodField::Optional, "Location", SceneType::Location, false,
						&featureSettingsCache)) {
					preservedConfig["entries"].push_back(item);
					continue;
				}
				hasValidEntry = true;
				if (HasLocationEntry(type, canonicalFormKey, entry.featureShortName, entry.settingPath,
						entry.settingKey, entry.period, EntrySource::User)) {
					// The canonical section already holds this setting, and it wins.
					if (!legacySection)
						preservedConfig["entries"].push_back(item);
					continue;
				}
				config.entries.push_back(std::move(entry));
			}
			// The canonical key carries the metadata and every loaded entry, so the author's spelling
			// only has to survive when it still holds entries this build rejected. Emitting it
			// unconditionally would leave an empty duplicate target beside the canonical one.
			if ((hasValidEntry || legacySection) && (formKey != canonicalFormKey || legacySection)) {
				if (preservedConfig["entries"].empty())
					continue;
				preservedConfig.erase("type");
				preservedConfig.erase("name");
				preservedConfig.erase("editorId");
				preservedConfig.erase("coc");
			}
			preservedSection[formKey] = std::move(preservedConfig);
		}
		if (legacySection && preservedSection.empty())
			unresolvedLocationUserSettings.erase(sectionName);
		else
			unresolvedLocationUserSettings[sectionName] = std::move(preservedSection);
	};

	for (const auto type : kLocationTargetTypes)
		loadSection(GetLocationSectionName(type), type, GetLocationTargetTypeName(type), false);
	loadSection(kLegacyLocationTypeSectionName, LocationTargetType::LocationType, kLegacyLocationTypeName, true);
}

void SceneSettingsManager::LoadWeatherUserSettings()
{
	for (auto& [_, config] : weatherSceneConfigs) {
		std::erase_if(config.entries, [](const SettingEntry& entry) { return entry.source == EntrySource::User; });
		config.userTimeOfDayEnabled.reset();
	}
	unresolvedWeatherUserSettings = json::object();
	weatherUserSettingsModified = false;
	if (!userSettingsDocumentLoaded || !userSettingsDocumentWritable || !preservedUserSettingsRoot.is_object())
		return;

	try {
		auto weatherIt = preservedUserSettingsRoot.find("weather");
		if (weatherIt == preservedUserSettingsRoot.end())
			return;
		if (!weatherIt->is_object()) {
			logger::warn("[SceneSettings] Preserving non-object weather section");
			return;
		}

		FeatureSettingsCache featureSettingsCache;
		logger::info("[SceneSettings] Weather section found with {} entries", weatherIt->size());
		for (const auto& [spidKey, weatherData] : weatherIt->items()) {
			RE::FormID weatherId = Util::SpidToFormId(spidKey);
			if (weatherId == 0) {
				unresolvedWeatherUserSettings[spidKey] = weatherData;
				logger::warn("[SceneSettings] Weather SPID '{}' could not be resolved - skipping", spidKey);
				continue;
			}
			if (!weatherData.is_object()) {
				unresolvedWeatherUserSettings[spidKey] = weatherData;
				logger::warn("[SceneSettings] Weather config '{}' is not an object - preserving", spidKey);
				continue;
			}
			auto preservedWeather = weatherData;
			auto& config = GetWeatherConfigMut(weatherId);
			ReadTimeOfDayMode(weatherData, preservedWeather, config, std::format("Weather config '{}'", spidKey));

			auto entriesIt = weatherData.find("entries");
			if (entriesIt == weatherData.end()) {
				unresolvedWeatherUserSettings[spidKey] = std::move(preservedWeather);
				continue;
			}
			if (!entriesIt->is_array()) {
				unresolvedWeatherUserSettings[spidKey] = preservedWeather;
				logger::warn("[SceneSettings] Weather config '{}' entries is not an array - preserving", spidKey);
				continue;
			}
			preservedWeather["entries"] = json::array();

			int loaded = 0;
			for (const auto& item : *entriesIt) {
				SettingEntry entry;
				if (!LoadEntryFromJson(item, entry, PeriodField::Optional, "Weather", std::nullopt, true,
						&featureSettingsCache)) {
					preservedWeather["entries"].push_back(item);
					continue;
				}
				if (HasWeatherEntryForPeriod(weatherId, entry.featureShortName, entry.settingPath,
						entry.settingKey, entry.period, EntrySource::User)) {
					preservedWeather["entries"].push_back(item);
					continue;
				}
				config.entries.push_back(std::move(entry));
				loaded++;
			}
			if (loaded > 0)
				logger::info("[SceneSettings] Loaded {} weather entries for {}", loaded, spidKey);
			unresolvedWeatherUserSettings[spidKey] = std::move(preservedWeather);
		}

		logger::info("[SceneSettings] Loaded weather user settings");
	} catch (const std::exception& e) {
		logger::error("[SceneSettings] Failed to load weather user settings: {}", e.what());
	}
}
