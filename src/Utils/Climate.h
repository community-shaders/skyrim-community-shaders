// Shared climate timing utilities
#pragma once

namespace Util::Climate
{
	/** @brief Hours per TNAM timing unit: the record stores times in 10-minute steps. */
	inline constexpr float HoursPerTimingUnit = 1.0f / 6.0f;

	/** @brief Key hours of a climate's sky colour transitions, derived the way the game's sky derives them. */
	struct DayHours
	{
		float nightEnd;     // Sky colours start leaving night: sunrise begin - fDaytimeColorExtension
		float sunrisePeak;  // Sunrise colours at full weight, mid-window
		float dayStart;     // Sky colours fully day: sunrise end
		float dayEnd;       // Sky colours start leaving day: sunset begin
		float sunsetPeak;   // Sunset colours at full weight, mid-window
		float nightStart;   // Sky colours fully night again: sunset end + fDaytimeColorExtension
	};

	/** @brief Per-ColorTime weights, indexed by RE::TESWeather::ColorTime. */
	using ColorTimeWeights = std::array<float, RE::TESWeather::ColorTime::kTotal>;

	/** @brief Middle of a sunrise or sunset interval in hours, matching Sun::Update's float ops. */
	inline float GetMiddleHour(const RE::TESClimate::Timing::Interval& interval)
	{
		return (interval.end * HoursPerTimingUnit + interval.begin * HoursPerTimingUnit) * 0.5f;
	}

	/** @brief The game calendar; the editor can draw before globals are cached, so the singleton is the fallback. */
	RE::Calendar* GetCalendar();

	/** @brief Key hours of a climate's day; falls back to the vanilla game setting before it is loaded. */
	DayHours GetDayHours(const RE::TESClimate& climate);

	/**
	 * @brief Weights the sky blends a weather's colour times with at an hour, mirroring its colour update.
	 * @return Sunrise and Sunset peak mid-window; weights sum to 1.
	 */
	ColorTimeWeights GetColorTimeWeights(const RE::TESClimate& climate, float hour);
}
