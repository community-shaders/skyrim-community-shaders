#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

class WeatherManager
{
public:
	static WeatherManager& GetSingleton();

	struct TimeOfDaySettings
	{
		float Dawn = 1.0f;
		float Sunrise = 1.0f;
		float Day = 1.0f;
		float Sunset = 1.0f;
		float Dusk = 1.0f;
		float Night = 1.0f;
		float InteriorDay = 1.0f;
		float InteriorNight = 1.0f;

		float& operator[](const std::string& timeOfDay)
		{
			if (timeOfDay == "Dawn")
				return Dawn;
			if (timeOfDay == "Sunrise")
				return Sunrise;
			if (timeOfDay == "Day")
				return Day;
			if (timeOfDay == "Sunset")
				return Sunset;
			if (timeOfDay == "Dusk")
				return Dusk;
			if (timeOfDay == "Night")
				return Night;
			if (timeOfDay == "InteriorDay")
				return InteriorDay;
			if (timeOfDay == "InteriorNight")
				return InteriorNight;
			return Dawn;  // fallback
		}

		const float& operator[](const std::string& timeOfDay) const
		{
			if (timeOfDay == "Dawn")
				return Dawn;
			if (timeOfDay == "Sunrise")
				return Sunrise;
			if (timeOfDay == "Day")
				return Day;
			if (timeOfDay == "Sunset")
				return Sunset;
			if (timeOfDay == "Dusk")
				return Dusk;
			if (timeOfDay == "Night")
				return Night;
			if (timeOfDay == "InteriorDay")
				return InteriorDay;
			if (timeOfDay == "InteriorNight")
				return InteriorNight;
			return Dawn;  // fallback
		}
	};

	struct WeatherSettings
	{
		struct
		{
			TimeOfDaySettings Amount;
		} BLOOM;

		struct
		{
			TimeOfDaySettings Amount;
		} LENS;
	};

	struct WeatherEntry
	{
		std::string fileName;
		std::vector<uint32_t> weatherIDs;
		WeatherSettings settings;
	};

	// Lifecycle
	void Initialize();
	void LoadWeatherList();
	void LoadWeatherFile(const std::string& filePath, WeatherSettings& settings);

	// Weather lookup
	WeatherEntry* FindWeatherEntry(uint32_t weatherID);
	WeatherSettings GetInterpolatedSettings(uint32_t currentWeatherID, uint32_t lastWeatherID, float blendFactor);

	// Helper function to compute time-of-day interpolated values
	float ComputeTimeOfDayValue(const TimeOfDaySettings& settings, const float timeOfDay1[4], const float timeOfDay2[4], float interiorFactor);

	// Get all weather entries for UI
	const std::unordered_map<std::string, WeatherEntry>& GetWeatherEntries() const { return weatherEntries; }
	const std::unordered_map<uint32_t, std::string>& GetWeatherIDMap() const { return weatherIDMap; }

private:
	std::unordered_map<std::string, WeatherEntry> weatherEntries;
	std::unordered_map<uint32_t, std::string> weatherIDMap;

	// Helper functions
	void ParseWeatherIDs(const std::string& weatherIDsStr, std::vector<uint32_t>& weatherIDs);
	void LoadTimeOfDaySettings(const std::string& filePath, const std::string& section, const std::string& prefix, TimeOfDaySettings& settings);
	uint32_t ParseHexID(const std::string& hexStr);
};