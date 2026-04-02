#include "SettingManager.h"

#include "WeatherManager.h"
#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <string>

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

	categories[category].settings[key] = setting;
}

void SettingManager::RegisterFloatSetting(const std::string& key, const std::string& category,
	float defaultValue, float minValue, float maxValue, bool hasWeatherSupport)
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

	categories[category].settings[key] = setting;
}

void SettingManager::RegisterTimeOfDaySetting(const std::string& key, const std::string& category,
	float defaultValue, bool hasWeatherSupport)
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

	categories[category].settings[key] = setting;
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

	categories[category].settings[key] = setting;
}

template <typename T>
T SettingManager::GetValue(const std::string& key, const std::string& category, bool rawValue)
{
	std::shared_lock lock(mutex);
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
	std::unique_lock lock(mutex);
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

		// Only write to weather data if the weather already has loaded settings
		auto weatherIt = weatherData.find(targetWeatherID);
		if (weatherIt != weatherData.end()) {
			weatherIt->second[settingKey] = value;
		} else {
			setting.currentValue = value;
		}
		return;
	}

	setting.currentValue = value;
}

float SettingManager::GetInterpolatedTimeOfDayValue(const std::string& key, const std::string& category)
{
	TimeOfDayValue timeOfDayValue = GetValue<TimeOfDayValue>(key, category);
	return ComputeTimeOfDayInterpolation(timeOfDayValue);
}

float3 SettingManager::GetInterpolatedColorTimeOfDayValue(const std::string& key, const std::string& category)
{
	ColorTimeOfDayValue colorTimeOfDayValue = GetValue<ColorTimeOfDayValue>(key, category);
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
	auto categoryIt = categories.find(category);
	if (categoryIt == categories.end())
		return nullptr;
	auto settingIt = categoryIt->second.settings.find(key);
	return (settingIt != categoryIt->second.settings.end()) ? &settingIt->second : nullptr;
}

std::vector<std::string> SettingManager::GetSettingsByCategory(const std::string& category) const
{
	std::shared_lock lock(mutex);
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
	std::shared_lock lock(mutex);
	std::vector<std::string> result;
	for (const auto& [category, _] : categories) {
		result.push_back(category);
	}
	std::sort(result.begin(), result.end());
	return result;
}

bool SettingManager::CategoryHasWeatherSupport(const std::string& category) const
{
	std::shared_lock lock(mutex);
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
	std::unique_lock lock(mutex);
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

	uint32_t weatherID;
	if (!TryParseWeatherID(weatherKey, weatherID)) {
		logger::error("[SettingManager] Invalid weather key: {}", weatherKey);
		return;
	}

	std::unique_lock lock(mutex);
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
	uint32_t weatherID;
	if (!TryParseWeatherID(weatherKey, weatherID)) {
		logger::error("[SettingManager] Invalid weather key: {}", weatherKey);
		return;
	}

	std::shared_lock lock(mutex);
	auto weatherIt = weatherData.find(weatherID);
	if (weatherIt == weatherData.end()) {
		return;
	}

	for (const auto& [category, categoryData] : categories) {
		for (const auto& [key, setting] : categoryData.settings) {
			if (setting.hasWeatherSupport) {
				std::string settingKey = category + "::" + key;
				auto valueIt = weatherIt->second.find(settingKey);
				if (valueIt != weatherIt->second.end()) {
					Setting tempSetting = setting;
					tempSetting.currentValue = valueIt->second;
					SaveSettingToFile(filePath, category, key, tempSetting);
				}
			}
		}
	}
}

void SettingManager::SaveAllWeatherSettings()
{
	auto& weatherManager = WeatherManager::GetSingleton();
	const auto& weatherFiles = weatherManager.GetWeatherFiles();

	for (const auto& [weatherKey, filePath] : weatherFiles) {
		SaveWeatherSettings(weatherKey, filePath);
	}
}

void SettingManager::ReloadAllWeatherSettings()
{
	{
		std::unique_lock lock(mutex);
		weatherData.clear();
	}
	auto& weatherManager = WeatherManager::GetSingleton();
	weatherManager.Initialize();
}

void SettingManager::SetTimeOfDayData(const float newTimeOfDay1[4], const float newTimeOfDay2[4], float newInteriorFactor)
{
	std::unique_lock lock(mutex);
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

	std::unique_lock lock(mutex);
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
	std::shared_lock lock(mutex);
	for (const auto& [category, categoryData] : categories) {
		for (const auto& [key, setting] : categoryData.settings) {
			if (!setting.hasWeatherSupport) {
				SaveSettingToFile(filePath, category, key, setting);
			}
		}

		bool hasWeatherSupport = false;
		for (const auto& [key, setting] : categoryData.settings) {
			if (setting.hasWeatherSupport) {
				hasWeatherSupport = true;
				break;
			}
		}

		if (hasWeatherSupport) {
			WritePrivateProfileStringA(category.c_str(), "IgnoreWeatherSystem",
				categoryData.ignoreWeatherSystem ? "true" : "false", filePath.c_str());
			WritePrivateProfileStringA(category.c_str(), "IgnoreWeatherSystemInterior",
				categoryData.ignoreWeatherSystemInterior ? "true" : "false", filePath.c_str());
		}
	}
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
	case 3:  // ColorTimeOfDayValue
		{
			const auto& valA = std::get<ColorTimeOfDayValue>(a);
			const auto& valB = std::get<ColorTimeOfDayValue>(b);
			ColorTimeOfDayValue result;
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

float3 SettingManager::ComputeColorTimeOfDayInterpolation(const ColorTimeOfDayValue& value)
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
			const std::vector<std::string> timeOfDayNames = { "Dawn", "Sunrise", "Day", "Sunset", "Dusk", "Night", "InteriorDay", "InteriorNight" };

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
			const std::vector<std::string> timeOfDayNames = { "Dawn", "Sunrise", "Day", "Sunset", "Dusk", "Night", "InteriorDay", "InteriorNight" };

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
			const std::vector<std::string> timeOfDayNames = { "Dawn", "Sunrise", "Day", "Sunset", "Dusk", "Night", "InteriorDay", "InteriorNight" };

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
			const std::vector<std::string> timeOfDayNames = { "Dawn", "Sunrise", "Day", "Sunset", "Dusk", "Night", "InteriorDay", "InteriorNight" };

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
		for (const auto& [key, setting] : categoryData.settings) {
			if (setting.hasWeatherSupport) {
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