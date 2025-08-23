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

	WeatherEntry* FindWeatherEntry(uint32_t weatherID);

	const std::unordered_map<std::string, WeatherEntry>& GetWeatherEntries() const { return weatherEntries; }

private:
	std::unordered_map<std::string, WeatherEntry> weatherEntries;
	std::unordered_map<uint32_t, std::string> weatherIDMap;

	void ParseWeatherIDs(const std::string& weatherIDsStr, std::vector<uint32_t>& weatherIDs);
	uint32_t ParseHexID(const std::string& hexStr);
};