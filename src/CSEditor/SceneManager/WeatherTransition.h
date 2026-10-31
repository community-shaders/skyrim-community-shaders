#pragma once

/// Eases the sky's weather blend and scales its length, run around Sky::UpdateWeather.
/// The engine rebuilds currentWeatherPct from the clock every frame, so the eased value never feeds back.
namespace WeatherTransition
{
	/** @brief Puts back the raw blend the engine expects and applies a changed duration scale. */
	void BeforeUpdate(RE::Sky* sky);

	/** @brief Remembers the engine's raw blend and replaces it with the eased one. */
	void AfterUpdate(RE::Sky* sky);
}
