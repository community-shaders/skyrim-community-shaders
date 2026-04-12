#include "SettingManager.h"

#include "WeatherManager.h"
#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <string>
#include <tuple>

static const char* const timeOfDayNames[] = { "Dawn", "Sunrise", "Day", "Sunset", "Dusk", "Night", "InteriorDay", "InteriorNight" };

static bool TryParseBool(const std::string& a_value, bool& a_out)
{
	std::string s = a_value;
	s.erase(0, s.find_first_not_of(" \t\r\n"));
	s.erase(s.find_last_not_of(" \t\r\n") + 1);
	std::transform(s.begin(), s.end(), s.begin(), ::tolower);
	if (s == "true" || s == "1") {
		a_out = true;
		return true;
	}
	if (s == "false" || s == "0") {
		a_out = false;
		return true;
	}
	return false;
}

static bool TryParseFloat(const std::string& a_value, float& a_out)
{
	if (a_value.empty())
		return false;
	try {
		size_t pos;
		a_out = std::stof(a_value, &pos);
		for (size_t i = pos; i < a_value.size(); ++i) {
			if (!std::isspace(static_cast<unsigned char>(a_value[i]))) {
				return false;
			}
		}
		return true;
	} catch (...) {
		return false;
	}
}

static bool TryParseWeatherID(const std::string& a_key, uint32_t& a_out)
{
	const std::string prefix = "weather_";
	if (a_key.size() <= prefix.size() || a_key.compare(0, prefix.size(), prefix) != 0) {
		return false;
	}

	try {
		std::string idStr = a_key.substr(prefix.size());
		size_t pos;
		a_out = std::stoul(idStr, &pos);
		return pos == idStr.size();
	} catch (...) {
		return false;
	}
}

SettingManager& SettingManager::GetSingleton()
{
	static SettingManager instance;
	return instance;
}

void SettingManager::RegisterSettingInternal(Setting& setting)
{
	// Already holding unique_lock from public Register... methods
	if (categories.find(setting.category) == categories.end()) {
		categoryOrder.push_back(setting.category);
	}

	auto& cat = categories[setting.category];
	auto it = cat.settings.find(setting.key);

	if (it == cat.settings.end()) {
		setting.id = static_cast<uint32_t>(allSettings.size());
		setting.lastSavedValue = setting.currentValue;
		cat.settings[setting.key] = setting.id;
		cat.settingOrder.push_back(setting.key);
		allSettings.push_back(setting);
	} else {
		// Update existing setting info but keep the same ID
		uint32_t existingID = it->second;
		setting.id = existingID;
		setting.lastSavedValue = allSettings[existingID].lastSavedValue;
		allSettings[existingID] = setting;
	}
}

void SettingManager::RegisterBoolSetting(const std::string& key, const std::string& category,
	bool defaultValue, bool hasWeatherSupport)
{
	std::unique_lock lock(mutex);
	Setting setting;
	setting.key = key;
	setting.category = category;
	setting.type = SettingType::Bool;
	setting.hasWeatherSupport = hasWeatherSupport;
	setting.defaultValue = defaultValue;
	setting.currentValue = defaultValue;

	RegisterSettingInternal(setting);
}

void SettingManager::RegisterFloatSetting(const std::string& key, const std::string& category,
	float defaultValue, float minValue, float maxValue, float step, bool hasWeatherSupport)
{
	std::unique_lock lock(mutex);
	Setting setting;
	setting.key = key;
	setting.category = category;
	setting.type = SettingType::Float;
	setting.hasWeatherSupport = hasWeatherSupport;
	setting.defaultValue = defaultValue;
	setting.currentValue = defaultValue;
	setting.minValue = minValue;
	setting.maxValue = maxValue;
	setting.step = step;

	RegisterSettingInternal(setting);
}

void SettingManager::RegisterTimeOfDaySetting(const std::string& key, const std::string& category,
	float defaultValue, float minValue, float maxValue, float step, bool hasWeatherSupport)
{
	std::unique_lock lock(mutex);
	TimeOfDayValue timeOfDayDefault;
	for (int i = 0; i < TimeOfDayValue::Total; ++i) {
		timeOfDayDefault.values[i] = defaultValue;
	}

	Setting setting;
	setting.key = key;
	setting.category = category;
	setting.type = SettingType::TimeOfDay;
	setting.hasWeatherSupport = hasWeatherSupport;
	setting.defaultValue = timeOfDayDefault;
	setting.currentValue = timeOfDayDefault;
	setting.minValue = minValue;
	setting.maxValue = maxValue;
	setting.step = step;

	RegisterSettingInternal(setting);
}

void SettingManager::RegisterColorTimeOfDaySetting(const std::string& key, const std::string& category,
	float3 defaultValue, bool hasWeatherSupport)
{
	std::unique_lock lock(mutex);
	ColorTimeOfDayValue colorTimeOfDayDefault;
	for (int i = 0; i < ColorTimeOfDayValue::Total; ++i) {
		colorTimeOfDayDefault.values[i] = defaultValue;
	}

	Setting setting;
	setting.key = key;
	setting.category = category;
	setting.type = SettingType::ColorTimeOfDay;
	setting.hasWeatherSupport = hasWeatherSupport;
	setting.defaultValue = colorTimeOfDayDefault;
	setting.currentValue = colorTimeOfDayDefault;

	RegisterSettingInternal(setting);
}

template <typename T>
T SettingManager::GetValue(const std::string& key, const std::string& category, bool rawValue)
{
	std::shared_lock lock(mutex);
	uint32_t id = GetSettingIDInternal(key, category);
	if (id == 0xFFFFFFFF) {
		return T{};
	}
	return GetValueInternal<T>(id, rawValue);
}

template <typename T>
T SettingManager::GetValue(uint32_t id, bool rawValue)
{
	std::shared_lock lock(mutex);
	return GetValueInternal<T>(id, rawValue);
}

template <typename T>
T SettingManager::GetValueInternal(uint32_t id, bool rawValue) const
{
	if (id >= allSettings.size()) {
		return T{};
	}

	const auto& setting = allSettings[id];
	const auto& categorySettings = categories.at(setting.category);

	if (setting.hasWeatherSupport) {
		bool shouldIgnoreWeather = (interiorFactor > 0.5f) ?
		                               categorySettings.ignoreWeatherSystemInterior :
		                               categorySettings.ignoreWeatherSystem;

		if (shouldIgnoreWeather) {
			return std::get<T>(setting.currentValue);
		}

		auto currentIt = weatherData.find(currentWeatherID);
		auto lastIt = weatherData.find(lastWeatherID);

		if (currentIt != weatherData.end() || lastIt != weatherData.end()) {
			SettingValue currentValue = setting.currentValue;
			SettingValue lastValue = setting.currentValue;

			if (currentIt != weatherData.end() && id < currentIt->second.size()) {
				currentValue = currentIt->second[id];
			}

			if (lastIt != weatherData.end() && id < lastIt->second.size()) {
				lastValue = lastIt->second[id];
			}

			if (rawValue) {
				return std::get<T>(weatherBlendFactor > 0.5f ? currentValue : lastValue);
			}

			SettingValue blendedValue = InterpolateValues(lastValue, currentValue, weatherBlendFactor);
			return std::get<T>(blendedValue);
		}
	}

	return std::get<T>(setting.currentValue);
}

template <typename T>
void SettingManager::SetValue(const std::string& key, const std::string& category, const T& value)
{
	std::unique_lock lock(mutex);
	uint32_t id = GetSettingIDInternal(key, category);
	if (id != 0xFFFFFFFF) {
		SetValueInternal<T>(id, value);
	}
}

template <typename T>
void SettingManager::SetValue(uint32_t id, const T& value)
{
	std::unique_lock lock(mutex);
	SetValueInternal<T>(id, value);
}

template <typename T>
void SettingManager::SetValueInternal(uint32_t id, const T& value)
{
	if (id >= allSettings.size()) {
		return;
	}

	auto& setting = allSettings[id];
	const auto& categorySettings = categories.at(setting.category);

	if (setting.hasWeatherSupport) {
		bool shouldIgnoreWeather = (interiorFactor > 0.5f) ?
		                               categorySettings.ignoreWeatherSystemInterior :
		                               categorySettings.ignoreWeatherSystem;

		if (shouldIgnoreWeather) {
			setting.currentValue = value;
			return;
		}

		uint32_t targetWeatherID = (weatherBlendFactor > 0.5f) ? currentWeatherID : lastWeatherID;

		// Update all weather IDs sharing the same file
		auto& weatherManager = WeatherManager::GetSingleton();
		auto* entry = weatherManager.FindWeatherEntry(targetWeatherID);

		if (entry) {
			for (uint32_t linkedID : entry->weatherIDs) {
				auto& data = weatherData[linkedID];
				if (data.size() < allSettings.size()) {
					data.resize(allSettings.size());
					// Initialize with current values from allSettings
					for (size_t i = 0; i < allSettings.size(); ++i) {
						data[i] = allSettings[i].currentValue;
					}
				}
				data[id] = value;
			}
		} else {
			// Fallback: update target ID only
			auto& data = weatherData[targetWeatherID];
			if (data.size() < allSettings.size()) {
				data.resize(allSettings.size());
				for (size_t i = 0; i < allSettings.size(); ++i) {
					data[i] = allSettings[i].currentValue;
				}
			}
			data[id] = value;
		}
		return;
	}

	setting.currentValue = value;
}

uint32_t SettingManager::GetSettingID(const std::string& key, const std::string& category) const
{
	std::shared_lock lock(mutex);
	return GetSettingIDInternal(key, category);
}

uint32_t SettingManager::GetSettingIDInternal(const std::string& key, const std::string& category) const
{
	auto catIt = categories.find(category);
	if (catIt != categories.end()) {
		auto setIt = catIt->second.settings.find(key);
		if (setIt != catIt->second.settings.end()) {
			return setIt->second;
		}
	}
	return 0xFFFFFFFF;
}

float SettingManager::GetInterpolatedTimeOfDayValue(const std::string& key, const std::string& category)
{
	std::shared_lock lock(mutex);
	uint32_t id = GetSettingIDInternal(key, category);
	if (id == 0xFFFFFFFF)
		return 0.0f;
	TimeOfDayValue timeOfDayValue = GetValueInternal<TimeOfDayValue>(id);
	return ComputeTimeOfDayInterpolation(timeOfDayValue);
}

float3 SettingManager::GetInterpolatedColorTimeOfDayValue(const std::string& key, const std::string& category)
{
	std::shared_lock lock(mutex);
	uint32_t id = GetSettingIDInternal(key, category);
	if (id == 0xFFFFFFFF)
		return {};
	ColorTimeOfDayValue colorTimeOfDayValue = GetValueInternal<ColorTimeOfDayValue>(id);
	return ComputeColorTimeOfDayInterpolation(colorTimeOfDayValue);
}

bool SettingManager::HasSetting(const std::string& key, const std::string& category) const
{
	std::shared_lock lock(mutex);
	auto categoryIt = categories.find(category);
	return categoryIt != categories.end() && categoryIt->second.settings.find(key) != categoryIt->second.settings.end();
}

const Setting* SettingManager::GetSettingInfo(const std::string& key, const std::string& category) const
{
	std::shared_lock lock(mutex);
	uint32_t id = GetSettingIDInternal(key, category);
	if (id < allSettings.size())
		return &allSettings[id];
	return nullptr;
}

const Setting* SettingManager::GetSettingInfo(uint32_t id) const
{
	std::shared_lock lock(mutex);
	if (id < allSettings.size()) {
		return &allSettings[id];
	}
	return nullptr;
}

std::vector<std::string> SettingManager::GetSettingsByCategory(const std::string& category) const
{
	std::shared_lock lock(mutex);
	auto categoryIt = categories.find(category);
	if (categoryIt != categories.end()) {
		return categoryIt->second.settingOrder;
	}
	return {};
}

std::vector<std::string> SettingManager::GetAllCategories() const
{
	std::shared_lock lock(mutex);
	return categoryOrder;
}

bool SettingManager::CategoryHasWeatherSupport(const std::string& category) const
{
	std::shared_lock lock(mutex);
	auto categoryIt = categories.find(category);
	if (categoryIt == categories.end())
		return false;

	for (const auto& [key, id] : categoryIt->second.settings) {
		if (allSettings[id].hasWeatherSupport) {
			return true;
		}
	}
	return false;
}

void SettingManager::SetWeatherBlendFactors(uint32_t newCurrentWeatherID, uint32_t newLastWeatherID, float blendFactor)
{
	std::unique_lock lock(mutex);
	currentWeatherID = newCurrentWeatherID;
	lastWeatherID = newLastWeatherID;
	weatherBlendFactor = blendFactor;
}

void SettingManager::LoadWeatherSettings(const std::vector<uint32_t>& weatherIDs, const std::string& filePath)
{
	if (!std::filesystem::exists(filePath)) {
		logger::warn("[SettingManager] Weather file not found: {}", filePath);
		return;
	}

	if (weatherIDs.empty()) {
		return;
	}

	auto writeTime = std::filesystem::last_write_time(filePath);

	// Snapshot allSettings and check for changes under a short shared lock
	std::vector<Setting> settingsCopy;
	{
		std::shared_lock lock(mutex);
		auto it = weatherFileWriteTimes.find(filePath);
		if (it != weatherFileWriteTimes.end() && it->second == writeTime) {
			return;  // File unchanged since last load
		}
		settingsCopy = allSettings;
	}

	// Do all file I/O without holding any lock
	std::vector<SettingValue> loadedValues(settingsCopy.size());
	for (size_t i = 0; i < settingsCopy.size(); ++i) {
		loadedValues[i] = settingsCopy[i].currentValue;
	}
	for (auto& setting : settingsCopy) {
		if (setting.hasWeatherSupport) {
			LoadSettingFromFile(filePath, setting.category, setting.key, setting);
			loadedValues[setting.id] = setting.currentValue;
		}
	}

	// Store loaded values for all provided weather IDs under write lock
	{
		std::unique_lock lock(mutex);
		weatherFileWriteTimes[filePath] = writeTime;
		for (uint32_t weatherID : weatherIDs) {
			weatherData[weatherID] = loadedValues;
			lastSavedWeatherData[weatherID] = loadedValues;
		}
	}
}

void SettingManager::SaveWeatherSettings(const std::string& weatherKey, const std::string& filePath)
{
	uint32_t weatherID;
	if (!TryParseWeatherID(weatherKey, weatherID)) {
		logger::error("[SettingManager] Invalid weather key: {}", weatherKey);
		return;
	}

	std::vector<std::tuple<std::string, std::string, Setting>> settingsToWrite;

	{
		std::unique_lock lock(mutex);
		auto weatherIt = weatherData.find(weatherID);
		if (weatherIt == weatherData.end()) {
			return;
		}

		auto lastIt = lastSavedWeatherData.find(weatherID);
		const auto& weatherValues = weatherIt->second;

		for (const auto& setting : allSettings) {
			if (setting.hasWeatherSupport && setting.id < weatherValues.size()) {
				bool changed = true;
				if (lastIt != lastSavedWeatherData.end() && setting.id < lastIt->second.size()) {
					if (weatherValues[setting.id] == lastIt->second[setting.id]) {
						changed = false;
					}
				}

				if (changed) {
					Setting tempSetting = setting;
					tempSetting.currentValue = weatherValues[setting.id];
					settingsToWrite.emplace_back(setting.category, setting.key, tempSetting);
				}
			}
		}

		// Update last saved state
		lastSavedWeatherData[weatherID] = weatherValues;
	}

	if (settingsToWrite.empty()) {
		return;
	}

	// Perform IO outside of lock to prevent deadlocks
	for (const auto& [category, key, setting] : settingsToWrite) {
		SaveSettingToFile(filePath, category, key, setting);
	}

	// Flush Windows .ini cache to disk
	WritePrivateProfileStringA(NULL, NULL, NULL, filePath.c_str());
}

void SettingManager::SaveAllWeatherSettings()
{
	auto& weatherManager = WeatherManager::GetSingleton();
	const auto& weatherFiles = weatherManager.GetWeatherFiles();

	// Deduplicate by file path to avoid redundant IO and overwrite bugs
	std::unordered_map<std::string, std::string> uniqueFiles;
	for (const auto& [weatherKey, filePath] : weatherFiles) {
		if (uniqueFiles.find(filePath) == uniqueFiles.end()) {
			uniqueFiles[filePath] = weatherKey;
		}
	}

	for (const auto& [filePath, weatherKey] : uniqueFiles) {
		SaveWeatherSettings(weatherKey, filePath);
	}
}

void SettingManager::ReloadAllWeatherSettings()
{
	{
		std::unique_lock lock(mutex);
		weatherData.clear();
		weatherFileWriteTimes.clear();  // Force re-read of all weather files
	}
	auto& weatherManager = WeatherManager::GetSingleton();
	weatherManager.Initialize();
}

void SettingManager::SetTimeOfDayData(const float newTimeOfDay1[4], const float newTimeOfDay2[4], float newInteriorFactor)
{
	std::unique_lock lock(mutex);
	memcpy(timeOfDay1, newTimeOfDay1, sizeof(timeOfDay1));
	memcpy(timeOfDay2, newTimeOfDay2, sizeof(timeOfDay2));
	interiorFactor = newInteriorFactor;
}

void SettingManager::LoadFromFile(const std::string& filePath)
{
	std::filesystem::path absPath = std::filesystem::absolute(filePath);

	if (!std::filesystem::exists(absPath)) {
		logger::warn("[SettingManager] Settings file not found: {}, using defaults", absPath.string());
		return;
	}

	auto writeTime = std::filesystem::last_write_time(absPath);

	// Snapshot settings and identify weather categories under brief shared lock
	std::vector<Setting> settingsCopy;
	std::vector<std::string> weatherCategories;
	{
		std::shared_lock lock(mutex);
		if (writeTime == lastMainIniWriteTime) {
			return;  // File unchanged since last load
		}
		settingsCopy = allSettings;
		for (const auto& [catName, catData] : categories) {
			for (const auto& [key, settingID] : catData.settings) {
				if (allSettings[settingID].hasWeatherSupport) {
					weatherCategories.push_back(catName);
					break;
				}
			}
		}
	}

	// Do all file I/O without holding any lock
	std::string absPathStr = absPath.string();
	for (auto& setting : settingsCopy) {
		LoadSettingFromFile(absPathStr, setting.category, setting.key, setting);
		setting.lastSavedValue = setting.currentValue;
	}

	// Load weather ignore settings (I/O without lock)
	struct WeatherIgnoreData
	{
		std::string category;
		bool ignoreWeatherSystem = false;
		bool ignoreWeatherSystemInterior = true;
	};
	std::vector<WeatherIgnoreData> weatherIgnoreResults;
	for (const auto& category : weatherCategories) {
		WeatherIgnoreData data;
		data.category = category;

		char buffer[256];
		GetPrivateProfileStringA(category.c_str(), "IgnoreWeatherSystem", "false", buffer, sizeof(buffer), absPathStr.c_str());
		bool parsed;
		if (TryParseBool(buffer, parsed)) {
			data.ignoreWeatherSystem = parsed;
		}

		GetPrivateProfileStringA(category.c_str(), "IgnoreWeatherSystemInterior", "true", buffer, sizeof(buffer), absPathStr.c_str());
		if (TryParseBool(buffer, parsed)) {
			data.ignoreWeatherSystemInterior = parsed;
		}

		weatherIgnoreResults.push_back(std::move(data));
	}

	// Store results under unique lock
	{
		std::unique_lock lock(mutex);
		lastMainIniWriteTime = writeTime;
		allSettings = std::move(settingsCopy);

		// Apply weather ignore settings
		for (const auto& data : weatherIgnoreResults) {
			auto catIt = categories.find(data.category);
			if (catIt != categories.end()) {
				catIt->second.ignoreWeatherSystem = data.ignoreWeatherSystem;
				catIt->second.ignoreWeatherSystemInterior = data.ignoreWeatherSystemInterior;
			}
		}

		// Sync lastSaved state for all categories
		for (auto& [catName, catData] : categories) {
			catData.lastSavedIgnoreWeatherSystem = catData.ignoreWeatherSystem;
			catData.lastSavedIgnoreWeatherSystemInterior = catData.ignoreWeatherSystemInterior;
		}

		lastSavedWeatherData = weatherData;
	}
}

void SettingManager::SaveToFile(const std::string& filePath)
{
	std::vector<std::tuple<std::string, std::string, Setting>> settingsToWrite;
	std::vector<std::tuple<std::string, bool, bool>> weatherSupportFlags;

	{
		std::unique_lock lock(mutex);
		for (auto& setting : allSettings) {
			if (!(setting.currentValue == setting.lastSavedValue)) {
				settingsToWrite.emplace_back(setting.category, setting.key, setting);
				setting.lastSavedValue = setting.currentValue;
			}
		}

		for (const auto& categoryName : categoryOrder) {
			auto& categoryData = categories.at(categoryName);
			bool hasWeatherSupport = false;
			for (const auto& [key, settingID] : categoryData.settings) {
				if (allSettings[settingID].hasWeatherSupport) {
					hasWeatherSupport = true;
					break;
				}
			}

			if (hasWeatherSupport) {
				if (categoryData.ignoreWeatherSystem != categoryData.lastSavedIgnoreWeatherSystem ||
					categoryData.ignoreWeatherSystemInterior != categoryData.lastSavedIgnoreWeatherSystemInterior) {
					weatherSupportFlags.emplace_back(categoryName, categoryData.ignoreWeatherSystem, categoryData.ignoreWeatherSystemInterior);
					categoryData.lastSavedIgnoreWeatherSystem = categoryData.ignoreWeatherSystem;
					categoryData.lastSavedIgnoreWeatherSystemInterior = categoryData.ignoreWeatherSystemInterior;
				}
			}
		}
	}

	if (settingsToWrite.empty() && weatherSupportFlags.empty()) {
		return;
	}

	// Perform IO outside of lock
	for (const auto& [category, key, setting] : settingsToWrite) {
		SaveSettingToFile(filePath, category, key, setting);
	}

	for (const auto& [category, ignoreOut, ignoreIn] : weatherSupportFlags) {
		WritePrivateProfileStringA(category.c_str(), "IgnoreWeatherSystem",
			ignoreOut ? "true" : "false", filePath.c_str());
		WritePrivateProfileStringA(category.c_str(), "IgnoreWeatherSystemInterior",
			ignoreIn ? "true" : "false", filePath.c_str());
	}

	// Flush cache
	WritePrivateProfileStringA(NULL, NULL, NULL, filePath.c_str());
}

SettingValue SettingManager::InterpolateValues(const SettingValue& a, const SettingValue& b, float t) const
{
	if (a.index() != b.index()) {
		return t > 0.5f ? b : a;
	}

	return std::visit([&](auto&& valA) -> SettingValue {
		using T = std::decay_t<decltype(valA)>;
		const T& valB = std::get<T>(b);

		if constexpr (std::is_same_v<T, bool>) {
			return t > 0.5f ? valB : valA;
		} else if constexpr (std::is_same_v<T, float>) {
			return valA + t * (valB - valA);
		} else if constexpr (std::is_same_v<T, TimeOfDayValue>) {
			TimeOfDayValue result;
			for (int i = 0; i < 8; ++i) {
				result.values[i] = valA.values[i] + t * (valB.values[i] - valA.values[i]);
			}
			return result;
		} else if constexpr (std::is_same_v<T, ColorTimeOfDayValue>) {
			ColorTimeOfDayValue result;
			for (int i = 0; i < 8; ++i) {
				result.values[i] = valA.values[i] + t * (valB.values[i] - valA.values[i]);
			}
			return result;
		}
		return valB;
	},
		a);
}

float SettingManager::ComputeTimeOfDayInterpolation(const TimeOfDayValue& value) const
{
	if (interiorFactor > 0.5f) {
		float dayNightFactor = (timeOfDay1[2] + timeOfDay1[1] + timeOfDay1[0] * 0.5f + timeOfDay1[3] * 0.5f);
		return value.values[TimeOfDayValue::InteriorNight] + dayNightFactor *
		                                                         (value.values[TimeOfDayValue::InteriorDay] - value.values[TimeOfDayValue::InteriorNight]);
	}

	return timeOfDay1[0] * value.values[TimeOfDayValue::Dawn] +
	       timeOfDay1[1] * value.values[TimeOfDayValue::Sunrise] +
	       timeOfDay1[2] * value.values[TimeOfDayValue::Day] +
	       timeOfDay1[3] * value.values[TimeOfDayValue::Sunset] +
	       timeOfDay2[0] * value.values[TimeOfDayValue::Dusk] +
	       timeOfDay2[1] * value.values[TimeOfDayValue::Night];
}

float3 SettingManager::ComputeColorTimeOfDayInterpolation(const ColorTimeOfDayValue& value) const
{
	if (interiorFactor > 0.5f) {
		float dayNightFactor = (timeOfDay1[2] + timeOfDay1[1] + timeOfDay1[0] * 0.5f + timeOfDay1[3] * 0.5f);
		float3 interiorNight = value.values[ColorTimeOfDayValue::InteriorNight];
		float3 interiorDay = value.values[ColorTimeOfDayValue::InteriorDay];
		return interiorNight + dayNightFactor * (interiorDay - interiorNight);
	}

	return timeOfDay1[0] * value.values[ColorTimeOfDayValue::Dawn] +
	       timeOfDay1[1] * value.values[ColorTimeOfDayValue::Sunrise] +
	       timeOfDay1[2] * value.values[ColorTimeOfDayValue::Day] +
	       timeOfDay1[3] * value.values[ColorTimeOfDayValue::Sunset] +
	       timeOfDay2[0] * value.values[ColorTimeOfDayValue::Dusk] +
	       timeOfDay2[1] * value.values[ColorTimeOfDayValue::Night];
}

void SettingManager::LoadSettingFromFile(const std::string& filePath, const std::string& section, const std::string& key, Setting& setting)
{
	switch (setting.type) {
	case SettingType::Bool:
		{
			bool defaultVal = std::get<bool>(setting.defaultValue);
			char buffer[256];
			GetPrivateProfileStringA(section.c_str(), key.c_str(), defaultVal ? "true" : "false", buffer, sizeof(buffer), filePath.c_str());
			bool parsed;
			if (TryParseBool(buffer, parsed)) {
				setting.currentValue = parsed;
			} else {
				setting.currentValue = defaultVal;
			}
			break;
		}
	case SettingType::Float:
		{
			float defaultVal = std::get<float>(setting.defaultValue);
			char buffer[256];
			GetPrivateProfileStringA(section.c_str(), key.c_str(), std::to_string(defaultVal).c_str(), buffer, sizeof(buffer), filePath.c_str());
			float parsed;
			if (TryParseFloat(buffer, parsed)) {
				setting.currentValue = std::clamp(parsed, setting.minValue, setting.maxValue);
			} else {
				setting.currentValue = defaultVal;
			}
			break;
		}
	case SettingType::TimeOfDay:
		{
			TimeOfDayValue timeOfDayValue = std::get<TimeOfDayValue>(setting.defaultValue);

			for (int i = 0; i < 8; ++i) {
				std::string fullKey = key + timeOfDayNames[i];
				char buffer[256];
				std::string defaultStr = std::to_string(timeOfDayValue.values[i]);
				GetPrivateProfileStringA(section.c_str(), fullKey.c_str(), defaultStr.c_str(), buffer, sizeof(buffer), filePath.c_str());
				float parsed;
				if (TryParseFloat(buffer, parsed)) {
					timeOfDayValue.values[i] = std::clamp(parsed, setting.minValue, setting.maxValue);
				}
			}

			setting.currentValue = timeOfDayValue;
			break;
		}
	case SettingType::ColorTimeOfDay:
		{
			ColorTimeOfDayValue colorTimeOfDayValue = std::get<ColorTimeOfDayValue>(setting.defaultValue);

			for (int i = 0; i < 8; ++i) {
				std::string fullKey = key + timeOfDayNames[i];
				char buffer[256];
				float3 defaultColor = colorTimeOfDayValue.values[i];
				std::string defaultStr = std::to_string(defaultColor.x) + ", " +
				                         std::to_string(defaultColor.y) + ", " +
				                         std::to_string(defaultColor.z);

				GetPrivateProfileStringA(section.c_str(), fullKey.c_str(), defaultStr.c_str(), buffer, sizeof(buffer), filePath.c_str());
				std::string valueStr = buffer;

				// Parse comma-separated float3 values
				std::stringstream ss(valueStr);
				std::string item;
				std::vector<float> components;
				bool success = true;

				while (std::getline(ss, item, ',')) {
					float parsed;
					if (TryParseFloat(item, parsed)) {
						components.push_back(std::clamp(parsed, setting.minValue, setting.maxValue));
					} else {
						success = false;
						break;
					}
				}

				// Ensure we have exactly 3 components and parsing was successful
				if (success && components.size() == 3) {
					colorTimeOfDayValue.values[i].x = components[0];
					colorTimeOfDayValue.values[i].y = components[1];
					colorTimeOfDayValue.values[i].z = components[2];
				}
			}

			setting.currentValue = colorTimeOfDayValue;
			break;
		}
	}
}

void SettingManager::SaveSettingToFile(const std::string& filePath, const std::string& section, const std::string& key, const Setting& setting)
{
	auto formatFloat = [](float value) -> std::string {
		char temp[32];
		sprintf_s(temp, "%.3f", value);
		std::string result = temp;

		// Remove trailing zeros
		while (result.length() > 1 && result.back() == '0') {
			result.pop_back();
		}

		// Ensure at least one decimal place (add .0 if needed)
		if (result.back() == '.') {
			result += '0';
		}

		return result;
	};

	switch (setting.type) {
	case SettingType::Bool:
		{
			bool value = std::get<bool>(setting.currentValue);
			WritePrivateProfileStringA(section.c_str(), key.c_str(), value ? "true" : "false", filePath.c_str());
			break;
		}
	case SettingType::Float:
		{
			float value = std::get<float>(setting.currentValue);
			std::string formatted = formatFloat(value);
			WritePrivateProfileStringA(section.c_str(), key.c_str(), formatted.c_str(), filePath.c_str());
			break;
		}
	case SettingType::TimeOfDay:
		{
			const TimeOfDayValue& timeOfDayValue = std::get<TimeOfDayValue>(setting.currentValue);

			for (int i = 0; i < 8; ++i) {
				std::string fullKey = key + timeOfDayNames[i];
				std::string formatted = formatFloat(timeOfDayValue.values[i]);
				WritePrivateProfileStringA(section.c_str(), fullKey.c_str(), formatted.c_str(), filePath.c_str());
			}
			break;
		}
	case SettingType::ColorTimeOfDay:
		{
			const ColorTimeOfDayValue& colorTimeOfDayValue = std::get<ColorTimeOfDayValue>(setting.currentValue);

			for (int i = 0; i < 8; ++i) {
				std::string fullKey = key + timeOfDayNames[i];
				const auto& color = colorTimeOfDayValue.values[i];
				std::string formatted = formatFloat(color.x) + ", " + formatFloat(color.y) + ", " + formatFloat(color.z);
				WritePrivateProfileStringA(section.c_str(), fullKey.c_str(), formatted.c_str(), filePath.c_str());
			}
			break;
		}
	}
}

void SettingManager::LoadWeatherIgnoreSettings(const std::string& filePath)
{
	// Internal helper, called from methods already holding a unique lock
	for (auto& [category, categoryData] : categories) {
		bool hasWeatherSupport = false;
		for (const auto& [key, settingID] : categoryData.settings) {
			if (allSettings[settingID].hasWeatherSupport) {
				hasWeatherSupport = true;
				break;
			}
		}

		if (hasWeatherSupport) {
			char buffer1[256];
			GetPrivateProfileStringA(category.c_str(), "IgnoreWeatherSystem", "false", buffer1, sizeof(buffer1), filePath.c_str());
			bool parsed;
			if (TryParseBool(buffer1, parsed)) {
				categoryData.ignoreWeatherSystem = parsed;
			}

			char buffer2[256];
			GetPrivateProfileStringA(category.c_str(), "IgnoreWeatherSystemInterior", "true", buffer2, sizeof(buffer2), filePath.c_str());
			if (TryParseBool(buffer2, parsed)) {
				categoryData.ignoreWeatherSystemInterior = parsed;
			}
		}

		categoryData.lastSavedIgnoreWeatherSystem = categoryData.ignoreWeatherSystem;
		categoryData.lastSavedIgnoreWeatherSystemInterior = categoryData.ignoreWeatherSystemInterior;
	}
}

bool SettingManager::GetIgnoreWeatherSystem(const std::string& category) const
{
	std::shared_lock lock(mutex);
	auto categoryIt = categories.find(category);
	return categoryIt != categories.end() ? categoryIt->second.ignoreWeatherSystem : false;
}

bool SettingManager::GetIgnoreWeatherSystemInterior(const std::string& category) const
{
	std::shared_lock lock(mutex);
	auto categoryIt = categories.find(category);
	return categoryIt != categories.end() ? categoryIt->second.ignoreWeatherSystemInterior : true;
}

void SettingManager::SetIgnoreWeatherSystem(const std::string& category, bool ignore)
{
	std::unique_lock lock(mutex);
	categories[category].ignoreWeatherSystem = ignore;
}

void SettingManager::SetIgnoreWeatherSystemInterior(const std::string& category, bool ignore)
{
	std::unique_lock lock(mutex);
	categories[category].ignoreWeatherSystemInterior = ignore;
}

void SettingManager::Load()
{
	LoadFromFile("enbseries.ini");
	ReloadAllWeatherSettings();
}

void SettingManager::Save()
{
	SaveToFile("enbseries.ini");
	SaveAllWeatherSettings();
}

// Explicit template instantiations
template bool SettingManager::GetValue<bool>(const std::string& key, const std::string& category, bool rawValue);
template float SettingManager::GetValue<float>(const std::string& key, const std::string& category, bool rawValue);
template TimeOfDayValue SettingManager::GetValue<TimeOfDayValue>(const std::string& key, const std::string& category, bool rawValue);
template ColorTimeOfDayValue SettingManager::GetValue<ColorTimeOfDayValue>(const std::string& key, const std::string& category, bool rawValue);

template void SettingManager::SetValue<bool>(const std::string& key, const std::string& category, const bool& value);
template void SettingManager::SetValue<float>(const std::string& key, const std::string& category, const float& value);
template void SettingManager::SetValue<TimeOfDayValue>(const std::string& key, const std::string& category, const TimeOfDayValue& value);
template void SettingManager::SetValue<ColorTimeOfDayValue>(const std::string& key, const std::string& category, const ColorTimeOfDayValue& value);

template bool SettingManager::GetValue<bool>(uint32_t id, bool rawValue);
template float SettingManager::GetValue<float>(uint32_t id, bool rawValue);
template TimeOfDayValue SettingManager::GetValue<TimeOfDayValue>(uint32_t id, bool rawValue);
template ColorTimeOfDayValue SettingManager::GetValue<ColorTimeOfDayValue>(uint32_t id, bool rawValue);

template void SettingManager::SetValue<bool>(uint32_t id, const bool& value);
template void SettingManager::SetValue<float>(uint32_t id, const float& value);
template void SettingManager::SetValue<TimeOfDayValue>(uint32_t id, const TimeOfDayValue& value);
template void SettingManager::SetValue<ColorTimeOfDayValue>(uint32_t id, const ColorTimeOfDayValue& value);

template bool SettingManager::GetValueInternal<bool>(uint32_t id, bool rawValue) const;
template float SettingManager::GetValueInternal<float>(uint32_t id, bool rawValue) const;
template TimeOfDayValue SettingManager::GetValueInternal<TimeOfDayValue>(uint32_t id, bool rawValue) const;
template ColorTimeOfDayValue SettingManager::GetValueInternal<ColorTimeOfDayValue>(uint32_t id, bool rawValue) const;

template void SettingManager::SetValueInternal<bool>(uint32_t id, const bool& value);
template void SettingManager::SetValueInternal<float>(uint32_t id, const float& value);
template void SettingManager::SetValueInternal<TimeOfDayValue>(uint32_t id, const TimeOfDayValue& value);
template void SettingManager::SetValueInternal<ColorTimeOfDayValue>(uint32_t id, const ColorTimeOfDayValue& value);