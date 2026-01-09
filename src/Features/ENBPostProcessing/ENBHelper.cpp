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
			cachedTime.dayOfYear = calendar->GetDayOfYear();
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

	const WeatherInfo& GetWeatherInfo()
	{
		return cachedWeather;
	}

	const LocationInfo& GetLocationInfo()
	{
		return cachedLocation;
	}

	const TimeInfo& GetTimeInfo()
	{
		return cachedTime;
	}

	const CameraInfo& GetCameraInfo()
	{
		return cachedCamera;
	}

	bool GetCurrentWeather(uint32_t& formID)
	{
		if (const auto* sky = RE::Sky::GetSingleton(); sky && sky->currentWeather) {
			formID = sky->currentWeather->formID;
			return true;
		}
		return false;
	}

	bool GetOutgoingWeather(uint32_t& formID)
	{
		if (const auto* sky = RE::Sky::GetSingleton(); sky && sky->lastWeather) {
			formID = sky->lastWeather->formID;
			return true;
		}
		return false;
	}

	bool GetWeatherTransition(float& transition)
	{
		if (const auto* sky = RE::Sky::GetSingleton()) {
			transition = sky->currentWeatherPct;
			return true;
		}
		return false;
	}

	bool GetCurrentWeatherClassification(int32_t& classification)
	{
		if (const auto* sky = RE::Sky::GetSingleton(); sky && sky->currentWeather) {
			classification = GetClassification(sky->currentWeather);
			return true;
		}
		return false;
	}

	bool GetOutgoingWeatherClassification(int32_t& classification)
	{
		if (const auto* sky = RE::Sky::GetSingleton(); sky && sky->lastWeather) {
			classification = GetClassification(sky->lastWeather);
			return true;
		}
		return false;
	}

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

	bool GetTime(float& hour)
	{
		if (const auto* calendar = RE::Calendar::GetSingleton()) {
			hour = calendar->GetHour();
			return true;
		}
		return false;
	}

	bool GetSkyMode(uint32_t& mode)
	{
		if (const auto* sky = RE::Sky::GetSingleton()) {
			mode = sky->mode.underlying();
			return true;
		}
		return false;
	}
}
