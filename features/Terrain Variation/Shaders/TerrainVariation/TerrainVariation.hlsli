// Implements stochastic noise sampling for terrain textures to reduce tiling artifacts and improve visual quality.
// Based on paper "Procedural Stochastic Textures by Tiling and Blending" by Thomas Deliot & Eric Heitz.
// https://eheitzresearch.wordpress.com/722-2/

#ifndef TERRAIN_VARIATION_HLSLI
#define TERRAIN_VARIATION_HLSLI

#include "Common/SharedData.hlsli"

// --------------------- CONSTANTS AND STRUCTURES --------------------- //
static const float HEIGHT_INFLUENCE = 0.3;  // How much height affects blending (0=pure stochastic, 1=pure height)
static const float2x2 SKEW_MATRIX = float2x2(1.0, 0.0, -0.57735027, 1.15470054);
static const float WORLD_SCALE = 332.54;
static const float3 LUMINANCE_WEIGHTS = float3(0.2126, 0.7152, 0.0722);
static const float2 HASH_MULTIPLIER = float2(1271.5151, 3337.8237);
// Fade width for c1/c2 rank-swap seams when discarding the third triangle corner.
static const float TAP_SWAP_FADE_WIDTH = 0.1;
// Disables the rank-swap fade near c0/c1 ties so the primary swap stays symmetric.
static const float DOMINANCE_GUARD_WIDTH = 0.02;
static const float STOCHASTIC_LOD_PHI = 1.618;
static const float STOCHASTIC_LOD_BLEND = 0.65;

// Structure to hold stochastic sampling offsets and tap weights
struct StochasticOffsets
{
	float2 offset1;
	float2 offset2;
	// Normalized weight of tap 1 in [0.5, 1]; tap 2 gets the remainder.
	float tap1Weight;
};

// Shared LOD base for the six-way blend (set once per pixel).
static float g_terrainStochasticLodBase;

struct StochasticCorner
{
	float2 cell;
	float w;
};

// --------------------- COMPUTE FUNCTIONS --------------------- //

// Hash function for stochastic sampling
inline float2 hash2D2D(float2 s)
{
	s = frac(s * HASH_MULTIPLIER);
	s += dot(s, s.yx + 19.19);
	return frac((s.xx + s.yy) * s.yx);
}

inline float2 hashLOD(float2 p)
{
	p = frac(p * 0.318);
	return frac(float2(dot(p, float2(1.0, 17.0)), dot(p, float2(1.0, 23.0))));
}

// LOD base from original UV derivatives + MipBias.
// Hashed offsets break implicit derivatives, so SampleLevel needs an explicit mip.
inline float ComputeTerrainStochasticLodBase(float2 uv)
{
	float2 dx = ddx(uv);
	float2 dy = ddy(uv);
	float minDeltaSq = min(dot(dx, dx), dot(dy, dy));
	return 0.5 * log2(max(minDeltaSq, 1e-12)) + SharedData::MipBias;
}

// Per-texture mip; square landscape maps only shift the shared LOD by log2(size).
inline float TerrainStochasticMipLevel(Texture2D tex)
{
	float2 textureDims;
	tex.GetDimensions(textureDims.x, textureDims.y);
	return max(g_terrainStochasticLodBase + log2(max(textureDims.x, textureDims.y)), 0.0);
}

// Two highest barycentric corners of the triangular grid.
// Near c1/c2 ties, fade w2 so the discarded corner cannot pop;
// near c0/c1 ties, disable that fade so the primary swap stays symmetric.
// Contrast exponent is 2, so weights are squared rather than pow()'d.
inline StochasticOffsets ComputeStochasticOffsets(float2 landscapeUV)
{
	float2 skewUV = mul(SKEW_MATRIX, landscapeUV * WORLD_SCALE);
	float2 vxID = floor(skewUV);
	float2 f = frac(skewUV);
	float bz = 1.0 - f.x - f.y;

	StochasticCorner c0, c1, c2;
	if (bz > 0) {
		c0.cell = vxID;
		c0.w = bz;
		c1.cell = vxID + float2(0, 1);
		c1.w = f.y;
		c2.cell = vxID + float2(1, 0);
		c2.w = f.x;
	} else {
		c0.cell = vxID + 1.0;
		c0.w = -bz;
		c1.cell = vxID + float2(1, 0);
		c1.w = 1.0 - f.y;
		c2.cell = vxID + float2(0, 1);
		c2.w = 1.0 - f.x;
	}

	if (c1.w > c0.w) {
		StochasticCorner t = c0;
		c0 = c1;
		c1 = t;
	}
	if (c2.w > c0.w) {
		StochasticCorner t = c0;
		c0 = c2;
		c2 = t;
	}
	if (c2.w > c1.w) {
		StochasticCorner t = c1;
		c1 = c2;
		c2 = t;
	}

	float w1 = saturate(c0.w);
	w1 *= w1;
	float w2 = saturate(c1.w);
	w2 *= w2;
	float rankFade = smoothstep(0.0, TAP_SWAP_FADE_WIDTH, c1.w - c2.w);
	float dominanceGuard = smoothstep(0.0, DOMINANCE_GUARD_WIDTH, c0.w - c1.w);
	w2 *= lerp(1.0, rankFade, dominanceGuard);

	StochasticOffsets o;
	o.offset1 = hash2D2D(c0.cell);
	o.offset2 = hash2D2D(c1.cell);
	o.tap1Weight = w1 * rcp(max(w1 + w2, 1e-8));
	return o;
}

// --------------------- STOCHASTIC SAMPLING FUNCTIONS --------------------- //

// Height-aware blend of two stochastic taps. At tap1Weight == 1 returns s1.
inline float4 StochasticBlendTwoSamples(float4 s1, float4 s2, float tap1Weight, float blendFactor1, float blendFactor2)
{
	float w1 = tap1Weight * (1.0 + HEIGHT_INFLUENCE * blendFactor1);
	float w2 = (1.0 - tap1Weight) * (1.0 + HEIGHT_INFLUENCE * blendFactor2);
	return lerp(s2, s1, w1 * rcp(max(w1 + w2, 1e-8)));
}

// Stochastic sampling function for Terrain LOD & LOD Mask.
inline float4 StochasticSampleLOD(float rnd, Texture2D tex, SamplerState samp, float2 uv)
{
	float2 cellID = floor(uv * 255437.0);
	float2 offset1 = hashLOD(cellID) * 0.08;
	float2 offset2 = hashLOD(cellID + 127.0) * 0.08;

	// Cheap pseudo-rotation using simple transforms
	float2 jitter = float2(rnd - 0.5, frac(rnd * STOCHASTIC_LOD_PHI) - 0.5);
	float2 j1 = (offset1 + jitter) * 0.01;
	float2 j2 = (offset2 + float2(jitter.y, -jitter.x)) * 0.01;
	float4 s1 = tex.SampleBias(samp, uv + j1, SharedData::MipBias);
	float4 s2 = tex.SampleBias(samp, uv + j2, SharedData::MipBias);

	// Simple 2-sample blend weighted toward first sample
	return lerp(s2, s1, STOCHASTIC_LOD_BLEND);
}

// Main stochastic sampling function
inline float4 StochasticEffect(Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsets)
{
	// Calculate custom mip level from original UVs.
	float mipLevel = TerrainStochasticMipLevel(tex);

	float4 s1 = tex.SampleLevel(samp, uv + offsets.offset1, mipLevel);
	float4 s2 = tex.SampleLevel(samp, uv + offsets.offset2, mipLevel);

	// Height calculation - use luminance for RGB data, alpha when available
	float h1 = lerp(dot(s1.rgb, LUMINANCE_WEIGHTS), s1.a, step(0.001, s1.a));
	float h2 = lerp(dot(s2.rgb, LUMINANCE_WEIGHTS), s2.a, step(0.001, s2.a));
	return StochasticBlendTwoSamples(s1, s2, offsets.tap1Weight, h1, h2);
}

// Stochastic sampling for parallax/height; same offsets as albedo so height stays aligned.
inline float4 StochasticEffectParallax(Texture2D tex, SamplerState samp, float2 uv, float mipLevel, StochasticOffsets offsets)
{
	float4 s1 = tex.SampleLevel(samp, uv + offsets.offset1, mipLevel);
	float4 s2 = tex.SampleLevel(samp, uv + offsets.offset2, mipLevel);
	return StochasticBlendTwoSamples(s1, s2, offsets.tap1Weight, s1.a, s2.a);
}

#endif  // TERRAIN_VARIATION_HLSLI
