#include "Climate.h"

#include "Globals.h"

namespace
{
	constexpr float VanillaDaytimeColorExtension = 0.5f;
	/// The sky's cap on the extended sunset end, keeping the colour window inside the day.
	constexpr float LatestNightStart = 23.99f;

	/** @brief A game setting's float, or the vanilla value when the setting is missing. */
	float GetSettingFloat(RE::Setting* setting, float vanillaValue)
	{
		return setting ? setting->GetFloat() : vanillaValue;
	}

	/** @brief A game setting by name, or null before the collection exists. */
	RE::Setting* FindSetting(const char* name)
	{
		const auto collection = globals::game::gameSettingCollection;
		return collection ? collection->GetSetting(name) : nullptr;
	}
}

namespace Util::Climate
{
	DayHours GetDayHours(const RE::TESClimate& climate)
	{
		// Retried until found: a call before the game settings exist would otherwise pin the vanilla value.
		static RE::Setting* daytimeColorExtension = nullptr;
		if (!daytimeColorExtension)
			daytimeColorExtension = FindSetting("fDaytimeColorExtension");

		const auto& timing = climate.timing;
		const float colorExtension = GetSettingFloat(daytimeColorExtension, VanillaDaytimeColorExtension);
		const float nightEnd = std::max(timing.sunrise.begin * HoursPerTimingUnit - colorExtension, 0.0f);
		const float dayStart = timing.sunrise.end * HoursPerTimingUnit;
		const float dayEnd = timing.sunset.begin * HoursPerTimingUnit;
		const float nightStart = std::min(timing.sunset.end * HoursPerTimingUnit + colorExtension, LatestNightStart);
		return {
			.nightEnd = nightEnd,
			.sunrisePeak = (nightEnd + dayStart) * 0.5f,
			.dayStart = dayStart,
			.dayEnd = dayEnd,
			.sunsetPeak = (dayEnd + nightStart) * 0.5f,
			.nightStart = nightStart
		};
	}

	ColorTimeWeights GetColorTimeWeights(const RE::TESClimate& climate, float hour)
	{
		using ColorTime = RE::TESWeather::ColorTime;
		const auto day = GetDayHours(climate);
		ColorTimeWeights weights{};
		// A transition window peaks its colour time at the middle, sharing the rest with the side the hour is on.
		auto blendWindow = [&](float start, float middle, ColorTime before, ColorTime peak, ColorTime after) {
			weights[peak] = 1.0f - std::abs(hour - middle) / (middle - start);
			weights[hour >= middle ? after : before] = 1.0f - weights[peak];
		};

		// Window bounds are exclusive, so a window's half length is never zero.
		if (hour > day.nightEnd && hour < day.dayStart)
			blendWindow(day.nightEnd, day.sunrisePeak, ColorTime::kNight, ColorTime::kSunrise, ColorTime::kDay);
		else if (hour >= day.dayStart && hour <= day.dayEnd)
			weights[ColorTime::kDay] = 1.0f;
		else if (hour > day.dayEnd && hour < day.nightStart)
			blendWindow(day.dayEnd, day.sunsetPeak, ColorTime::kDay, ColorTime::kSunset, ColorTime::kNight);
		else
			weights[ColorTime::kNight] = 1.0f;
		return weights;
	}

	RE::Calendar* GetCalendar()
	{
		return globals::game::calendar ? globals::game::calendar : RE::Calendar::GetSingleton();
	}
}
