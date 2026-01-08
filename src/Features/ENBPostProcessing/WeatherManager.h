#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

class WeatherManager
{
public:
	static WeatherManager& GetSingleton();

	struct WeatherEntry
	{
		std::string fileName;
		std::vector<uint32_t> weatherIDs;
	};

	void Initialize();
	void LoadWeatherList();
	void LoadLocationWeather();

	WeatherEntry* FindWeatherEntry(uint32_t weatherID);

	/// @brief Gets the effective weather ID, checking for location-based overrides first.
	/// @param actualWeatherID The real weather form ID from the game
	/// @return Location-mapped weather ID if applicable, otherwise the actual weather ID
	uint32_t GetEffectiveWeatherID(uint32_t actualWeatherID);

	const std::unordered_map<std::string, WeatherEntry>& GetWeatherEntries() const { return weatherEntries; }

private:
	std::unordered_map<std::string, WeatherEntry> weatherEntries;
	std::unordered_map<uint32_t, std::string> weatherIDMap;

	// Location weather: worldSpaceID -> (locationID -> fakeWeatherID)
	std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint32_t>> locationWeatherMap;

	void ParseWeatherIDs(const std::string& weatherIDsStr, std::vector<uint32_t>& weatherIDs);
	uint32_t ParseHexID(const std::string& hexStr);
};