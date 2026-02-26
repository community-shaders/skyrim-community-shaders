#pragma once

#include "WeatherVariableRegistry.h"
#include <map>
#include <string>

using json = nlohmann::json;

class WeatherManager
{
public:
	static WeatherManager* GetSingleton()
	{
		static WeatherManager singleton;
		return &singleton;
	}

	struct CurrentWeathers
	{
		RE::TESWeather* currentWeather = nullptr;
		RE::TESWeather* lastWeather = nullptr;
		float lerpFactor = 0.0f;
	};

	// Get current weather state and transition info
	CurrentWeathers GetCurrentWeathers();

	// Load all per-weather settings from disk into cache
	void LoadPerWeatherSettingsFromDisk();

	// Per-frame update - notify features of weather changes
	void UpdateFeatures();

	// Save feature settings for a specific weather
	void SaveSettingsToWeather(RE::TESWeather* weather, const std::string& featureName, const json& settings);

	// Load feature settings for a specific weather
	bool LoadSettingsFromWeather(RE::TESWeather* weather, const std::string& featureName, json& o_json);

	// Get the weather key for caching
	static std::string GetWeatherKey(RE::TESWeather* weather);

	// Clear all cached feature settings for a specific weather
	void ClearAllFeatureSettingsForWeather(RE::TESWeather* weather);

	// Check if settings exist for a weather
	bool HasWeatherSettings(RE::TESWeather* weather) const;

	// Clear all cached settings
	void ClearCache();

private:
	WeatherManager() = default;
	~WeatherManager() = default;
	WeatherManager(const WeatherManager&) = delete;
	WeatherManager& operator=(const WeatherManager&) = delete;

	// Cache of all loaded per-weather settings: weatherKey -> featureName -> settings
	std::map<std::string, std::map<std::string, json>> perWeatherSettingsCache;

	// Track last known weather state to detect changes
	CurrentWeathers lastKnownWeather;

	// Cached last weather - sky->lastWeather can be cleared before currentWeatherPct reaches 1.0
	RE::TESWeather* cachedLastWeather = nullptr;
};
