#include "SettingManager.h"

#include "WeatherManager.h"

SettingManager& SettingManager::GetSingleton()
{
	static SettingManager instance;
	return instance;
}

void SettingManager::RegisterBoolSetting(const std::string& key, const std::string& category,
	bool defaultValue, bool hasWeatherSupport)
{
	auto setting = std::make_unique<SettingInfo>();
	setting->key = key;
	setting->category = category;
	setting->type = SettingType::Bool;
	setting->hasWeatherSupport = hasWeatherSupport;
	setting->defaultValue = defaultValue;
	setting->currentValue = defaultValue;

	std::string compositeKey = MakeCompositeKey(key, category);
	settings[compositeKey] = std::move(setting);
}

void SettingManager::RegisterFloatSetting(const std::string& key, const std::string& category,
	float defaultValue, float minValue, float maxValue, bool hasWeatherSupport)
{
	auto setting = std::make_unique<SettingInfo>();
	setting->key = key;
	setting->category = category;
	setting->type = SettingType::Float;
	setting->hasWeatherSupport = hasWeatherSupport;
	setting->defaultValue = defaultValue;
	setting->currentValue = defaultValue;
	setting->minValue = minValue;
	setting->maxValue = maxValue;

	std::string compositeKey = MakeCompositeKey(key, category);
	settings[compositeKey] = std::move(setting);
}

void SettingManager::RegisterTimeOfDaySetting(const std::string& key, const std::string& category,
	const TimeOfDayValue& defaultValue, bool hasWeatherSupport)
{
	auto setting = std::make_unique<SettingInfo>();
	setting->key = key;
	setting->category = category;
	setting->type = SettingType::TimeOfDay;
	setting->hasWeatherSupport = hasWeatherSupport;
	setting->defaultValue = defaultValue;
	setting->currentValue = defaultValue;

	std::string compositeKey = MakeCompositeKey(key, category);
	settings[compositeKey] = std::move(setting);
}

template <typename T>
T SettingManager::GetValue(const std::string& key, const std::string& category, bool rawValue)
{
	std::string compositeKey = MakeCompositeKey(key, category);
	auto it = settings.find(compositeKey);
	if (it == settings.end()) {
		logger::error("[SettingManager] Setting '{}::{}' not found", category, key);
		return T{};
	}

	const auto& setting = *it->second;

	// If setting has weather support, try to get weather-blended value
	if (setting.hasWeatherSupport) {
		// Check ignore weather system settings
		bool shouldIgnoreWeather = false;

		// Check if we should ignore weather system based on interior state
		auto ignoreWeatherIt = ignoreWeatherSystem.find(setting.category);
		auto ignoreWeatherInteriorIt = ignoreWeatherSystemInterior.find(setting.category);

		if (interiorFactor > 0.5f) {
			// We're in an interior
			if (ignoreWeatherInteriorIt != ignoreWeatherSystemInterior.end() && ignoreWeatherInteriorIt->second) {
				shouldIgnoreWeather = true;
			}
		} else {
			// We're in an exterior
			if (ignoreWeatherIt != ignoreWeatherSystem.end() && ignoreWeatherIt->second) {
				shouldIgnoreWeather = true;
			}
		}

		// If ignore is set, skip weather processing and use enbseries.ini values
		if (shouldIgnoreWeather) {
			return std::get<T>(setting.currentValue);
		}

		auto& weatherManager = WeatherManager::GetSingleton();

		// Check if we have weather settings for current weather
		auto currentWeatherEntry = weatherManager.FindWeatherEntry(currentWeatherID);
		auto lastWeatherEntry = weatherManager.FindWeatherEntry(lastWeatherID);

		if (currentWeatherEntry || lastWeatherEntry) {
			// Get weather values
			SettingValue currentWeatherValue = setting.currentValue;  // fallback
			SettingValue lastWeatherValue = setting.currentValue;     // fallback

			// Look up weather-specific values
			if (currentWeatherEntry) {
				std::ostringstream oss;
				oss << "weather_" << currentWeatherID;
				auto weatherIt = weatherSettings.find(oss.str());
				if (weatherIt != weatherSettings.end()) {
					auto valueIt = weatherIt->second.find(compositeKey);
					if (valueIt != weatherIt->second.end()) {
						currentWeatherValue = valueIt->second;
					}
				}
			}

			if (lastWeatherEntry) {
				std::ostringstream oss;
				oss << "weather_" << lastWeatherID;
				auto weatherIt = weatherSettings.find(oss.str());
				if (weatherIt != weatherSettings.end()) {
					auto valueIt = weatherIt->second.find(compositeKey);
					if (valueIt != weatherIt->second.end()) {
						lastWeatherValue = valueIt->second;
					}
				}
			}

			// Return with no interpolation
			if (rawValue) {
				return std::get<T>(weatherBlendFactor > 0.5f ? currentWeatherValue : lastWeatherValue);
			}

			// Interpolate between weather values
			SettingValue blendedValue = InterpolateWeatherValues(currentWeatherValue, lastWeatherValue, weatherBlendFactor);
			return std::get<T>(blendedValue);
		}
	}

	// Return base setting value
	return std::get<T>(setting.currentValue);
}

template <typename T>
void SettingManager::SetValue(const std::string& key, const std::string& category, const T& value)
{
	std::string compositeKey = MakeCompositeKey(key, category);
	auto it = settings.find(compositeKey);
	if (it == settings.end()) {
		logger::error("[SettingManager] Setting '{}::{}' not found", category, key);
		return;
	}

	const auto& setting = *it->second;

	// If setting has weather support, use the same logic as GetValue to determine what to update
	if (setting.hasWeatherSupport) {
		// Check ignore weather system settings
		bool shouldIgnoreWeather = false;

		// Check if we should ignore weather system based on interior state
		auto ignoreWeatherIt = ignoreWeatherSystem.find(setting.category);
		auto ignoreWeatherInteriorIt = ignoreWeatherSystemInterior.find(setting.category);

		if (interiorFactor > 0.5f) {
			// We're in an interior
			if (ignoreWeatherInteriorIt != ignoreWeatherSystemInterior.end() && ignoreWeatherInteriorIt->second) {
				shouldIgnoreWeather = true;
			}
		} else {
			// We're in an exterior
			if (ignoreWeatherIt != ignoreWeatherSystem.end() && ignoreWeatherIt->second) {
				shouldIgnoreWeather = true;
			}
		}

		// If ignore is set, update base setting value (same as GetValue logic)
		if (shouldIgnoreWeather) {
			it->second->currentValue = value;
			logger::debug("[SettingManager] Updated base setting [{}]::{} (weather ignored)", category, key);
			return;
		}

		auto& weatherManager = WeatherManager::GetSingleton();

		// Check if we have weather settings for current weather
		auto currentWeatherEntry = weatherManager.FindWeatherEntry(currentWeatherID);
		auto lastWeatherEntry = weatherManager.FindWeatherEntry(lastWeatherID);

		if (currentWeatherEntry || lastWeatherEntry) {
			// Determine which weather to update based on blend factor (same logic as GetValue)
			uint32_t targetWeatherID = (weatherBlendFactor > 0.5f) ? currentWeatherID : lastWeatherID;
			auto targetWeatherEntry = (weatherBlendFactor > 0.5f) ? currentWeatherEntry : lastWeatherEntry;

			if (targetWeatherEntry) {
				std::ostringstream oss;
				oss << "weather_" << targetWeatherID;
				std::string weatherKey = oss.str();

				// Update the weather-specific value
				weatherSettings[weatherKey][compositeKey] = value;

				logger::debug("[SettingManager] Updated weather setting [{}]::{} for weather {} (blend: {:.2f})",
					category, key, targetWeatherID, weatherBlendFactor);
				return;
			}
		}
	}

	// Update the base setting value (fallback or non-weather setting)
	it->second->currentValue = value;
	logger::debug("[SettingManager] Updated base setting [{}]::{}", category, key);
}

float SettingManager::GetInterpolatedTimeOfDayValue(const std::string& key)
{
	// This method is deprecated - try to find the setting by key across all categories
	// This is kept for backwards compatibility but should be avoided
	for (const auto& [compositeKey, setting] : settings) {
		if (setting->key == key && setting->type == SettingType::TimeOfDay) {
			TimeOfDayValue timeOfDayValue = std::get<TimeOfDayValue>(setting->currentValue);
			return ComputeTimeOfDayInterpolation(timeOfDayValue);
		}
	}
	logger::warn("[SettingManager] GetInterpolatedTimeOfDayValue: Setting '{}' not found", key);
	return 0.0f;
}

float SettingManager::GetInterpolatedTimeOfDayValue(const std::string& key, const std::string& category)
{
	TimeOfDayValue timeOfDayValue = GetValue<TimeOfDayValue>(key, category);
	return ComputeTimeOfDayInterpolation(timeOfDayValue);
}

bool SettingManager::HasSetting(const std::string& key, const std::string& category) const
{
	std::string compositeKey = MakeCompositeKey(key, category);
	return settings.find(compositeKey) != settings.end();
}

const SettingInfo* SettingManager::GetSettingInfo(const std::string& key, const std::string& category) const
{
	std::string compositeKey = MakeCompositeKey(key, category);
	auto it = settings.find(compositeKey);
	return (it != settings.end()) ? it->second.get() : nullptr;
}

std::vector<std::string> SettingManager::GetSettingsByCategory(const std::string& category) const
{
	std::vector<std::string> result;
	for (const auto& [compositeKey, setting] : settings) {
		if (setting->category == category) {
			result.push_back(setting->key);
		}
	}
	std::sort(result.begin(), result.end());
	return result;
}

std::vector<std::string> SettingManager::GetAllCategories() const
{
	std::set<std::string> categorySet;
	for (const auto& [key, setting] : settings) {
		categorySet.insert(setting->category);
	}
	return std::vector<std::string>(categorySet.begin(), categorySet.end());
}

bool SettingManager::CategoryHasWeatherSupport(const std::string& category) const
{
	for (const auto& [compositeKey, setting] : settings) {
		if (setting->category == category && setting->hasWeatherSupport) {
			return true;
		}
	}
	return false;
}

std::vector<std::string> SettingManager::GetCategoriesWithWeatherSupport() const
{
	std::set<std::string> categoriesWithWeather;
	for (const auto& [compositeKey, setting] : settings) {
		if (setting->hasWeatherSupport) {
			categoriesWithWeather.insert(setting->category);
		}
	}
	return std::vector<std::string>(categoriesWithWeather.begin(), categoriesWithWeather.end());
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

	auto& weatherSettingMap = weatherSettings[weatherKey];

	// Load all weather-supported settings from the file
	for (const auto& [compositeKey, setting] : settings) {
		if (setting->hasWeatherSupport) {
			LoadSettingFromFile(filePath, setting->category, setting->key, *setting);
			// Store the loaded value in weather settings
			weatherSettingMap[compositeKey] = setting->currentValue;
			// Reset current value to default for next weather file
			setting->currentValue = setting->defaultValue;
		}
	}

	logger::debug("[SettingManager] Loaded weather settings from: {}", filePath);
}

void SettingManager::SaveWeatherSettings(const std::string& weatherKey, const std::string& filePath)
{
	auto weatherIt = weatherSettings.find(weatherKey);
	if (weatherIt == weatherSettings.end()) {
		logger::warn("[SettingManager] No weather settings found for key: {}", weatherKey);
		return;
	}

	const auto& weatherSettingMap = weatherIt->second;

	// Create directory if it doesn't exist
	std::filesystem::path weatherFilePath(filePath);
	std::filesystem::create_directories(weatherFilePath.parent_path());

	// Save all weather settings for this weather
	for (const auto& [compositeKey, value] : weatherSettingMap) {
		// Find the original setting to get metadata
		auto settingIt = settings.find(compositeKey);
		if (settingIt == settings.end()) {
			continue;
		}

		const auto& setting = *settingIt->second;

		// Create a temporary setting with the weather value
		SettingInfo tempSetting = setting;
		tempSetting.currentValue = value;

		// Save to weather file
		SaveSettingToFile(filePath, setting.category, setting.key, tempSetting);
	}

	logger::debug("[SettingManager] Saved weather settings to: {}", filePath);
}

void SettingManager::SaveAllWeatherSettings()
{
	auto& weatherManager = WeatherManager::GetSingleton();
	const auto& weatherEntries = weatherManager.GetWeatherEntries();

	if (weatherEntries.empty()) {
		logger::warn("[SettingManager] No weather entries found, skipping weather file save");
		return;
	}

	// Save current settings to all weather files
	int savedCount = 0;
	for (const auto& [sectionName, entry] : weatherEntries) {
		std::string weatherFilePath = "enbseries/" + entry.fileName;

		// Create directory if it doesn't exist
		std::filesystem::path weatherFilePathObj(weatherFilePath);
		std::filesystem::create_directories(weatherFilePathObj.parent_path());

		// For each weather ID in this file, check if we have weather-specific values
		// Use the first weather ID for this file to look up values
		if (!entry.weatherIDs.empty()) {
			uint32_t weatherID = entry.weatherIDs[0];  // Use first weather ID as representative
			std::ostringstream oss;
			oss << "weather_" << weatherID;
			std::string weatherKey = oss.str();

			auto weatherIt = weatherSettings.find(weatherKey);
			if (weatherIt != weatherSettings.end()) {
				// Save weather-specific values for this file
				const auto& weatherSettingMap = weatherIt->second;
				for (const auto& [compositeKey, weatherValue] : weatherSettingMap) {
					auto settingIt = settings.find(compositeKey);
					if (settingIt != settings.end()) {
						const auto& setting = *settingIt->second;

						// Create temporary setting with weather-specific value
						SettingInfo tempSetting = setting;
						tempSetting.currentValue = weatherValue;

						SaveSettingToFile(weatherFilePath, setting.category, setting.key, tempSetting);
					}
				}
			} else {
				// No weather-specific values found, save current UI values
				for (const auto& [compositeKey, setting] : settings) {
					if (setting->hasWeatherSupport) {
						SaveSettingToFile(weatherFilePath, setting->category, setting->key, *setting);
					}
				}
			}
		} else {
			// No weather IDs, save current UI values as fallback
			for (const auto& [compositeKey, setting] : settings) {
				if (setting->hasWeatherSupport) {
					SaveSettingToFile(weatherFilePath, setting->category, setting->key, *setting);
				}
			}
		}

		savedCount++;
		logger::debug("[SettingManager] Saved settings to weather file: {}", weatherFilePath);
	}

	logger::info("[SettingManager] Saved settings to {} weather files", savedCount);
}

void SettingManager::ReloadAllWeatherSettings()
{
	// Clear existing weather settings
	weatherSettings.clear();

	// Reload through WeatherManager
	auto& weatherManager = WeatherManager::GetSingleton();
	weatherManager.Initialize();  // This will reload _weatherlist.ini and all weather files

	logger::info("[SettingManager] Reloaded all weather settings");
}

void SettingManager::SetTimeOfDayData(const float newTimeOfDay1[4], const float newTimeOfDay2[4], float newInteriorFactor)
{
	memcpy(this->timeOfDay1, newTimeOfDay1, sizeof(this->timeOfDay1));
	memcpy(this->timeOfDay2, newTimeOfDay2, sizeof(this->timeOfDay2));
	this->interiorFactor = newInteriorFactor;
}

void SettingManager::LoadFromFile(const std::string& filePath)
{
	// Convert to absolute path
	std::filesystem::path absPath = std::filesystem::absolute(filePath);

	if (!std::filesystem::exists(absPath)) {
		logger::warn("[SettingManager] Settings file not found: {}, using defaults", absPath.string());
		return;
	}

	for (const auto& [compositeKey, setting] : settings) {
		if (!setting->hasWeatherSupport) {  // Only load non-weather settings from main file
			LoadSettingFromFile(absPath.string(), setting->category, setting->key, *setting);
		}
	}

	// Load weather ignore settings
	LoadWeatherIgnoreSettings(absPath.string());

	logger::info("[SettingManager] Loaded settings from: {}", absPath.string());
}

void SettingManager::SaveToFile(const std::string& filePath)
{
	for (const auto& [compositeKey, setting] : settings) {
		if (!setting->hasWeatherSupport) {  // Only save non-weather settings to main file
			SaveSettingToFile(filePath, setting->category, setting->key, *setting);
		}
	}

	// Save weather ignore settings for categories with weather support
	SaveWeatherIgnoreSettings(filePath);

	logger::info("[SettingManager] Saved settings to: {}", filePath);
}

std::string SettingManager::MakeCompositeKey(const std::string& key, const std::string& category) const
{
	return category + "::" + key;
}

SettingValue SettingManager::InterpolateWeatherValues(const SettingValue& currentValue, const SettingValue& lastValue, float t)
{
	if (currentValue.index() != lastValue.index()) {
		return currentValue;  // Type mismatch, return current
	}

	switch (currentValue.index()) {
	case 0:  // bool
		return t > 0.5f ? std::get<bool>(currentValue) : std::get<bool>(lastValue);
	case 1:  // float
		{
			float a = std::get<float>(lastValue);
			float b = std::get<float>(currentValue);
			return a + t * (b - a);
		}
	case 2:  // TimeOfDayValue
		{
			const auto& a = std::get<TimeOfDayValue>(lastValue);
			const auto& b = std::get<TimeOfDayValue>(currentValue);
			return InterpolateTimeOfDayValues(a, b, t);
		}
	}
	return currentValue;
}

TimeOfDayValue SettingManager::InterpolateTimeOfDayValues(const TimeOfDayValue& a, const TimeOfDayValue& b, float t)
{
	TimeOfDayValue result;
	result.Dawn = a.Dawn + t * (b.Dawn - a.Dawn);
	result.Sunrise = a.Sunrise + t * (b.Sunrise - a.Sunrise);
	result.Day = a.Day + t * (b.Day - a.Day);
	result.Sunset = a.Sunset + t * (b.Sunset - a.Sunset);
	result.Dusk = a.Dusk + t * (b.Dusk - a.Dusk);
	result.Night = a.Night + t * (b.Night - a.Night);
	result.InteriorDay = a.InteriorDay + t * (b.InteriorDay - a.InteriorDay);
	result.InteriorNight = a.InteriorNight + t * (b.InteriorNight - a.InteriorNight);
	return result;
}

float SettingManager::ComputeTimeOfDayInterpolation(const TimeOfDayValue& value)
{
	float result = 0.0f;

	if (interiorFactor > 0.5f) {
		// Interior - interpolate between InteriorDay and InteriorNight based on time
		float dayNightFactor = (timeOfDay1[2] + timeOfDay1[1] + timeOfDay1[0] * 0.5f + timeOfDay1[3] * 0.5f);
		result = value.InteriorNight + dayNightFactor * (value.InteriorDay - value.InteriorNight);
	} else {
		// Exterior - use full time-of-day interpolation
		result += timeOfDay1[0] * value.Dawn;     // Dawn
		result += timeOfDay1[1] * value.Sunrise;  // Sunrise
		result += timeOfDay1[2] * value.Day;      // Day
		result += timeOfDay1[3] * value.Sunset;   // Sunset
		result += timeOfDay2[0] * value.Dusk;     // Dusk
		result += timeOfDay2[1] * value.Night;    // Night
	}

	return result;
}

void SettingManager::LoadSettingFromFile(const std::string& filePath, const std::string& section, const std::string& key, SettingInfo& setting)
{
	char buffer[256];

	switch (setting.type) {
	case SettingType::Bool:
		{
			DWORD result = GetPrivateProfileStringA(section.c_str(), key.c_str(), "false", buffer, sizeof(buffer), filePath.c_str());
			if (result == 0) {
				logger::warn("[SettingManager] Failed to load bool setting [{}]::{} from {}", section, key, filePath);
			}
			std::string valueStr = buffer;
			std::transform(valueStr.begin(), valueStr.end(), valueStr.begin(), ::tolower);
			setting.currentValue = (valueStr == "true" || valueStr == "1");
			break;
		}
	case SettingType::Float:
		{
			float defaultVal = std::get<float>(setting.defaultValue);
			DWORD result = GetPrivateProfileStringA(section.c_str(), key.c_str(), std::to_string(defaultVal).c_str(), buffer, sizeof(buffer), filePath.c_str());
			if (result == 0) {
				logger::warn("[SettingManager] Failed to load float setting [{}]::{} from {}", section, key, filePath);
			}
			setting.currentValue = static_cast<float>(atof(buffer));
			break;
		}
	case SettingType::TimeOfDay:
		{
			TimeOfDayValue timeOfDayValue = std::get<TimeOfDayValue>(setting.defaultValue);
			const std::vector<std::string> timeOfDayNames = { "Dawn", "Sunrise", "Day", "Sunset", "Dusk", "Night", "InteriorDay", "InteriorNight" };

			for (const auto& timeOfDay : timeOfDayNames) {
				std::string fullKey = key + timeOfDay;
				DWORD result = GetPrivateProfileStringA(section.c_str(), fullKey.c_str(), "1.0", buffer, sizeof(buffer), filePath.c_str());
				if (result == 0) {
					logger::warn("[SettingManager] Failed to load TimeOfDay setting [{}]::{} from {}", section, fullKey, filePath);
				}
				timeOfDayValue[timeOfDay] = static_cast<float>(atof(buffer));
			}

			setting.currentValue = timeOfDayValue;
			break;
		}
	}
}

void SettingManager::SaveSettingToFile(const std::string& filePath, const std::string& section, const std::string& key, const SettingInfo& setting)
{
	char buffer[256];

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
			sprintf_s(buffer, "%.3f", value);
			WritePrivateProfileStringA(section.c_str(), key.c_str(), buffer, filePath.c_str());
			break;
		}
	case SettingType::TimeOfDay:
		{
			const TimeOfDayValue& timeOfDayValue = std::get<TimeOfDayValue>(setting.currentValue);
			const std::vector<std::string> timeOfDayNames = { "Dawn", "Sunrise", "Day", "Sunset", "Dusk", "Night", "InteriorDay", "InteriorNight" };

			for (const auto& timeOfDay : timeOfDayNames) {
				std::string fullKey = key + timeOfDay;
				sprintf_s(buffer, "%.3f", timeOfDayValue[timeOfDay]);
				WritePrivateProfileStringA(section.c_str(), fullKey.c_str(), buffer, filePath.c_str());
			}
			break;
		}
	}
}

void SettingManager::SaveWeatherIgnoreSettings(const std::string& filePath)
{
	auto categoriesWithWeather = GetCategoriesWithWeatherSupport();

	for (const auto& category : categoriesWithWeather) {
		// Get current values or use defaults
		bool ignoreWeather = false;
		bool ignoreWeatherInterior = true;

		auto itIgnore = ignoreWeatherSystem.find(category);
		if (itIgnore != ignoreWeatherSystem.end()) {
			ignoreWeather = itIgnore->second;
		}

		auto itIgnoreInterior = ignoreWeatherSystemInterior.find(category);
		if (itIgnoreInterior != ignoreWeatherSystemInterior.end()) {
			ignoreWeatherInterior = itIgnoreInterior->second;
		}

		// Write the ignore settings
		WritePrivateProfileStringA(category.c_str(), "IgnoreWeatherSystem", ignoreWeather ? "true" : "false", filePath.c_str());
		WritePrivateProfileStringA(category.c_str(), "IgnoreWeatherSystemInterior", ignoreWeatherInterior ? "true" : "false", filePath.c_str());
	}

	logger::debug("[SettingManager] Saved weather ignore settings for {} categories", categoriesWithWeather.size());
}

void SettingManager::LoadWeatherIgnoreSettings(const std::string& filePath)
{
	auto categoriesWithWeather = GetCategoriesWithWeatherSupport();
	char buffer[256];

	for (const auto& category : categoriesWithWeather) {
		// Load IgnoreWeatherSystem setting
		DWORD result = GetPrivateProfileStringA(category.c_str(), "IgnoreWeatherSystem", "false", buffer, sizeof(buffer), filePath.c_str());
		if (result > 0) {
			std::string valueStr = buffer;
			std::transform(valueStr.begin(), valueStr.end(), valueStr.begin(), ::tolower);
			bool ignoreWeatherSystemValue = (valueStr == "true" || valueStr == "1");
			this->ignoreWeatherSystem[category] = ignoreWeatherSystemValue;
			logger::debug("[SettingManager] Loaded [{}]::IgnoreWeatherSystem = {}", category, ignoreWeatherSystemValue);
		} else {
			// Set default value
			this->ignoreWeatherSystem[category] = false;
		}

		// Load IgnoreWeatherSystemInterior setting
		result = GetPrivateProfileStringA(category.c_str(), "IgnoreWeatherSystemInterior", "true", buffer, sizeof(buffer), filePath.c_str());
		if (result > 0) {
			std::string valueStr = buffer;
			std::transform(valueStr.begin(), valueStr.end(), valueStr.begin(), ::tolower);
			bool ignoreWeatherSystemInteriorValue = (valueStr == "true" || valueStr == "1");
			this->ignoreWeatherSystemInterior[category] = ignoreWeatherSystemInteriorValue;
			logger::debug("[SettingManager] Loaded [{}]::IgnoreWeatherSystemInterior = {}", category, ignoreWeatherSystemInteriorValue);
		} else {
			// Set default value
			this->ignoreWeatherSystemInterior[category] = true;
		}
	}

	logger::debug("[SettingManager] Loaded weather ignore settings for {} categories", categoriesWithWeather.size());
}

bool SettingManager::GetIgnoreWeatherSystem(const std::string& category) const
{
	auto it = ignoreWeatherSystem.find(category);
	return (it != ignoreWeatherSystem.end()) ? it->second : false;  // Default to false
}

bool SettingManager::GetIgnoreWeatherSystemInterior(const std::string& category) const
{
	auto it = ignoreWeatherSystemInterior.find(category);
	return (it != ignoreWeatherSystemInterior.end()) ? it->second : true;  // Default to true
}

void SettingManager::SetIgnoreWeatherSystem(const std::string& category, bool ignore)
{
	ignoreWeatherSystem[category] = ignore;
}

void SettingManager::SetIgnoreWeatherSystemInterior(const std::string& category, bool ignore)
{
	ignoreWeatherSystemInterior[category] = ignore;
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