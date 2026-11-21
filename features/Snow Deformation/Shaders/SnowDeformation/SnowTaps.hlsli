#ifndef SNOW_DEFORMATION_SNOWTAPS_HLSLI
#define SNOW_DEFORMATION_SNOWTAPS_HLSLI

float2 StochasticHash(float2 cell)
{
	float3 p3 = frac(float3(cell.x, cell.y, cell.x) * float3(0.1031, 0.1030, 0.0973));
	p3 += dot(p3, p3.yzx + 33.33);
	return frac(float2((p3.x + p3.y) * p3.z, (p3.x + p3.z) * p3.y));
}

struct SnowTaps
{
	float2 uv0, uv1, uv2;
	float3 weights;
	float2 duvdx, duvdy;
};

// Fills the uvs and weights of SnowTaps taps for uv at worldXY; the gradients stay the caller's.
// A macro, not a function: Lighting compiles to the same code as with the taps written inline.
#define SNOW_TAPS_FILL(taps, uv, worldXY)                                                                        \
	{                                                                                                            \
		float2 lattice = mul(float2x2(1.0, -0.57735027, 0.0, 1.15470054), (worldXY) * (0.6 / 256.0));            \
		float2 cellBase = floor(lattice);                                                                        \
		float2 f = frac(lattice);                                                                                \
		bool upper = f.x + f.y >= 1.0;                                                                           \
		float2 v0 = cellBase + (upper ? float2(1.0, 1.0) : float2(0.0, 0.0));                                    \
		float2 v1 = cellBase + (upper ? float2(0.0, 1.0) : float2(1.0, 0.0));                                    \
		float2 v2 = cellBase + (upper ? float2(1.0, 0.0) : float2(0.0, 1.0));                                    \
		float3 bary = upper ? float3(f.x + f.y - 1.0, 1.0 - f.x, 1.0 - f.y) : float3(1.0 - f.x - f.y, f.x, f.y); \
		bary = pow(bary, 4.0);                                                                                   \
		bary /= dot(bary, 1.0);                                                                                  \
		taps.weights = bary;                                                                                     \
		taps.uv0 = (uv) + StochasticHash(v0);                                                                    \
		taps.uv1 = (uv) + StochasticHash(v1);                                                                    \
		taps.uv2 = (uv) + StochasticHash(v2);                                                                    \
	}

SnowTaps ComputeSnowTapsNoGrad(float2 uv, float2 worldXY)
{
	SnowTaps taps;
	taps.duvdx = 0.0.xx;
	taps.duvdy = 0.0.xx;
	SNOW_TAPS_FILL(taps, uv, worldXY);
	return taps;
}

#endif
