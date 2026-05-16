#pragma once

#include "Utils/Moon.h"

namespace Util::Sky
{
	static constexpr float SunScaleFactor = 48.0f / 2048.0f;
	static constexpr float SecundaIntensityFactor = Util::Moon::SecundaIntensityFactor;

	inline float SmoothStep(float edge0, float edge1, float x)
	{
		float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	inline float CalculateVisibility(const RE::NiPoint3& dir, float dist, float radius)
	{
		float height = dir.z * dist;
		return SmoothStep(-radius, radius, height);
	}

	struct ClimateTimings
	{
		float sunriseBegin = 0.0f;
		float sunriseEnd = 0.0f;
		float sunsetBegin = 0.0f;
		float sunsetEnd = 0.0f;
		float sunrise = 0.0f;
		float sunset = 0.0f;
		float sunriseFadeOutMoonStart = 0.0f;
		float sunriseFadeOutMoonEnd = 0.0f;
		float sunsetFadeInMoonStart = 0.0f;
		float sunsetFadeInMoonEnd = 0.0f;

		void Update(const RE::TESClimate* climate, float sunriseBeginOffset = 0.0f, float sunriseEndOffset = 0.0f, float sunsetBeginOffset = 0.0f, float sunsetEndOffset = 0.0f)
		{
			sunriseBegin = (climate->timing.sunrise.begin / 6.0f) + sunriseBeginOffset;
			sunriseEnd = (climate->timing.sunrise.end / 6.0f) + sunriseEndOffset;
			sunsetBegin = (climate->timing.sunset.begin / 6.0f) + sunsetBeginOffset;
			sunsetEnd = (climate->timing.sunset.end / 6.0f) + sunsetEndOffset;

			constexpr float kMinGapHours = 0.1f;
			if (sunriseEnd <= sunriseBegin)
				sunriseEnd = sunriseBegin + kMinGapHours;
			if (sunsetEnd <= sunsetBegin)
				sunsetEnd = sunsetBegin + kMinGapHours;
			if (sunsetBegin <= sunriseEnd)
				sunsetBegin = sunriseEnd + kMinGapHours;
			if (sunsetEnd <= sunsetBegin)
				sunsetEnd = sunsetBegin + kMinGapHours;

			sunrise = (sunriseBegin + sunriseEnd) * 0.5f - 0.25f;
			sunset = (sunsetBegin + sunsetEnd) * 0.5f + 0.25f;
			sunriseFadeOutMoonStart = sunriseBegin - 0.5f;
			sunriseFadeOutMoonEnd = sunriseBegin + 1.0f;
			sunsetFadeInMoonStart = sunsetEnd - 1.0f;
			sunsetFadeInMoonEnd = sunsetEnd + 0.5f;
		}

	};
}
