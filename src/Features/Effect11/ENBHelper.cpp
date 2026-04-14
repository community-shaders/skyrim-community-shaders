#include "ENBHelper.h"

// ENB Helper functionality
// Based on ENBHelperSE by aers (https://github.com/xSyphel/ENBHelperSE)
// Original code licensed under MIT License - Copyright (c) 2021 aers

namespace ENBHelper
{
	namespace
	{
		WeatherInfo cachedWeather;
		LocationInfo cachedLocation;
		TimeInfo cachedTime;
		CameraInfo cachedCamera;

		/**
		 * @brief Map a weather record to a discrete weather classification.
		 *
		 * Maps the weather's data flags to a small integer code representing the
		 * dominant weather type. If the input is null or no recognized flags are
		 * present, the function reports an unknown classification.
		 *
		 * @param weather Pointer to the TESWeather record to classify; may be null.
		 * @return int32_t Classification code: `0` = pleasant, `1` = cloudy, `2` = rainy,
		 * `3` = snow, `-1` = unknown or `weather` is null.
		 */
		int32_t GetClassification(RE::TESWeather* weather)
		{
			if (!weather)
				return -1;

			using Flags = RE::TESWeather::WeatherDataFlag;
			const auto flags = weather->data.flags;

			if (flags.any(Flags::kPleasant))
				return 0;
			if (flags.any(Flags::kCloudy))
				return 1;
			if (flags.any(Flags::kRainy))
				return 2;
			if (flags.any(Flags::kSnow))
				return 3;

			return -1;
		}
	}

	/**
	 * @brief Populates cached weather, time, location, and camera information from engine singletons.
	 *
	 * Updates cachedWeather (current and outgoing weather form IDs, classifications, and transition; sky mode),
	 * cachedTime (game hour and an approximate day-of-year), cachedLocation (current location ID, interior flag,
	 * and worldspace ID when applicable), and cachedCamera (camera position and rotation) when the corresponding
	 * engine singletons and sub-objects are available.
	 */
	void Update()
	{
		// Update weather info
		if (const auto* sky = RE::Sky::GetSingleton()) {
			if (sky->currentWeather) {
				cachedWeather.currentWeatherFormID = sky->currentWeather->formID;
				cachedWeather.currentClassification = GetClassification(sky->currentWeather);
			} else {
				cachedWeather.currentWeatherFormID = 0;
				cachedWeather.currentClassification = -1;
			}

			if (sky->lastWeather) {
				cachedWeather.outgoingWeatherFormID = sky->lastWeather->formID;
				cachedWeather.outgoingClassification = GetClassification(sky->lastWeather);
			} else {
				cachedWeather.outgoingWeatherFormID = 0;
				cachedWeather.outgoingClassification = -1;
			}

			cachedWeather.weatherTransition = sky->currentWeatherPct;
			cachedTime.skyMode = sky->mode.underlying();
		}

		// Update time info
		if (const auto* calendar = RE::Calendar::GetSingleton()) {
			cachedTime.gameHour = calendar->GetHour();
			cachedTime.dayOfYear = calendar->GetDay() + (calendar->GetMonth() - 1) * 30.0f;  // Approximate (GetDayOfYear() is not available)
		}

		// Update location info
		if (const auto* player = RE::PlayerCharacter::GetSingleton()) {
			if (const auto* location = player->GetCurrentLocation()) {
				cachedLocation.locationFormID = location->formID;
			} else {
				cachedLocation.locationFormID = 0;
			}

			if (const auto* parentCell = player->GetParentCell()) {
				cachedLocation.isInterior = parentCell->IsInteriorCell();
				if (cachedLocation.isInterior) {
					cachedLocation.worldSpaceFormID = 0;
				} else if (const auto* worldSpace = player->GetWorldspace()) {
					cachedLocation.worldSpaceFormID = worldSpace->formID;
				}
			}
		}

		// Update camera info
		if (const auto* playerCamera = RE::PlayerCamera::GetSingleton()) {
			if (const auto* cameraNode = playerCamera->cameraRoot.get()) {
				if (cameraNode->world.scale != 0.0f) {
					cachedCamera.position = cameraNode->world.translate;
					cachedCamera.rotation = cameraNode->world.rotate;
				}
			}
		}
	}

	/**
	 * @brief Returns the most recently cached weather state.
	 *
	 * @return const WeatherInfo& Reference to the cached WeatherInfo containing current and outgoing weather form IDs, classifications, transition percentage, and sky mode.
	 */
	const WeatherInfo& GetWeatherInfo()
	{
		return cachedWeather;
	}

	/**
	 * @brief Gets the most recently cached player location information.
	 *
	 * The returned value reflects data last populated by ENBHelper::Update(), including the current
	 * location form ID, whether the player is in an interior cell, and the cached worldspace form ID.
	 *
	 * @return const LocationInfo& Reference to the cached LocationInfo.
	 */
	const LocationInfo& GetLocationInfo()
	{
		return cachedLocation;
	}

	/**
	 * @brief Retrieve cached game time information.
	 *
	 * Provides the most recently populated TimeInfo snapshot.
	 *
	 * @return const TimeInfo& Reference to the cached TimeInfo containing the last-updated gameHour and dayOfYear.
	 */
	const TimeInfo& GetTimeInfo()
	{
		return cachedTime;
	}

	/**
	 * @brief Retrieve the cached camera state.
	 *
	 * @return Reference to the most recently cached CameraInfo.
	 */
	const CameraInfo& GetCameraInfo()
	{
		return cachedCamera;
	}

	/**
	 * @brief Retrieve the current weather form ID from the game's sky singleton.
	 *
	 * If a current weather exists, writes its form ID to `formID`.
	 *
	 * @param[out] formID Destination for the current weather's form ID when available.
	 * @return true if the current weather was found and `formID` was set, false otherwise.
	 */
	bool GetCurrentWeather(uint32_t& formID)
	{
		if (const auto* sky = RE::Sky::GetSingleton(); sky && sky->currentWeather) {
			formID = sky->currentWeather->formID;
			return true;
		}
		return false;
	}

	/**
	 * @brief Retrieves the outgoing (previous) weather's form ID.
	 *
	 * @param formID Output variable that will be set to the outgoing weather's form ID when available.
	 * @return true if the outgoing weather was available and `formID` was set, false otherwise.
	 */
	bool GetOutgoingWeather(uint32_t& formID)
	{
		if (const auto* sky = RE::Sky::GetSingleton(); sky && sky->lastWeather) {
			formID = sky->lastWeather->formID;
			return true;
		}
		return false;
	}

	/**
	 * @brief Obtain the current weather transition percentage from the sky system.
	 *
	 * @param[out] transition Set to the current weather transition value in the range 0.0 to 1.0.
	 * @return `true` if the sky singleton was available and `transition` was written, `false` otherwise.
	 */
	bool GetWeatherTransition(float& transition)
	{
		if (const auto* sky = RE::Sky::GetSingleton()) {
			transition = sky->currentWeatherPct;
			return true;
		}
		return false;
	}

	/**
	 * @brief Retrieves the current weather classification from the sky and stores it in `classification`.
	 *
	 * The classification is an integer code representing the current weather:
	 *  - `0` = pleasant
	 *  - `1` = cloudy
	 *  - `2` = rainy
	 *  - `3` = snow
	 *  - `-1` = unknown or unrecognized weather
	 *
	 * @param[out] classification Set to the current weather classification code when the function succeeds.
	 * @return true if the current weather was available and `classification` was written, false otherwise.
	 */
	bool GetCurrentWeatherClassification(int32_t& classification)
	{
		if (const auto* sky = RE::Sky::GetSingleton(); sky && sky->currentWeather) {
			classification = GetClassification(sky->currentWeather);
			return true;
		}
		return false;
	}

	/**
	 * @brief Retrieves the outgoing (previous) weather's classification.
	 *
	 * Sets `classification` to the outgoing weather's classification code when available.
	 *
	 * @param[out] classification Integer classification: 0 = pleasant, 1 = cloudy, 2 = rainy, 3 = snow, -1 = unknown or no weather.
	 * @return true if the outgoing weather existed and `classification` was set, false otherwise.
	 */
	bool GetOutgoingWeatherClassification(int32_t& classification)
	{
		if (const auto* sky = RE::Sky::GetSingleton(); sky && sky->lastWeather) {
			classification = GetClassification(sky->lastWeather);
			return true;
		}
		return false;
	}

	/**
	 * @brief Retrieves the player's current location form ID.
	 *
	 * Sets `formID` to the form ID of the location the player is currently in when available.
	 *
	 * @param[out] formID Will be assigned the current location's form ID on success.
	 * @return true if the current location was found and `formID` was assigned, false otherwise.
	 */
	bool GetCurrentLocationID(uint32_t& formID)
	{
		if (const auto* player = RE::PlayerCharacter::GetSingleton()) {
			if (const auto* location = player->GetCurrentLocation()) {
				formID = location->formID;
				return true;
			}
		}
		return false;
	}

	/**
	 * @brief Obtains the player's current worldspace form ID or indicates interior status.
	 *
	 * Sets `formID` to the worldspace form ID when the player is in an exterior worldspace, or to 0 when the player is in an interior cell.
	 *
	 * @param[out] formID Receives the worldspace form ID, or 0 if the player is in an interior cell.
	 * @return true if a player singleton existed and `formID` was set, `false` if the player or relevant world/location data was unavailable.
	 */
	bool GetWorldSpaceID(uint32_t& formID)
	{
		if (const auto* player = RE::PlayerCharacter::GetSingleton()) {
			if (const auto* parentCell = player->GetParentCell(); parentCell && parentCell->IsInteriorCell()) {
				formID = 0;
				return true;
			}
			if (const auto* worldSpace = player->GetWorldspace()) {
				formID = worldSpace->formID;
				return true;
			}
		}
		return false;
	}

	/**
	 * @brief Retrieves the current in-game hour from the calendar.
	 *
	 * @param[out] hour Set to the current in-game hour (including fractional part) when available.
	 * @return `true` if the calendar singleton was available and `hour` was populated, `false` otherwise.
	 */
	bool GetTime(float& hour)
	{
		if (const auto* calendar = RE::Calendar::GetSingleton()) {
			hour = calendar->GetHour();
			return true;
		}
		return false;
	}

	/**
	 * @brief Retrieve the current sky mode from the game's Sky singleton.
	 *
	 * @param mode Output set to the sky's mode underlying value when available.
	 * @return true if the Sky singleton existed and `mode` was written, false otherwise.
	 */
	bool GetSkyMode(uint32_t& mode)
	{
		if (const auto* sky = RE::Sky::GetSingleton()) {
			mode = sky->mode.underlying();
			return true;
		}
		return false;
	}
}
