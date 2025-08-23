#include "SettingManager.h"

#include "IniFileCache.h"
#include "WeatherManager.h"

SettingManager& SettingManager::GetSingleton()
{
	static SettingManager instance;
	return instance;
}

void SettingManager::RegisterBoolSetting(const std::string& key, const std::string& category,
	bool defaultValue, bool hasWeatherSupport)
{
	Setting setting;
	setting.key = key;
	setting.category = category;
	setting.type = SettingType::Bool;
	setting.hasWeatherSupport = hasWeatherSupport;
	setting.defaultValue = defaultValue;
	setting.currentValue = defaultValue;

	categories[category].settings[key] = setting;
}

void SettingManager::RegisterFloatSetting(const std::string& key, const std::string& category,
	float defaultValue, float minValue, float maxValue, bool hasWeatherSupport)
{
	Setting setting;
	setting.key = key;
	setting.category = category;
	setting.type = SettingType::Float;
	setting.hasWeatherSupport = hasWeatherSupport;
	setting.defaultValue = defaultValue;
	setting.currentValue = defaultValue;
	setting.minValue = minValue;
	setting.maxValue = maxValue;

	categories[category].settings[key] = setting;
}

void SettingManager::RegisterTimeOfDaySetting(const std::string& key, const std::string& category,
	float defaultValue, bool hasWeatherSupport)
{
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

	categories[category].settings[key] = setting;
}

template <typename T>
T SettingManager::GetValue(const std::string& key, const std::string& category, bool rawValue)
{
	auto categoryIt = categories.find(category);
	if (categoryIt == categories.end()) {
		logger::error("[SettingManager] Category '{}' not found", category);
		return T{};
	}

	auto settingIt = categoryIt->second.settings.find(key);
	if (settingIt == categoryIt->second.settings.end()) {
		logger::error("[SettingManager] Setting '{}::{}' not found", category, key);
		return T{};
	}

	const auto& setting = settingIt->second;
	const auto& categorySettings = categoryIt->second;

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
			std::string settingKey = category + "::" + key;
			bool foundWeatherData = false;

			if (currentIt != weatherData.end()) {
				auto valueIt = currentIt->second.find(settingKey);
				if (valueIt != currentIt->second.end()) {
					currentValue = valueIt->second;
					foundWeatherData = true;
				}
			}

			if (lastIt != weatherData.end()) {
				auto valueIt = lastIt->second.find(settingKey);
				if (valueIt != lastIt->second.end()) {
					lastValue = valueIt->second;
					foundWeatherData = true;
				}
			}

			if (foundWeatherData) {
				if (rawValue) {
					return std::get<T>(weatherBlendFactor > 0.5f ? currentValue : lastValue);
				}

				SettingValue blendedValue = InterpolateValues(lastValue, currentValue, weatherBlendFactor);
				return std::get<T>(blendedValue);
			}
		}
	}

	return std::get<T>(setting.currentValue);
}

template <typename T>
void SettingManager::SetValue(const std::string& key, const std::string& category, const T& value)
{
	auto categoryIt = categories.find(category);
	if (categoryIt == categories.end()) {
		logger::error("[SettingManager] Category '{}' not found", category);
		return;
	}

	auto settingIt = categoryIt->second.settings.find(key);
	if (settingIt == categoryIt->second.settings.end()) {
		logger::error("[SettingManager] Setting '{}::{}' not found", category, key);
		return;
	}

	auto& setting = settingIt->second;
	const auto& categorySettings = categoryIt->second;

	if (setting.hasWeatherSupport) {
		bool shouldIgnoreWeather = (interiorFactor > 0.5f) ?
		                               categorySettings.ignoreWeatherSystemInterior :
		                               categorySettings.ignoreWeatherSystem;

		if (shouldIgnoreWeather) {
			setting.currentValue = value;
			return;
		}

		uint32_t targetWeatherID = (weatherBlendFactor > 0.5f) ? currentWeatherID : lastWeatherID;
		std::string settingKey = category + "::" + key;
		weatherData[targetWeatherID][settingKey] = value;

		return;
	}

	setting.currentValue = value;
}

float SettingManager::GetInterpolatedTimeOfDayValue(const std::string& key, const std::string& category)
{
	TimeOfDayValue timeOfDayValue = GetValue<TimeOfDayValue>(key, category);
	return ComputeTimeOfDayInterpolation(timeOfDayValue);
}

bool SettingManager::HasSetting(const std::string& key, const std::string& category) const
{
	auto categoryIt = categories.find(category);
	return categoryIt != categories.end() && categoryIt->second.settings.find(key) != categoryIt->second.settings.end();
}

const Setting* SettingManager::GetSettingInfo(const std::string& key, const std::string& category) const
{
	auto categoryIt = categories.find(category);
	if (categoryIt == categories.end())
		return nullptr;
	auto settingIt = categoryIt->second.settings.find(key);
	return (settingIt != categoryIt->second.settings.end()) ? &settingIt->second : nullptr;
}

std::vector<std::string> SettingManager::GetSettingsByCategory(const std::string& category) const
{
	std::vector<std::string> result;
	auto categoryIt = categories.find(category);
	if (categoryIt != categories.end()) {
		for (const auto& [key, setting] : categoryIt->second.settings) {
			result.push_back(key);
		}
		std::sort(result.begin(), result.end());
	}
	return result;
}

std::vector<std::string> SettingManager::GetAllCategories() const
{
	std::vector<std::string> result;
	for (const auto& [category, _] : categories) {
		result.push_back(category);
	}
	std::sort(result.begin(), result.end());
	return result;
}

bool SettingManager::CategoryHasWeatherSupport(const std::string& category) const
{
	auto categoryIt = categories.find(category);
	if (categoryIt == categories.end())
		return false;
	for (const auto& [key, setting] : categoryIt->second.settings) {
		if (setting.hasWeatherSupport) {
			return true;
		}
	}
	return false;
}

void SettingManager::SetWeatherBlendFactors(uint32_t newCurrentWeatherID, uint32_t newLastWeatherID, float blendFactor)
{
	this->currentWeatherID = newCurrentWeatherID;
	this->lastWeatherID = newLastWeatherID;
	this->weatherBlendFactor = blendFactor;
}

void SettingManager::LoadWeatherSettings(const std::string& weatherKey, const std::string& filePath)
{
	if (!std::filesystem::exists(filePath)) {
		logger::warn("[SettingManager] Weather file not found: {}", filePath);
		return;
	}

	std::string weatherIDStr = weatherKey.substr(8);  // Remove "weather_" prefix
	uint32_t weatherID = std::stoul(weatherIDStr);

	for (const auto& [category, categoryData] : categories) {
		for (const auto& [key, setting] : categoryData.settings) {
			if (setting.hasWeatherSupport) {
				Setting tempSetting = setting;
				LoadSettingFromFile(filePath, category, key, tempSetting);
				std::string settingKey = category + "::" + key;
				weatherData[weatherID][settingKey] = tempSetting.currentValue;
			}
		}
	}
}

void SettingManager::SaveWeatherSettings(const std::string& weatherKey, const std::string& filePath)
{
	std::string weatherIDStr = weatherKey.substr(8);  // Remove "weather_" prefix
	uint32_t weatherID = std::stoul(weatherIDStr);

	auto weatherIt = weatherData.find(weatherID);
	if (weatherIt == weatherData.end()) {
		logger::warn("[SettingManager] No weather settings found for key: {}", weatherKey);
		return;
	}

	std::filesystem::create_directories(std::filesystem::path(filePath).parent_path());

	for (const auto& [settingKey, value] : weatherIt->second) {
		size_t pos = settingKey.find("::");
		if (pos == std::string::npos)
			continue;

		std::string category = settingKey.substr(0, pos);
		std::string key = settingKey.substr(pos + 2);

		auto categoryIt = categories.find(category);
		if (categoryIt == categories.end())
			continue;

		auto keyIt = categoryIt->second.settings.find(key);
		if (keyIt == categoryIt->second.settings.end())
			continue;

		Setting tempSetting = keyIt->second;
		tempSetting.currentValue = value;
		SaveSettingToFile(filePath, category, key, tempSetting);
	}

	logger::debug("[SettingManager] Saved weather settings to: {}", filePath);
	IniAPI::FlushAll();
}

void SettingManager::SaveAllWeatherSettings()
{
	auto& weatherManager = WeatherManager::GetSingleton();
	const auto& weatherEntries = weatherManager.GetWeatherEntries();

	if (weatherEntries.empty()) {
		logger::warn("[SettingManager] No weather entries found, skipping weather file save");
		return;
	}

	int savedCount = 0;
	for (const auto& [sectionName, entry] : weatherEntries) {
		std::string weatherFilePath = "enbseries/" + entry.fileName;
		std::filesystem::create_directories(std::filesystem::path(weatherFilePath).parent_path());

		if (!entry.weatherIDs.empty()) {
			uint32_t weatherID = entry.weatherIDs[0];
			auto weatherIt = weatherData.find(weatherID);

			if (weatherIt != weatherData.end()) {
				for (const auto& [settingKey, value] : weatherIt->second) {
					size_t pos = settingKey.find("::");
					if (pos == std::string::npos)
						continue;

					std::string category = settingKey.substr(0, pos);
					std::string key = settingKey.substr(pos + 2);

					auto categoryIt = categories.find(category);
					if (categoryIt == categories.end())
						continue;

					auto keyIt = categoryIt->second.settings.find(key);
					if (keyIt == categoryIt->second.settings.end())
						continue;

					Setting tempSetting = keyIt->second;
					tempSetting.currentValue = value;
					SaveSettingToFile(weatherFilePath, category, key, tempSetting);
				}
			} else {
				for (const auto& [category, categoryData] : categories) {
					for (const auto& [key, setting] : categoryData.settings) {
						if (setting.hasWeatherSupport) {
							SaveSettingToFile(weatherFilePath, category, key, setting);
						}
					}
				}
			}
		}

		savedCount++;
	}

	IniAPI::FlushAll();
	logger::info("[SettingManager] Saved settings to {} weather files", savedCount);
}

void SettingManager::ReloadAllWeatherSettings()
{
	weatherData.clear();
	auto& weatherManager = WeatherManager::GetSingleton();
	weatherManager.Initialize();
}

void SettingManager::SetTimeOfDayData(const float newTimeOfDay1[4], const float newTimeOfDay2[4], float newInteriorFactor)
{
	memcpy(this->timeOfDay1, newTimeOfDay1, sizeof(this->timeOfDay1));
	memcpy(this->timeOfDay2, newTimeOfDay2, sizeof(this->timeOfDay2));
	this->interiorFactor = newInteriorFactor;
}

void SettingManager::LoadFromFile(const std::string& filePath)
{
	std::filesystem::path absPath = std::filesystem::absolute(filePath);

	if (!std::filesystem::exists(absPath)) {
		logger::warn("[SettingManager] Settings file not found: {}, using defaults", absPath.string());
		return;
	}

	for (auto& [category, categoryData] : categories) {
		for (auto& [key, setting] : categoryData.settings) {
			if (!setting.hasWeatherSupport) {
				LoadSettingFromFile(absPath.string(), category, key, setting);
			}
		}
	}

	LoadWeatherIgnoreSettings(absPath.string());
}

void SettingManager::SaveToFile(const std::string& filePath)
{
	for (const auto& [category, categoryData] : categories) {
		for (const auto& [key, setting] : categoryData.settings) {
			if (!setting.hasWeatherSupport) {
				SaveSettingToFile(filePath, category, key, setting);
			}
		}
	}

	SaveWeatherIgnoreSettings(filePath);
	IniAPI::FlushAll();
}

SettingValue SettingManager::InterpolateValues(const SettingValue& a, const SettingValue& b, float t)
{
	if (a.index() != b.index()) {
		return t > 0.5f ? b : a;
	}

	switch (a.index()) {
	case 0:  // bool
		return t > 0.5f ? std::get<bool>(b) : std::get<bool>(a);
	case 1:  // float
		{
			float valA = std::get<float>(a);
			float valB = std::get<float>(b);
			return valA + t * (valB - valA);
		}
	case 2:  // TimeOfDayValue
		{
			const auto& valA = std::get<TimeOfDayValue>(a);
			const auto& valB = std::get<TimeOfDayValue>(b);
			TimeOfDayValue result;
			for (int i = 0; i < 8; ++i) {
				result.values[i] = valA.values[i] + t * (valB.values[i] - valA.values[i]);
			}
			return result;
		}
	}
	return b;
}

float SettingManager::ComputeTimeOfDayInterpolation(const TimeOfDayValue& value)
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

void SettingManager::LoadSettingFromFile(const std::string& filePath, const std::string& section, const std::string& key, Setting& setting)
{
	switch (setting.type) {
	case SettingType::Bool:
		{
			std::string valueStr = IniAPI::GetPrivateProfileString(section, key, "false", filePath);
			std::transform(valueStr.begin(), valueStr.end(), valueStr.begin(), ::tolower);
			setting.currentValue = (valueStr == "true" || valueStr == "1");
			break;
		}
	case SettingType::Float:
		{
			float defaultVal = std::get<float>(setting.defaultValue);
			std::string valueStr = IniAPI::GetPrivateProfileString(section, key, std::to_string(defaultVal), filePath);
			setting.currentValue = static_cast<float>(atof(valueStr.c_str()));
			break;
		}
	case SettingType::TimeOfDay:
		{
			TimeOfDayValue timeOfDayValue = std::get<TimeOfDayValue>(setting.defaultValue);
			const std::vector<std::string> timeOfDayNames = { "Dawn", "Sunrise", "Day", "Sunset", "Dusk", "Night", "InteriorDay", "InteriorNight" };

			for (int i = 0; i < 8; ++i) {
				std::string fullKey = key + timeOfDayNames[i];
				std::string valueStr = IniAPI::GetPrivateProfileString(section, fullKey, "1.0", filePath);
				timeOfDayValue.values[i] = static_cast<float>(atof(valueStr.c_str()));
			}

			setting.currentValue = timeOfDayValue;
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
			IniAPI::WritePrivateProfileString(section, key, value ? "true" : "false", filePath);
			break;
		}
	case SettingType::Float:
		{
			float value = std::get<float>(setting.currentValue);
			std::string formatted = formatFloat(value);
			IniAPI::WritePrivateProfileString(section, key, formatted, filePath);
			break;
		}
	case SettingType::TimeOfDay:
		{
			const TimeOfDayValue& timeOfDayValue = std::get<TimeOfDayValue>(setting.currentValue);
			const std::vector<std::string> timeOfDayNames = { "Dawn", "Sunrise", "Day", "Sunset", "Dusk", "Night", "InteriorDay", "InteriorNight" };

			for (int i = 0; i < 8; ++i) {
				std::string fullKey = key + timeOfDayNames[i];
				std::string formatted = formatFloat(timeOfDayValue.values[i]);
				IniAPI::WritePrivateProfileString(section, fullKey, formatted, filePath);
			}
			break;
		}
	}
}

void SettingManager::SaveWeatherIgnoreSettings(const std::string& filePath)
{
	int count = 0;
	for (const auto& [category, categoryData] : categories) {
		bool hasWeatherSupport = false;
		for (const auto& [key, setting] : categoryData.settings) {
			if (setting.hasWeatherSupport) {
				hasWeatherSupport = true;
				break;
			}
		}

		if (hasWeatherSupport) {
			IniAPI::WritePrivateProfileString(category, "IgnoreWeatherSystem",
				categoryData.ignoreWeatherSystem ? "true" : "false", filePath);
			IniAPI::WritePrivateProfileString(category, "IgnoreWeatherSystemInterior",
				categoryData.ignoreWeatherSystemInterior ? "true" : "false", filePath);
			count++;
		}
	}
}

void SettingManager::LoadWeatherIgnoreSettings(const std::string& filePath)
{
	for (auto& [category, categoryData] : categories) {
		bool hasWeatherSupport = false;
		for (const auto& [key, setting] : categoryData.settings) {
			if (setting.hasWeatherSupport) {
				hasWeatherSupport = true;
				break;
			}
		}

		if (hasWeatherSupport) {
			std::string valueStr = IniAPI::GetPrivateProfileString(category, "IgnoreWeatherSystem", "false", filePath);
			std::transform(valueStr.begin(), valueStr.end(), valueStr.begin(), ::tolower);
			categoryData.ignoreWeatherSystem = (valueStr == "true" || valueStr == "1");

			valueStr = IniAPI::GetPrivateProfileString(category, "IgnoreWeatherSystemInterior", "true", filePath);
			std::transform(valueStr.begin(), valueStr.end(), valueStr.begin(), ::tolower);
			categoryData.ignoreWeatherSystemInterior = (valueStr == "true" || valueStr == "1");
		}
	}
}

bool SettingManager::GetIgnoreWeatherSystem(const std::string& category) const
{
	auto categoryIt = categories.find(category);
	return categoryIt != categories.end() ? categoryIt->second.ignoreWeatherSystem : false;
}

bool SettingManager::GetIgnoreWeatherSystemInterior(const std::string& category) const
{
	auto categoryIt = categories.find(category);
	return categoryIt != categories.end() ? categoryIt->second.ignoreWeatherSystemInterior : true;
}

void SettingManager::SetIgnoreWeatherSystem(const std::string& category, bool ignore)
{
	categories[category].ignoreWeatherSystem = ignore;
}

void SettingManager::SetIgnoreWeatherSystemInterior(const std::string& category, bool ignore)
{
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

template void SettingManager::SetValue<bool>(const std::string& key, const std::string& category, const bool& value);
template void SettingManager::SetValue<float>(const std::string& key, const std::string& category, const float& value);
template void SettingManager::SetValue<TimeOfDayValue>(const std::string& key, const std::string& category, const TimeOfDayValue& value);