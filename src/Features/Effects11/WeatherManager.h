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

	/// @brief Gets the ENB SDK weather index of a weather ID: N for its [WEATHERnnn] section.
	/// @return 0 when the weather is not listed or EnableMultipleWeathers is off ("weather not captured")
	uint32_t GetWeatherIndex(uint32_t weatherID) const;

	const std::unordered_map<std::string, WeatherEntry>& GetWeatherEntries() const { return weatherEntries; }

	std::unordered_map<std::string, std::string> GetWeatherFiles() const;

	/** @brief Drops the load-order index; ENB weather list and location keys are matched without it. */
	static constexpr uint32_t LocalFormIDMask = 0x00FFFFFF;

private:
	std::unordered_map<std::string, WeatherEntry> weatherEntries;
	std::unordered_map<uint32_t, std::string> weatherIDMap;

	// Location weather: worldSpaceID -> (locationID -> fakeWeatherID)
	std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint32_t>> locationWeatherMap;

	void ParseWeatherIDs(const std::string& weatherIDsStr, std::vector<uint32_t>& weatherIDs);
	uint32_t ParseHexID(const std::string& hexStr);
};