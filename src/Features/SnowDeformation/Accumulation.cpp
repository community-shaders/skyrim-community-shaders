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
	const float growth = intensity / kAccumulationHours;
	const float melt = (1.0f - intensity) / kAccumulationMeltHours;
	constexpr float fade = 1.0f / (kAccumulationFadeDays * 24.0f);

	const float current = snowAccumulation.load(std::memory_order_relaxed);
	snowAccumulation.store(std::clamp(current + elapsed * (growth - melt - fade), 0.0f, 1.0f),
		std::memory_order_relaxed);
}

float SnowDeformation::GetAccumulationDepthScale() const
{
	if (!settings.EnableSnowAccumulation)
		return 1.0f;

	return 1.0f + snowAccumulation.load(std::memory_order_relaxed) * (kAccumulationPeak - 1.0f);
}
