#ifndef __SNOW_NOISE_DEPENDENCY_HLSL__
#define __SNOW_NOISE_DEPENDENCY_HLSL__

float ShapeNoiseHash(float2 cell)
{
	float3 p3 = frac(float3(cell.x, cell.y, cell.x) * float3(0.1031, 0.1030, 0.0973));
	p3 += dot(p3, p3.yzx + 33.33);
	return frac((p3.x + p3.y) * p3.z);
}

float ShapeNoise(float2 p)
{
	float2 i = floor(p);
	float2 f = frac(p);
	f = f * f * (3.0 - 2.0 * f);
	return lerp(lerp(ShapeNoiseHash(i), ShapeNoiseHash(i + float2(1, 0)), f.x),
		lerp(ShapeNoiseHash(i + float2(0, 1)), ShapeNoiseHash(i + float2(1, 1)), f.x), f.y);
}

static const float kNoiseTorusCells = 256.0;

float ShapeNoiseTorus(float2 p, float a_cells)
{
	float2 i = floor(p);
	float2 f = frac(p);
	f = f * f * (3.0 - 2.0 * f);
	float2 c0 = i - a_cells * floor(i / a_cells);
	float2 c1 = c0 + 1.0;
	c1 -= a_cells * floor(c1 / a_cells);
	return lerp(lerp(ShapeNoiseHash(c0), ShapeNoiseHash(float2(c1.x, c0.y)), f.x),
		lerp(ShapeNoiseHash(float2(c0.x, c1.y)), ShapeNoiseHash(c1), f.x), f.y);
}

float UndulationNorm(float2 worldXY, float scale)
{
	float2 p = worldXY / max(scale, 0.05);
	float n = ShapeNoise(p / 340.0) * 0.72 + ShapeNoise(p / 110.0) * 0.28;
	return (n - 0.5) * 2.0;
}

static const float kUndulationGradStep = 6.0;

#endif
