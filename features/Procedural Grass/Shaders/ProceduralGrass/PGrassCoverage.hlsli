#ifndef __PGRASS_COVERAGE_HLSLI__
#define __PGRASS_COVERAGE_HLSLI__

// Grass-map edge coverage for blade generation.

// The grass map is a grid of LAND vertices, so its edges and bare cells follow grid lines. A smooth world-space warp bends
// them and a smaller per-blade jitter softens them; together they stay within the edge noise distance tile tests pad by.
static const float GrassMapWarpCell = 256.0f;
static const float GrassMapWarpShare = 0.7f;

float2 GetGrassMapWarpCorner(int2 cell)
{
	return float2(Random::pcg2d(asuint(cell) ^ uint2(0x51ED270Bu, 0x2545F491u))) * (2.0f / 4294967296.0f) - 1.0f;
}

/** @brief Returns a smooth, stable displacement in [-1, 1] per axis that varies over a few grass-map cells. */
float2 GetGrassMapWarp(float2 worldPos2D)
{
	float2 cellPos = worldPos2D * (1.0f / GrassMapWarpCell);
	int2 cell = (int2)floor(cellPos);
	float2 blend = cellPos - float2(cell);
	blend = blend * blend * (3.0f - 2.0f * blend);
	float2 bottom = lerp(GetGrassMapWarpCorner(cell), GetGrassMapWarpCorner(cell + int2(1, 0)), blend.x);
	float2 top = lerp(GetGrassMapWarpCorner(cell + int2(0, 1)), GetGrassMapWarpCorner(cell + int2(1, 1)), blend.x);
	return lerp(bottom, top, blend.y);
}

/**
 * @brief Fades blade density across cells touching bare LAND. World-space noise varies the boundary, while the
 * per-blade dither thins it gradually. Filled cells stay fully covered so the fade cannot reopen interior holes.
 */
float GetGrassMapEdgeCoverage(float2 bladeWorldPos2D, float presence)
{
	float broad = Random::ValueNoise2D(bladeWorldPos2D * (1.0f / 96.0f)) - 0.5f;
	float fine = Random::ValueNoise2D(bladeWorldPos2D * (1.0f / 40.0f) + 17.0f) - 0.5f;
	float fadeEnd = clamp(0.8f + broad * 0.4f + fine * 0.2f, 0.5f, 1.0f);
	return smoothstep(0.0f, fadeEnd, presence);
}

#endif
