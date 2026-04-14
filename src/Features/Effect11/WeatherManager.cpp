#include "WeatherManager.h"

#include "EffectManager.h"
#include "SettingManager.h"
#include <Windows.h>
#include <filesystem>
#include <sstream>

/**
 * @brief Provides global access to the single WeatherManager instance.
 *
 * @return WeatherManager& Reference to the singleton WeatherManager instance.
 */
WeatherManager& WeatherManager::GetSingleton()
{
	static WeatherManager instance;
	return instance;
}

/**
 * @brief Initializes manager state from configuration files.
 *
 * Loads weather entries and the location-based weather override mappings from the enbseries configuration INI files so the manager is ready to resolve weather data at runtime.
 */
void WeatherManager::Initialize()
{
	LoadWeatherList();
	LoadLocationWeather();
}

/**
 * @brief Loads weather entries and builds ID mappings from enbseries/_weatherlist.ini.
 *
 * Reads INI sections whose names start with "WEATHER", extracts each section's
 * FileName and comma-separated WeatherIDs, and populates the manager's
 * internal weatherEntries and weatherIDMap. Existing entries are cleared
 * before loading. For each entry whose referenced weather file exists,
 * calls SettingManager::GetSingleton().LoadWeatherSettings(...) with the
 * entry's weather IDs and file path. Sections missing required keys or
 * invalid entries are skipped; missing files produce warnings.
 */
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

		// Load the weather file through SettingManager once for all associated IDs
		std::filesystem::path weatherFilePath = "enbseries/" + entry.fileName;
		if (std::filesystem::exists(weatherFilePath)) {
			SettingManager::GetSingleton().LoadWeatherSettings(entry.weatherIDs, weatherFilePath.string());
		} else {
			logger::warn("[WeatherManager] Weather file not found: {}", weatherFilePath.string());
		}

		weatherEntries[sectionName] = std::move(entry);
		for (uint32_t weatherID : weatherEntries[sectionName].weatherIDs) {
			weatherIDMap[weatherID] = sectionName;
		}
	}
}

/**
 * @brief Retrieve the WeatherEntry associated with a weather ID when per-location/multiple weathers are enabled.
 *
 * Looks up the internal mapping for the provided weather ID and returns a pointer to the stored WeatherEntry if the
 * "enableMultipleWeathers" setting is enabled and a matching entry exists.
 *
 * @param weatherID Weather form ID to look up (parsed hex form ID).
 * @return WeatherEntry* Pointer to the WeatherEntry stored inside WeatherManager, or `nullptr` if multiple weathers are
 *         disabled or no matching entry is found.
 */
WeatherManager::WeatherEntry* WeatherManager::FindWeatherEntry(uint32_t weatherID)
{
	auto& effectManager = EffectManager::GetSingleton();
	if (!SettingManager::GetSingleton().GetValueInternal<bool>(effectManager.ids.enableMultipleWeathers)) {
		return nullptr;
	}

	auto it = weatherIDMap.find(weatherID);
	if (it != weatherIDMap.end()) {
		auto entryIt = weatherEntries.find(it->second);
		if (entryIt != weatherEntries.end()) {
			return &entryIt->second;
		}
	}
	return nullptr;
}

/**
 * @brief Parse a comma-separated list of hexadecimal weather IDs into a vector of numeric IDs.
 *
 * Parses `weatherIDsStr` for comma-separated hex tokens, trims surrounding spaces and tabs,
 * converts each non-empty token to a uint32_t via hex parsing, appends only non-zero IDs
 * to `weatherIDs`, and leaves `weatherIDs` empty if no valid IDs are found.
 *
 * @param weatherIDsStr String containing comma-separated hexadecimal weather IDs (e.g. "0x1A, 2B, 3C").
 * @param[out] weatherIDs Destination vector which will be cleared and populated with parsed IDs.
 */
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

/**
 * @brief Parses a hexadecimal string into a 32-bit unsigned identifier.
 *
 * @param hexStr Hexadecimal string representing the ID; an empty string yields no ID.
 * @return uint32_t The parsed unsigned 32-bit value, or `0` if `hexStr` is empty.
 */
uint32_t WeatherManager::ParseHexID(const std::string& hexStr)
{
	if (hexStr.empty()) {
		return 0;
	}

	return static_cast<uint32_t>(std::stoul(hexStr, nullptr, 16));
}

/**
 * @brief Loads location-specific weather overrides from enbseries/_locationweather.ini.
 *
 * Parses the INI where each section name is a worldspace ID (hex) and each entry
 * within a section is of the form `locationID=fakeWeatherID` (both hex). Populates
 * the member map `locationWeatherMap` with mappings worldSpaceID -> (locationID -> fakeWeatherID).
 * Lines that are empty, commented (start with '/' or ';'), malformed, or parse to zero IDs are ignored.
 * If the INI file does not exist, the function logs that location weather is disabled and leaves
 * `locationWeatherMap` empty.
 */
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

/**
 * @brief Determine the effective weather ID for the player based on location overrides.
 *
 * When location-based weather is enabled and a mapping exists for the player's current
 * worldspace and location, returns the configured override; otherwise returns the input ID.
 *
 * @param actualWeatherID The original weather ID to consider for remapping.
 * @return uint32_t The override weather ID for the player's location if present; otherwise `actualWeatherID`.
 */
uint32_t WeatherManager::GetEffectiveWeatherID(uint32_t actualWeatherID)
{
	auto& effectManager = EffectManager::GetSingleton();
	if (!SettingManager::GetSingleton().GetValue<bool>(effectManager.ids.enableLocationWeather)) {
		return actualWeatherID;
	}

	if (locationWeatherMap.empty()) {
		return actualWeatherID;
	}

	auto player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return actualWeatherID;
	}

	RE::TESObjectCELL* parentCell = nullptr;
	try {
		parentCell = player->GetParentCell();
	} catch (...) {
		return actualWeatherID;
	}

	if (!parentCell) {
		return actualWeatherID;
	}

	uint32_t worldSpaceID = 0;
	uint32_t locationID = 0;

	try {
		if (auto worldSpace = parentCell->GetRuntimeData().worldSpace) {
			worldSpaceID = worldSpace->GetFormID() & 0x00FFFFFF;
		}

		if (auto location = parentCell->GetLocation()) {
			locationID = location->GetFormID() & 0x00FFFFFF;
		}
	} catch (...) {
		return actualWeatherID;
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

/**
 * @brief Builds a mapping of loaded weather IDs to their weather file paths.
 *
 * Each loaded WeatherEntry contributes one map entry per parsed weather ID.
 *
 * @return std::unordered_map<std::string, std::string> A map where each key is
 *         "weather_<id>" (decimal ID) and each value is the relative path to
 *         the weather file in the form "enbseries/<fileName>".
 */
std::unordered_map<std::string, std::string> WeatherManager::GetWeatherFiles() const
{
	std::unordered_map<std::string, std::string> result;

	for (const auto& [sectionName, entry] : weatherEntries) {
		std::string weatherFilePath = "enbseries/" + entry.fileName;
		for (uint32_t weatherID : entry.weatherIDs) {
			result["weather_" + std::to_string(weatherID)] = weatherFilePath;
		}
	}

	return result;
}