#include "Features/SnowDeformation.h"

#include "Globals.h"
#include "State.h"

#include <numbers>

void SnowDeformation::TickAccumulation()
{
	if (weatherClock.reversed)
		return;

	const float elapsed = weatherClock.elapsedHours;
	if (elapsed <= 0.0f)
		return;

	auto* tes = globals::game::tes;
	if (!tes || (!tes->interiorCell && !NoSkyWorldspace()))
		accumWeatherIntensity.store(std::clamp(snowfallIntensity, 0.0f, 1.0f), std::memory_order_relaxed);

	const float intensity = accumWeatherIntensity.load(std::memory_order_relaxed);
	const float growth = settings.AccumulationHours > 0.01f ?
	                         intensity / settings.AccumulationHours :
	                         0.0f;
	const float melt = settings.AccumulationMeltHours > 0.01f ?
	                       (1.0f - intensity) / settings.AccumulationMeltHours :
	                       0.0f;
	const float fade = settings.AccumulationFadeDays > 0.01f ?
	                       1.0f / (settings.AccumulationFadeDays * 24.0f) :
	                       0.0f;

	const float current = snowAccumulation.load(std::memory_order_relaxed);
	snowAccumulation.store(std::clamp(current + elapsed * (growth - melt - fade), 0.0f, 1.0f),
		std::memory_order_relaxed);
}

float SnowDeformation::GetAccumulationDepthScale() const
{
	if (!settings.EnableSnowAccumulation)
		return 1.0f;

	const float peak = std::max(settings.AccumulationPeak, 1.0f);
	return 1.0f + snowAccumulation.load(std::memory_order_relaxed) * (peak - 1.0f);
}

float SnowDeformation::GetSnowSoftness() const
{
	return 0.0f;
}

void SnowDeformation::TickSnowClimate()
{
	snowClimate.ageingRate = 1.0f;
}

void SnowDeformation::TickSnowAgeing()
{
	auto& climate = snowClimate;
	if (gameClock.reversed)
		return;
	const float elapsed = gameClock.elapsedHours;
	if (elapsed <= 0.0f)
		return;
	climate.ageingClock += climate.ageingRate * elapsed;
	climate.ageingTotal += (double)climate.ageingRate * elapsed;
	while (climate.ageingClock >= 2.0f * kAgeingClockStart) {
		climate.ageingClock -= kAgeingClockStart;
		climate.ageingRebase += kAgeingClockStart;
	}
}

float SnowDeformation::SnowIceShown(uint32_t) const
{
	return 0.0f;
}

float SnowDeformation::SnowIceSinceCut(uint32_t, uint32_t) const
{
	return 0.0f;
}

float SnowDeformation::SnowWetIceStrength() const
{
	return 0.0f;
}

float4 SnowDeformation::TrackAgeConstants() const
{
	return { snowClimate.ageingClock, 0.0f, 0.0f, 0.0f };
}

SnowDeformation::SnowCondition SnowDeformation::GetSnowCondition() const
{
	return {};
}

SnowDeformation::SnowClimateLook SnowDeformation::ComputeSnowClimateLook() const
{
	return {};
}
