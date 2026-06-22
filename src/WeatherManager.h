#pragma once

#include "WeatherVariableRegistry.h"
#include <map>
#include <string>

using json = nlohmann::json;

/**
 * @brief Manages per-weather feature settings and drives weather-dependent variable interpolation.
 *
 * Loads weather-specific JSON overrides from disk, caches them in memory, and
 * each frame interpolates registered weather variables between the outgoing and
 * incoming weather states using the engine's transition lerp factor.
 */
class WeatherManager
{
public:
	/** @brief Returns the global singleton instance. */
	static WeatherManager* GetSingleton()
	{
		static WeatherManager singleton;
		return &singleton;
	}

	/** @brief Snapshot of the current and previous weather with the engine transition factor. */
	struct CurrentWeathers
	{
		RE::TESWeather* currentWeather = nullptr;
		RE::TESWeather* lastWeather = nullptr;
		float lerpFactor = 0.0f;
	};

	/**
	 * @brief Queries the engine for the active weather transition state.
	 *
	 * Caches the last weather pointer to handle cases where the engine clears
	 * it before the transition lerp factor reaches 1.0.
	 *
	 * @return Current weather pair and interpolation factor.
	 */
	CurrentWeathers GetCurrentWeathers();

	/**
	 * @brief Loads all per-weather JSON settings files from the Weathers directory into the in-memory cache.
	 *
	 * Scans the CommunityShaders/Weathers/ directory for .json files and populates
	 * the internal cache keyed by weather form identifier.
	 */
	void LoadPerWeatherSettingsFromDisk();

	/**
	 * @brief Per-frame update that detects weather changes and interpolates registered feature variables.
	 *
	 * Manages transition lifecycle (begin/end) and lerps all weather-registered
	 * variables between outgoing and incoming weather override values.
	 */
	void UpdateFeatures();

	/**
	 * @brief Persists feature settings for a specific weather to both cache and disk.
	 *
	 * If settings is an empty object, the feature entry is removed from the weather file.
	 * If all feature entries are removed, the weather file itself is deleted.
	 *
	 * @param weather The weather form to associate settings with.
	 * @param featureName Short name of the feature owning these settings.
	 * @param settings JSON object containing the feature's weather-specific overrides.
	 */
	void SaveSettingsToWeather(RE::TESWeather* weather, const std::string& featureName, const json& settings);

	/**
	 * @brief Loads cached feature settings for a specific weather.
	 *
	 * Returns false if no override exists or the override's __enabled flag is false.
	 *
	 * @param weather The weather form to look up.
	 * @param featureName Short name of the feature to retrieve settings for.
	 * @param o_json Output parameter receiving the feature's weather-specific settings.
	 * @return True if enabled settings were found and written to o_json.
	 */
	bool LoadSettingsFromWeather(RE::TESWeather* weather, const std::string& featureName, json& o_json);

	/**
	 * @brief Generates a stable string key for a weather form, used as the cache and filename identifier.
	 * @param weather The weather form to generate a key for.
	 * @return A string uniquely identifying the weather form and its source plugin.
	 */
	static std::string GetWeatherKey(RE::TESWeather* weather);

	/**
	 * @brief Removes all cached feature settings for a specific weather.
	 * @param weather The weather form whose cached settings should be erased.
	 */
	void ClearAllFeatureSettingsForWeather(RE::TESWeather* weather);

	/**
	 * @brief Checks whether any cached settings exist for the given weather.
	 * @param weather The weather form to check.
	 * @return True if the cache contains at least one feature entry for this weather.
	 */
	bool HasWeatherSettings(RE::TESWeather* weather) const;

	/** @brief Clears all cached per-weather settings and resets the weather state tracker. */
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
