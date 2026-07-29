#include "ENBHelper.h"

#include "Globals.h"

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
		if (const auto* sky = globals::game::sky) {
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
		} else {
			cachedWeather.currentWeatherFormID = 0;
			cachedWeather.currentClassification = -1;
			cachedWeather.outgoingWeatherFormID = 0;
			cachedWeather.outgoingClassification = -1;
			cachedWeather.weatherTransition = 0.0f;
			cachedTime.skyMode = 0;
		}

		if (const auto* calendar = globals::game::calendar) {
			cachedTime.gameHour = calendar->GetHour();
			cachedTime.dayOfYear = calendar->GetDay() + (calendar->GetMonth() - 1) * 30.0f;
		} else {
			cachedTime.gameHour = 12.0f;
			cachedTime.dayOfYear = 0.0f;
		}

		if (const auto* player = globals::game::player) {
			if (const auto* location = player->GetCurrentLocation()) {
				cachedLocation.locationFormID = location->formID;
			} else {
				cachedLocation.locationFormID = 0;
			}

			if (const auto* parentCell = player->GetParentCell()) {
				cachedLocation.isInterior = parentCell->IsInteriorCell();
				if (!cachedLocation.isInterior) {
					if (const auto* worldSpace = player->GetWorldspace()) {
						cachedLocation.worldSpaceFormID = worldSpace->formID;
					} else {
						cachedLocation.worldSpaceFormID = 0;
					}
				} else {
					cachedLocation.worldSpaceFormID = 0;
				}
			} else {
				cachedLocation.isInterior = false;
				cachedLocation.worldSpaceFormID = 0;
			}
		} else {
			cachedLocation.locationFormID = 0;
			cachedLocation.isInterior = false;
			cachedLocation.worldSpaceFormID = 0;
		}
	}

}
