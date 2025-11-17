
#pragma once

inline float getDayInYear()
{
	auto calendar = RE::Calendar::GetSingleton();
	if (calendar)
		return (calendar->GetMonth() - 1) * 30 + (calendar->GetDay() - 1) + calendar->GetHour() / 24.f;
	return 0;
}

// TODO: It seems sunrise/sunset begin is a fixed angle, need more RE
inline float getVanillaSunLerpFactor()
{
	auto sky = RE::Sky::GetSingleton();
	auto calendar = RE::Calendar::GetSingleton();
	if (sky && calendar) {
		auto game_hour = calendar->GetHour();

		RE::NiPoint3 sun_dir;

		auto climate = sky->currentClimate;
		if (climate) {
			float sunrise = (climate->timing.sunrise.GetEndTime().tm_hour + climate->timing.sunrise.GetBeginTime().tm_hour) * .5f;
			float sunset = (climate->timing.sunset.GetEndTime().tm_hour + climate->timing.sunset.GetBeginTime().tm_hour) * .5f;
			bool is_daytime = (game_hour > sunrise) && (game_hour < sunset);
			if (is_daytime) {
				float lerp_factor = (game_hour - sunrise) / (sunset - sunrise);
				return std::lerp(.25f, .75f, lerp_factor);
			} else {
				float lerp_factor = game_hour - sunset;
				lerp_factor += 24.f * (lerp_factor < 0);
				lerp_factor /= 24 + sunrise - sunset;
				float t = std::lerp(.75f, 1.25f, lerp_factor);
				t -= t > 1;
				return t;
			}
		}
	}

	return 0;
}