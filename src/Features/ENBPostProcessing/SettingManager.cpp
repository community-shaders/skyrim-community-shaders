#include "SettingManager.h"

#include "WeatherManager.h"
#include <Windows.h>

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

void SettingManager::RegisterColorTimeOfDaySetting(const std::string& key, const std::string& category,
	float3 defaultValue, bool hasWeatherSupport)
{
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

float3 SettingManager::GetInterpolatedColorTimeOfDayValue(const std::string& key, const std::string& category)
{
	ColorTimeOfDayValue colorTimeOfDayValue = GetValue<ColorTimeOfDayValue>(key, category);
	return ComputeColorTimeOfDayInterpolation(colorTimeOfDayValue);
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
			char buffer[256];
			GetPrivateProfileStringA(section.c_str(), key.c_str(), "false", buffer, sizeof(buffer), filePath.c_str());
			std::string valueStr = buffer;
			std::transform(valueStr.begin(), valueStr.end(), valueStr.begin(), ::tolower);
			setting.currentValue = (valueStr == "true" || valueStr == "1");
			break;
		}
	case SettingType::Float:
		{
			float defaultVal = std::get<float>(setting.defaultValue);
			char buffer[256];
			GetPrivateProfileStringA(section.c_str(), key.c_str(), std::to_string(defaultVal).c_str(), buffer, sizeof(buffer), filePath.c_str());
			std::string valueStr = buffer;
			setting.currentValue = static_cast<float>(atof(valueStr.c_str()));
			break;
		}
	case SettingType::TimeOfDay:
		{
			TimeOfDayValue timeOfDayValue = std::get<TimeOfDayValue>(setting.defaultValue);
			const std::vector<std::string> timeOfDayNames = { "Dawn", "Sunrise", "Day", "Sunset", "Dusk", "Night", "InteriorDay", "InteriorNight" };

			for (int i = 0; i < 8; ++i) {
				std::string fullKey = key + timeOfDayNames[i];
				char buffer[256];
				GetPrivateProfileStringA(section.c_str(), fullKey.c_str(), "0.0", buffer, sizeof(buffer), filePath.c_str());
				std::string valueStr = buffer;
				timeOfDayValue.values[i] = static_cast<float>(atof(valueStr.c_str()));
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
				GetPrivateProfileStringA(section.c_str(), fullKey.c_str(), "1.0, 1.0, 1.0", buffer, sizeof(buffer), filePath.c_str());
				std::string valueStr = buffer;

				// Parse comma-separated float3 values
				std::stringstream ss(valueStr);
				std::string item;
				std::vector<float> components;

				while (std::getline(ss, item, ',')) {
					// Trim whitespace
					item.erase(0, item.find_first_not_of(" \t"));
					item.erase(item.find_last_not_of(" \t") + 1);
					components.push_back(static_cast<float>(atof(item.c_str())));
				}

				// Ensure we have exactly 3 components
				if (components.size() >= 3) {
					colorTimeOfDayValue.values[i] = { components[0], components[1], components[2] };
				} else {
					// Default fallback
					colorTimeOfDayValue.values[i] = { 1.0f, 1.0f, 1.0f };
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
			std::string valueStr = buffer1;
			std::transform(valueStr.begin(), valueStr.end(), valueStr.begin(), ::tolower);
			categoryData.ignoreWeatherSystem = (valueStr == "true" || valueStr == "1");

			char buffer2[256];
			GetPrivateProfileStringA(category.c_str(), "IgnoreWeatherSystemInterior", "true", buffer2, sizeof(buffer2), filePath.c_str());
			valueStr = buffer2;
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