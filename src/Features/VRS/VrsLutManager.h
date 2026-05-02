#pragma once

#include <nvapi.h>

/// LUT manager for NVAPI per-viewport shading rates.
/// Default preset: 1:1 mapping preserving directional asymmetry.
/// Debug presets override all entries to a uniform rate.
class VrsLutManager
{
public:
	static void FillActiveRateTable(NV_PIXEL_SHADING_RATE* table, uint32_t count, uint32_t lutPreset = 0);
	static void FillDisabledRateTable(NV_PIXEL_SHADING_RATE* table, uint32_t count);
};
