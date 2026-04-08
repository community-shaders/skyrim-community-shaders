#pragma once

#include <RE/T/TESForm.h>

/// Per-weather scene settings panel.
/// Displayed as a tab in the WeatherWidget, combining flat overrides
/// (InteriorOnly style) with an optional Time-of-Day table.
namespace WeatherScenePanel
{
	void Draw(RE::FormID weatherId);
}
