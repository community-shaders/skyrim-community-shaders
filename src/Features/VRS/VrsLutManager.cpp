#include "VrsLutManager.h"

#include "VrsSrsBuilder.h"
#include <algorithm>

void VrsLutManager::FillActiveRateTable(NV_PIXEL_SHADING_RATE* table, uint32_t count, uint32_t lutPreset)
{
	std::fill(table, table + count, NV_PIXEL_X1_PER_RASTER_PIXEL);

	switch (lutPreset) {
	case 1:  // Full 1×1: force native rate (debug)
		break;
	case 2:  // Full 4×4: force coarsest rate (debug)
		std::fill(table, table + count, NV_PIXEL_X1_PER_4X4_RASTER_PIXELS);
		if (count > SrsLevel::kCull)
			table[SrsLevel::kCull] = NV_PIXEL_X0_CULL_RASTER_PIXELS;
		break;
	default:  // Default: 1:1 mapping preserving directional asymmetry
		if (count > SrsLevel::k1x1)
			table[SrsLevel::k1x1] = NV_PIXEL_X1_PER_RASTER_PIXEL;
		if (count > SrsLevel::k2x1)
			table[SrsLevel::k2x1] = NV_PIXEL_X1_PER_2X1_RASTER_PIXELS;
		if (count > SrsLevel::k1x2)
			table[SrsLevel::k1x2] = NV_PIXEL_X1_PER_1X2_RASTER_PIXELS;
		if (count > SrsLevel::k2x2)
			table[SrsLevel::k2x2] = NV_PIXEL_X1_PER_2X2_RASTER_PIXELS;
		if (count > SrsLevel::k4x2)
			table[SrsLevel::k4x2] = NV_PIXEL_X1_PER_4X2_RASTER_PIXELS;
		if (count > SrsLevel::k2x4)
			table[SrsLevel::k2x4] = NV_PIXEL_X1_PER_2X4_RASTER_PIXELS;
		if (count > SrsLevel::k4x4)
			table[SrsLevel::k4x4] = NV_PIXEL_X1_PER_4X4_RASTER_PIXELS;
		if (count > SrsLevel::kCull)
			table[SrsLevel::kCull] = NV_PIXEL_X0_CULL_RASTER_PIXELS;
		break;
	}
}

void VrsLutManager::FillDisabledRateTable(NV_PIXEL_SHADING_RATE* table, uint32_t count)
{
	std::fill(table, table + count, NV_PIXEL_X1_PER_RASTER_PIXEL);
}
