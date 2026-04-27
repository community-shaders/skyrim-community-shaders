// Implements stochastic noise sampling for terrain textures to reduce tiling artifacts and improve visual quality.
// Based on paper "Procedural Stochastic Textures by Tiling and Blending" by Thomas Deliot & Eric Heitz.
// https://eheitzresearch.wordpress.com/722-2/

#ifndef TERRAIN_VARIATION_HLSLI
#define TERRAIN_VARIATION_HLSLI

#include "Common/SharedData.hlsli"

// --------------------- CONSTANTS --------------------- //
static const float2x2 SKEW_MATRIX = float2x2(1.0, 0.0, -0.57735027, 1.15470054);
static const float WORLD_SCALE = 332.54;
static const float2 HASH_MULTIPLIER = float2(1271.5151, 3337.8237);
static const float HEIGHT_BLEND_CONTRAST = 12.0;
static const float HEIGHT_INFLUENCE = 0.3;
static const float CONTRAST_FACTOR = HEIGHT_BLEND_CONTRAST * (1.0 - HEIGHT_INFLUENCE);
static const float3 LUMINANCE_WEIGHTS = float3(0.2126, 0.7152, 0.0722);
static const float HEIGHT_BLEND_FADE_MIP_START = 1.6;
static const float HEIGHT_BLEND_FADE_MIP_RANGE = 2.2;
// TV distant/minified fallback: bypass stochastic blend and use one sample.
static const float TV_SINGLE_SAMPLE_MIP_START = 2.9;
static const float TV_SINGLE_SAMPLE_PARALLAX_MIP_START = 2.6;
// Golden ratio for frac(rnd * φ) low-discrepancy jitter; precompute once per pixel at callsite when possible.
static const float STOCHASTIC_LOD_PHI = 1.618;

// --------------------- STRUCTURES --------------------- //
struct StochasticOffsets
{
	float2 offset1;
	float2 offset2;
	float2 offset3;
	float3 weights;
};

// Triangle corner for barycentric sort: pack cell id + weight so each swap updates both.
struct StochasticCorner
{
	float2 cell;
	float w;
};

// --------------------- HASH FUNCTIONS --------------------- //
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

// --------------------- COMPUTE FUNCTIONS --------------------- //
inline StochasticOffsets ComputeStochasticOffsets(float2 landscapeUV)
{
	if (!SharedData::terrainVariationSettings.enableTilingFix)
		return (StochasticOffsets)0;

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

	// Sort by weight descending (3-comparator network). Only c0/c1 are hashed; weights.xy must be the two largest.
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

	StochasticOffsets o;
	o.offset1 = hash2D2D(c0.cell);
	o.offset2 = hash2D2D(c1.cell);
	o.offset3 = 0;
	o.weights = float3(c0.w, c1.w, c2.w);
	return o;
}

inline StochasticOffsets ComputeStochasticOffsetsLOD(float2 landscapeUV)
{
	if (!SharedData::terrainVariationSettings.enableLODTerrainTilingFix)
		return (StochasticOffsets)0;

	float2 cellID = floor(landscapeUV * 255437.0);
	float2 h1 = hashLOD(cellID);
	float2 h2 = hashLOD(cellID + 127.0);

	StochasticOffsets o;
	o.offset1 = h1 * 0.08;
	o.offset2 = h2 * 0.08;
	o.offset3 = 0;
	o.weights = float3(0.65, 0.35, 0.0);
	return o;
}

// --------------------- SAMPLING FUNCTIONS --------------------- //

inline float2 StochasticSampleLODJitter(float rnd)
{
	return float2(rnd - 0.5, frac(rnd * STOCHASTIC_LOD_PHI) - 0.5);
}

inline float StochasticHeightFadeFromMip(float mipLevel)
{
	return saturate((mipLevel - HEIGHT_BLEND_FADE_MIP_START) / HEIGHT_BLEND_FADE_MIP_RANGE);
}

// LOD terrain stochastic sampling — 2 SampleBias, fixed blend (pass jitter from StochasticSampleLODJitter(screenNoise)).
inline float4 StochasticSampleLOD(float2 jitter, Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsetsLOD)
{
	if (!SharedData::terrainVariationSettings.enableLODTerrainTilingFix)
		return tex.SampleBias(samp, uv, SharedData::MipBias);

	float4 s1 = tex.SampleBias(samp, uv + (offsetsLOD.offset1 + jitter) * 0.01, SharedData::MipBias);
	float4 s2 = tex.SampleBias(samp, uv + (offsetsLOD.offset2 + float2(jitter.y, -jitter.x)) * 0.01, SharedData::MipBias);
	return lerp(s2, s1, offsetsLOD.weights.x);
}


// 2-sample height-blended stochastic sampling — branchless, no wavefront divergence.
// Sorting in ComputeStochasticOffsets guarantees offset1/offset2 are the two
// highest-weight barycentric vertices, so dropping offset3 loses minimal quality.
inline float4 StochasticEffect(Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsets, float extraLandMipBias)
{
	if (!SharedData::terrainVariationSettings.enableTilingFix)
		return tex.SampleBias(samp, uv, SharedData::MipBias + extraLandMipBias);

	float mipLevel = tex.CalculateLevelOfDetail(samp, uv) + SharedData::MipBias + extraLandMipBias;
	// Far/minified: skip TV blending math + second sample.
	if (mipLevel >= TV_SINGLE_SAMPLE_MIP_START)
		return tex.SampleLevel(samp, uv + offsets.offset1, mipLevel);
	float4 s1 = tex.SampleLevel(samp, uv + offsets.offset1, mipLevel);
	float4 s2 = tex.SampleLevel(samp, uv + offsets.offset2, mipLevel);

	float w1 = exp2(log2(saturate(offsets.weights.x)) * CONTRAST_FACTOR);
	float w2 = exp2(log2(saturate(offsets.weights.y)) * CONTRAST_FACTOR);
	float heightFade = StochasticHeightFadeFromMip(mipLevel);
	float heightInfluence = HEIGHT_INFLUENCE * (1.0 - heightFade);

	float h1 = lerp(dot(s1.rgb, LUMINANCE_WEIGHTS), s1.a, step(0.001, s1.a));
	float h2 = lerp(dot(s2.rgb, LUMINANCE_WEIGHTS), s2.a, step(0.001, s2.a));

	w1 *= (1.0 + heightInfluence * h1);
	w2 *= (1.0 + heightInfluence * h2);

	return lerp(s2, s1, w1 * rcp(w1 + w2));
}


// 2-sample parallax sampling — uses heightmap (alpha) only for blend weights.
inline float4 StochasticEffectParallax(Texture2D tex, SamplerState samp, float2 uv, float mipLevel, StochasticOffsets offsets)
{
	if (!SharedData::terrainVariationSettings.enableTilingFix)
		return tex.SampleLevel(samp, uv, mipLevel);
	// Keep parallax height active, but cut TV to one sample once heavily minified.
	if (mipLevel >= TV_SINGLE_SAMPLE_PARALLAX_MIP_START)
		return tex.SampleLevel(samp, uv + offsets.offset1, mipLevel);

	float4 s1 = tex.SampleLevel(samp, uv + offsets.offset1, mipLevel);
	float4 s2 = tex.SampleLevel(samp, uv + offsets.offset2, mipLevel);

	float w1 = exp2(log2(saturate(offsets.weights.x)) * CONTRAST_FACTOR);
	float w2 = exp2(log2(saturate(offsets.weights.y)) * CONTRAST_FACTOR);
	float heightFade = StochasticHeightFadeFromMip(mipLevel);
	float heightInfluence = HEIGHT_INFLUENCE * (1.0 - heightFade);

	w1 *= (1.0 + heightInfluence * s1.a);
	w2 *= (1.0 + heightInfluence * s2.a);

	return lerp(s2, s1, w1 * rcp(w1 + w2));
}


inline float4 SampleTerrain(Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsets, float extraLandMipBias)
{
	return StochasticEffect(tex, samp, uv, offsets, extraLandMipBias);
}

#endif  // TERRAIN_VARIATION_HLSLI