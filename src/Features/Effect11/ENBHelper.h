#pragma once

// ENB Helper functionality
// Based on ENBHelperSE by aers (https://github.com/xSyphel/ENBHelperSE)
// Original code licensed under MIT License - Copyright (c) 2021 aers

#include <cstdint>

/**
 * Cached weather state for the current frame.
 *
 * currentWeatherFormID: form ID of the active weather.
 * outgoingWeatherFormID: form ID of the weather being transitioned to.
 * weatherTransition: interpolation factor from current to outgoing weather (0.0 to 1.0).
 * currentClassification: classification of the current weather (0=Pleasant, 1=Cloudy, 2=Rainy, 3=Snow, -1=Unknown).
 * outgoingClassification: classification of the outgoing weather or -1 if unknown.
 */

/**
 * Cached location state for the current frame.
 *
 * locationFormID: form ID of the current location.
 * worldSpaceFormID: form ID of the current world space.
 * isInterior: true if the player is in an interior cell.
 */

/**
 * Cached time and day information for the current frame.
 *
 * gameHour: current in-game hour (0.0–24.0).
 * dayOfYear: current day of year.
 * skyMode: engine sky mode identifier.
 */

/**
 * Cached camera transform for the current frame.
 *
 * position: world-space camera position.
 * rotation: camera rotation matrix.
 */

/**
 * Update all cached ENBHelper values for the current frame.
 *
 * Call once per frame before reading any cached data.
 */

/**
 * Get the cached WeatherInfo for the current frame.
 *
 * @returns const reference to the frame-local WeatherInfo.
 */

/**
 * Get the cached LocationInfo for the current frame.
 *
 * @returns const reference to the frame-local LocationInfo.
 */

/**
 * Get the cached TimeInfo for the current frame.
 *
 * @returns const reference to the frame-local TimeInfo.
 */

/**
 * Get the cached CameraInfo for the current frame.
 *
 * @returns const reference to the frame-local CameraInfo.
 */

/**
 * Retrieve the current weather form ID.
 *
 * @param[out] formID Receives the current weather form ID on success.
 * @returns `true` if a valid current weather form ID was written to `formID`, `false` otherwise.
 */

/**
 * Retrieve the outgoing (target) weather form ID.
 *
 * @param[out] formID Receives the outgoing weather form ID on success.
 * @returns `true` if a valid outgoing weather form ID was written to `formID`, `false` otherwise.
 */

/**
 * Retrieve the current weather transition factor.
 *
 * @param[out] transition Receives the weather transition factor (0.0–1.0) on success.
 * @returns `true` if a valid transition value was written to `transition`, `false` otherwise.
 */

/**
 * Retrieve the current weather classification.
 *
 * @param[out] classification Receives the current weather classification on success (0=Pleasant, 1=Cloudy, 2=Rainy, 3=Snow, -1=Unknown).
 * @returns `true` if a classification was written to `classification`, `false` otherwise.
 */

/**
 * Retrieve the outgoing weather classification.
 *
 * @param[out] classification Receives the outgoing weather classification on success (0=Pleasant, 1=Cloudy, 2=Rainy, 3=Snow, -1=Unknown).
 * @returns `true` if a classification was written to `classification`, `false` otherwise.
 */

/**
 * Retrieve the current location form ID.
 *
 * @param[out] formID Receives the current location form ID on success.
 * @returns `true` if a valid location form ID was written to `formID`, `false` otherwise.
 */

/**
 * Retrieve the current world space form ID.
 *
 * @param[out] formID Receives the current world space form ID on success.
 * @returns `true` if a valid world space form ID was written to `formID`, `false` otherwise.
 */

/**
 * Retrieve the current in-game hour.
 *
 * @param[out] hour Receives the current game hour (0.0–24.0) on success.
 * @returns `true` if a valid hour was written to `hour`, `false` otherwise.
 */

/**
 * Retrieve the current sky mode.
 *
 * @param[out] mode Receives the current engine sky mode identifier on success.
 * @returns `true` if a valid sky mode was written to `mode`, `false` otherwise.
 */
namespace ENBHelper
{
	struct WeatherInfo
	{
		uint32_t currentWeatherFormID = 0;
		uint32_t outgoingWeatherFormID = 0;
		float weatherTransition = 0.0f;
		int32_t currentClassification = -1;  // 0=Pleasant, 1=Cloudy, 2=Rainy, 3=Snow, -1=Unknown
		int32_t outgoingClassification = -1;
	};

	struct LocationInfo
	{
		uint32_t locationFormID = 0;
		uint32_t worldSpaceFormID = 0;
		bool isInterior = false;
	};

	struct TimeInfo
	{
		float gameHour = 12.0f;
		float dayOfYear = 0.0f;
		uint32_t skyMode = 0;
	};

	struct CameraInfo
	{
		RE::NiPoint3 position = { 0.0f, 0.0f, 0.0f };
		RE::NiMatrix3 rotation;
	};

	// Update all cached values - call once per frame
	void Update();

	// Accessors for cached data
	const WeatherInfo& GetWeatherInfo();
	const LocationInfo& GetLocationInfo();
	const TimeInfo& GetTimeInfo();
	const CameraInfo& GetCameraInfo();

	// Individual getters (for compatibility with ENBHelperSE API)
	bool GetCurrentWeather(uint32_t& formID);
	bool GetOutgoingWeather(uint32_t& formID);
	bool GetWeatherTransition(float& transition);
	bool GetCurrentWeatherClassification(int32_t& classification);
	bool GetOutgoingWeatherClassification(int32_t& classification);
	bool GetCurrentLocationID(uint32_t& formID);
	bool GetWorldSpaceID(uint32_t& formID);
	bool GetTime(float& hour);
	bool GetSkyMode(uint32_t& mode);
}
