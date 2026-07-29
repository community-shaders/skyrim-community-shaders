#pragma once

// ENB Helper functionality
// Based on ENBHelperSE by aers (https://github.com/xSyphel/ENBHelperSE)
// Original code licensed under MIT License - Copyright (c) 2021 aers

#include <cstdint>

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

	// Update all cached values - call once per frame
	void Update();
}
