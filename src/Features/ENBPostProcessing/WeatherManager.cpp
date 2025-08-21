#include "WeatherManager.h"
#include "SettingsRegistry.h"
#include "PCH.h"
#include <Windows.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

WeatherManager& WeatherManager::GetSingleton()
{
	static WeatherManager instance;
	return instance;
}

void WeatherManager::Initialize()
{
	LoadWeatherList();
}

void WeatherManager::LoadWeatherList()
{
	std::filesystem::path weatherListPath = "enbseries/_weatherlist.ini";

	if (!std::filesystem::exists(weatherListPath)) {
		logger::warn("[WeatherManager] _weatherlist.ini not found at {}", weatherListPath.string());
		return;
	}

	// Clear existing data
	weatherEntries.clear();
	weatherIDMap.clear();

	// Use GetPrivateProfileString to enumerate sections
	std::string weatherListPathStr = weatherListPath.string();

	// Get all section names
	constexpr DWORD bufferSize = 32768;
	std::vector<char> buffer(bufferSize);
	DWORD result = GetPrivateProfileSectionNamesA(buffer.data(), bufferSize, weatherListPathStr.c_str());

	if (result == 0 || result == bufferSize - 2) {
		logger::error("[WeatherManager] Failed to read sections from _weatherlist.ini");
		return;
	}

	// Parse section names (null-separated strings)
	const char* ptr = buffer.data();
	while (*ptr != '\0') {
		std::string sectionName = ptr;
		ptr += sectionName.length() + 1;

		// Skip non-weather sections
		if (sectionName.find("WEATHER") != 0) {
			continue;
		}

		// Get filename
		char fileName[MAX_PATH] = {};
		GetPrivateProfileStringA(sectionName.c_str(), "FileName", "", fileName, MAX_PATH, weatherListPathStr.c_str());
		if (strlen(fileName) == 0) {
			continue;  // Skip empty weather entries
		}

		// Get weather IDs
		char weatherIDsStr[1024] = {};
		GetPrivateProfileStringA(sectionName.c_str(), "WeatherIDs", "", weatherIDsStr, 1024, weatherListPathStr.c_str());
		if (strlen(weatherIDsStr) == 0) {
			continue;  // Skip entries without weather IDs
		}

		WeatherEntry entry;
		entry.fileName = fileName;
		ParseWeatherIDs(weatherIDsStr, entry.weatherIDs);

		// Load the weather file through SettingsRegistry
		std::filesystem::path weatherFilePath = "enbseries/" + entry.fileName;
		if (std::filesystem::exists(weatherFilePath)) {
			// Create weather key for each weather ID
			for (uint32_t weatherID : entry.weatherIDs) {
				std::ostringstream oss;
				oss << "weather_" << weatherID;
				std::string weatherKey = oss.str();
				SettingsRegistry::GetSingleton().LoadWeatherSettings(weatherKey, weatherFilePath.string());
			}
		} else {
			logger::warn("[WeatherManager] Weather file not found: {}", weatherFilePath.string());
		}

		// Store entry and map weather IDs
		weatherEntries[sectionName] = std::move(entry);

		for (uint32_t weatherID : weatherEntries[sectionName].weatherIDs) {
			weatherIDMap[weatherID] = sectionName;
		}
	}

	logger::info("[WeatherManager] Loaded {} weather entries from _weatherlist.ini", weatherEntries.size());
}

void WeatherManager::LoadWeatherFile(const std::string& filePath, WeatherSettings& settings)
{
	if (!std::filesystem::exists(filePath)) {
		logger::warn("[WeatherManager] Weather file not found: {}", filePath);
		return;
	}

	// Load BLOOM settings
	LoadTimeOfDaySettings(filePath, "BLOOM", "Amount", settings.BLOOM.Amount);

	// Load LENS settings
	LoadTimeOfDaySettings(filePath, "LENS", "Amount", settings.LENS.Amount);

	logger::debug("[WeatherManager] Loaded weather settings from: {}", filePath);
}

WeatherManager::WeatherEntry* WeatherManager::FindWeatherEntry(uint32_t weatherID)
{
	auto it = weatherIDMap.find(weatherID);
	if (it != weatherIDMap.end()) {
		auto entryIt = weatherEntries.find(it->second);
		if (entryIt != weatherEntries.end()) {
			return &entryIt->second;
		}
	}
	return nullptr;
}

WeatherManager::WeatherSettings WeatherManager::GetInterpolatedSettings(uint32_t currentWeatherID, uint32_t lastWeatherID, float blendFactor)
{
	WeatherEntry* currentEntry = FindWeatherEntry(currentWeatherID);
	WeatherEntry* lastEntry = FindWeatherEntry(lastWeatherID);

	// If we don't have weather files, return default settings
	if (!currentEntry && !lastEntry) {
		return WeatherSettings{};
	}

	// If only one weather has settings, use it
	if (!lastEntry || blendFactor >= 1.0f) {
		return currentEntry ? currentEntry->settings : WeatherSettings{};
	}

	if (!currentEntry || blendFactor <= 0.0f) {
		return lastEntry ? lastEntry->settings : WeatherSettings{};
	}

	// Interpolate between the two weather settings
	WeatherSettings result;

	// Helper lambda for interpolating TimeOfDaySettings
	auto lerpTimeOfDay = [](const TimeOfDaySettings& a, const TimeOfDaySettings& b, float t) -> TimeOfDaySettings {
		TimeOfDaySettings result;
		result.Dawn = a.Dawn + t * (b.Dawn - a.Dawn);
		result.Sunrise = a.Sunrise + t * (b.Sunrise - a.Sunrise);
		result.Day = a.Day + t * (b.Day - a.Day);
		result.Sunset = a.Sunset + t * (b.Sunset - a.Sunset);
		result.Dusk = a.Dusk + t * (b.Dusk - a.Dusk);
		result.Night = a.Night + t * (b.Night - a.Night);
		result.InteriorDay = a.InteriorDay + t * (b.InteriorDay - a.InteriorDay);
		result.InteriorNight = a.InteriorNight + t * (b.InteriorNight - a.InteriorNight);
		return result;
	};

	// Interpolate BLOOM settings
	result.BLOOM.Amount = lerpTimeOfDay(lastEntry->settings.BLOOM.Amount, currentEntry->settings.BLOOM.Amount, blendFactor);

	// Interpolate LENS settings
	result.LENS.Amount = lerpTimeOfDay(lastEntry->settings.LENS.Amount, currentEntry->settings.LENS.Amount, blendFactor);

	return result;
}

float WeatherManager::ComputeTimeOfDayValue(const TimeOfDaySettings& settings, const float timeOfDay1[4], const float timeOfDay2[4], float interiorFactor)
{
	float result = 0.0f;

	if (interiorFactor > 0.5f) {
		// Interior - interpolate between InteriorDay and InteriorNight based on time
		// Use a simple day/night cycle for interior
		float dayNightFactor = (timeOfDay1[2] + timeOfDay1[1] + timeOfDay1[0] * 0.5f + timeOfDay1[3] * 0.5f);  // More day-like during dawn/dusk
		result = settings.InteriorNight + dayNightFactor * (settings.InteriorDay - settings.InteriorNight);
	} else {
		// Exterior - use full time-of-day interpolation
		// timeOfDay1: [Dawn, Sunrise, Day, Sunset] (x, y, z, w)
		// timeOfDay2: [Dusk, Night, 0, 0] (x, y, z, w)
		result += timeOfDay1[0] * settings.Dawn;     // Dawn
		result += timeOfDay1[1] * settings.Sunrise;  // Sunrise
		result += timeOfDay1[2] * settings.Day;      // Day
		result += timeOfDay1[3] * settings.Sunset;   // Sunset
		result += timeOfDay2[0] * settings.Dusk;     // Dusk
		result += timeOfDay2[1] * settings.Night;    // Night
	}

	return result;
}

void WeatherManager::ParseWeatherIDs(const std::string& weatherIDsStr, std::vector<uint32_t>& weatherIDs)
{
	weatherIDs.clear();

	std::stringstream ss(weatherIDsStr);
	std::string token;

	while (std::getline(ss, token, ',')) {
		// Trim whitespace
		token.erase(0, token.find_first_not_of(" \t"));
		token.erase(token.find_last_not_of(" \t") + 1);

		if (!token.empty()) {
			try {
				uint32_t weatherID = ParseHexID(token);
				if (weatherID != 0) {
					weatherIDs.push_back(weatherID);
				}
			} catch (const std::exception& e) {
				logger::warn("[WeatherManager] Failed to parse weather ID '{}': {}", token, e.what());
			}
		}
	}
}

void WeatherManager::LoadTimeOfDaySettings(const std::string& filePath, const std::string& section, const std::string& prefix, TimeOfDaySettings& settings)
{
	const std::vector<std::string> timeOfDayNames = { "Dawn", "Sunrise", "Day", "Sunset", "Dusk", "Night", "InteriorDay", "InteriorNight" };

	for (const auto& timeOfDay : timeOfDayNames) {
		std::string key = prefix + timeOfDay;
		char buffer[64];
		GetPrivateProfileStringA(section.c_str(), key.c_str(), "1.0", buffer, sizeof(buffer), filePath.c_str());
		settings[timeOfDay] = static_cast<float>(atof(buffer));
	}
}

uint32_t WeatherManager::ParseHexID(const std::string& hexStr)
{
	if (hexStr.empty()) {
		return 0;
	}

	// Handle hex strings with or without "0x" prefix
	std::string processedStr = hexStr;
	if (processedStr.substr(0, 2) == "0x" || processedStr.substr(0, 2) == "0X") {
		processedStr = processedStr.substr(2);
	}

	// Convert to uppercase for consistent parsing
	std::transform(processedStr.begin(), processedStr.end(), processedStr.begin(), ::toupper);

	return static_cast<uint32_t>(std::stoul(processedStr, nullptr, 16));
}