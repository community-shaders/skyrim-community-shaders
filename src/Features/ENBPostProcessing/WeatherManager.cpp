#include "WeatherManager.h"

#include "SettingManager.h"
#include <Windows.h>
#include <filesystem>
#include <sstream>

WeatherManager& WeatherManager::GetSingleton()
{
	static WeatherManager instance;
	return instance;
}

void WeatherManager::Initialize()
{
	LoadWeatherList();
	LoadLocationWeather();
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

		// Load the weather file through SettingManager
		std::filesystem::path weatherFilePath = "enbseries/" + entry.fileName;
		if (std::filesystem::exists(weatherFilePath)) {
			// Create weather key for each weather ID
			for (uint32_t weatherID : entry.weatherIDs) {
				std::ostringstream oss;
				oss << "weather_" << weatherID;
				std::string weatherKey = oss.str();
				SettingManager::GetSingleton().LoadWeatherSettings(weatherKey, weatherFilePath.string());
			}
		} else {
			logger::warn("[WeatherManager] Weather file not found: {}", weatherFilePath.string());
		}

		weatherEntries[sectionName] = std::move(entry);
		for (uint32_t weatherID : weatherEntries[sectionName].weatherIDs) {
			weatherIDMap[weatherID] = sectionName;
		}
	}
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

uint32_t WeatherManager::ParseHexID(const std::string& hexStr)
{
	if (hexStr.empty()) {
		return 0;
	}

	return static_cast<uint32_t>(std::stoul(hexStr, nullptr, 16));
}

void WeatherManager::LoadLocationWeather()
{
	std::filesystem::path locationWeatherPath = "enbseries/_locationweather.ini";

	if (!std::filesystem::exists(locationWeatherPath)) {
		logger::info("[WeatherManager] _locationweather.ini not found, location weather disabled");
		return;
	}

	locationWeatherMap.clear();

	std::string pathStr = locationWeatherPath.string();

	constexpr DWORD bufferSize = 32768;
	std::vector<char> buffer(bufferSize);
	DWORD result = GetPrivateProfileSectionNamesA(buffer.data(), bufferSize, pathStr.c_str());

	if (result == 0 || result == bufferSize - 2) {
		logger::error("[WeatherManager] Failed to read sections from _locationweather.ini");
		return;
	}

	const char* ptr = buffer.data();
	while (*ptr != '\0') {
		std::string sectionName = ptr;
		ptr += sectionName.length() + 1;

		uint32_t worldSpaceID = 0;
		try {
			worldSpaceID = ParseHexID(sectionName);
		} catch (...) {
			continue;
		}

		std::vector<char> sectionBuffer(bufferSize);
		DWORD sectionResult = GetPrivateProfileSectionA(sectionName.c_str(), sectionBuffer.data(), bufferSize, pathStr.c_str());

		if (sectionResult == 0 || sectionResult == bufferSize - 2) {
			continue;
		}

		const char* entryPtr = sectionBuffer.data();
		while (*entryPtr != '\0') {
			std::string entry = entryPtr;
			entryPtr += entry.length() + 1;

			if (entry.empty() || entry[0] == '/' || entry[0] == ';') {
				continue;
			}

			size_t eqPos = entry.find('=');
			if (eqPos == std::string::npos) {
				continue;
			}

			std::string locationStr = entry.substr(0, eqPos);
			std::string weatherStr = entry.substr(eqPos + 1);

			try {
				uint32_t locationID = ParseHexID(locationStr);
				uint32_t fakeWeatherID = ParseHexID(weatherStr);
				if (locationID != 0 && fakeWeatherID != 0) {
					locationWeatherMap[worldSpaceID][locationID] = fakeWeatherID;
				}
			} catch (...) {
				continue;
			}
		}
	}

	logger::info("[WeatherManager] Loaded location weather for {} worldspaces", locationWeatherMap.size());
}

uint32_t WeatherManager::GetEffectiveWeatherID(uint32_t actualWeatherID)
{
	if (locationWeatherMap.empty()) {
		return actualWeatherID;
	}

	auto player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return actualWeatherID;
	}

	RE::TESObjectCELL* parentCell = player->GetParentCell();
	if (!parentCell) {
		return actualWeatherID;
	}

	uint32_t worldSpaceID = 0;
	if (auto worldSpace = parentCell->GetRuntimeData().worldSpace) {
		worldSpaceID = worldSpace->GetFormID() & 0x00FFFFFF;
	}

	uint32_t locationID = 0;
	if (auto location = parentCell->GetLocation()) {
		locationID = location->GetFormID() & 0x00FFFFFF;
	}

	if (locationID == 0) {
		return actualWeatherID;
	}

	auto worldIt = locationWeatherMap.find(worldSpaceID);
	if (worldIt == locationWeatherMap.end()) {
		return actualWeatherID;
	}

	auto locIt = worldIt->second.find(locationID);
	if (locIt != worldIt->second.end()) {
		return locIt->second;
	}

	return actualWeatherID;
}