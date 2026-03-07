// Implements stochastic noise sampling for terrain textures to reduce tiling artifacts and improve visual quality.
// Based on paper "Procedural Stochastic Textures by Tiling and Blending" by Thomas Deliot & Eric Heitz.
// https://eheitzresearch.wordpress.com/722-2/

#ifndef TERRAIN_VARIATION_HLSLI
#define TERRAIN_VARIATION_HLSLI

#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"

// --------------------- CONSTANTS --------------------- //
static const float2x2 SKEW_MATRIX = float2x2(1.0, 0.0, -0.57735027, 1.15470054);
static const float WORLD_SCALE = 332.54;
static const float2 HASH_MULTIPLIER = float2(1271.5151, 3337.8237);
static const float HEIGHT_INFLUENCE = 0.9;
static const float LOW_WEIGHT_THRESHOLD = 0.5;
static const float3 LUMINANCE_WEIGHTS = float3(0.2126, 0.7152, 0.0722);

// --------------------- STRUCTURES --------------------- //
struct StochasticOffsets
{
	float2 offset1;
	float2 offset2;
	float2 offset3;
	float3 weights;
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
	return frac(p.x + p.y * float2(17.0, 23.0));
}

// --------------------- COMPUTE FUNCTIONS --------------------- //
inline StochasticOffsets ComputeStochasticOffsets(float2 landscapeUV)
{
	float2 skewUV = mul(SKEW_MATRIX, landscapeUV * WORLD_SCALE);
	float2 vxID = floor(skewUV);
	float2 f = frac(skewUV);
	float bz = 1.0 - f.x - f.y;

	float2 v0, v1, v2;
	float3 barry;
	if (bz > 0) {
		v0 = vxID;
		v1 = vxID + float2(0, 1);
		v2 = vxID + float2(1, 0);
		barry = float3(bz, f.y, f.x);
	} else {
		v0 = vxID + 1.0;
		v1 = vxID + float2(1, 0);
		v2 = vxID + float2(0, 1);
		barry = float3(-bz, 1.0 - f.y, 1.0 - f.x);
	}

	StochasticOffsets o;
	o.offset1 = hash2D2D(v0);
	o.offset2 = hash2D2D(v1);
	o.offset3 = hash2D2D(v2);
	o.weights = barry;

	// Sort descending by barycentric weight (importance sampling)
	if (o.weights.y > o.weights.x) {
		float2 tO = o.offset1;
		o.offset1 = o.offset2;
		o.offset2 = tO;
		float tW = o.weights.x;
		o.weights.x = o.weights.y;
		o.weights.y = tW;
	}
	if (o.weights.z > o.weights.x) {
		float2 tO = o.offset1;
		o.offset1 = o.offset3;
		o.offset3 = tO;
		float tW = o.weights.x;
		o.weights.x = o.weights.z;
		o.weights.z = tW;
	}
	if (o.weights.z > o.weights.y) {
		float2 tO = o.offset2;
		o.offset2 = o.offset3;
		o.offset3 = tO;
		float tW = o.weights.y;
		o.weights.y = o.weights.z;
		o.weights.z = tW;
	}

	return o;
}

inline StochasticOffsets ComputeStochasticOffsetsLOD(float2 landscapeUV)
{
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

// LOD terrain stochastic sampling — 2 SampleBias, fixed blend
inline float4 StochasticSampleLOD(float rnd, Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsetsLOD, float2 dx, float2 dy)
{
	float2 dir1 = float2(rnd - 0.5, frac(rnd * 1.618) - 0.5);
	float4 s1 = tex.SampleBias(samp, uv + (offsetsLOD.offset1 + dir1) * 0.01, SharedData::MipBias);
	float4 s2 = tex.SampleBias(samp, uv + (offsetsLOD.offset2 + float2(dir1.y, -dir1.x)) * 0.01, SharedData::MipBias);
	return lerp(s2, s1, offsetsLOD.weights.x);
}

// Layer-weight-aware stochastic sampling (Jason Booth technique).
// Low-weight layers use 1 sample — the branch is coherent because layer weights
// are constant per terrain cell, so entire wavefronts take the same path.
// High-weight layers get 2-sample height-blended for quality.
// Height is derived from alpha when available (displacement maps), luminance otherwise.
inline float4 StochasticEffect(Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsets, float layerWeight)
{
	float mipLevel = tex.CalculateLevelOfDetail(samp, uv) + SharedData::MipBias;
	float4 s1 = tex.SampleLevel(samp, uv + offsets.offset1, mipLevel);
	if (layerWeight < LOW_WEIGHT_THRESHOLD)
		return s1;
	float4 s2 = tex.SampleLevel(samp, uv + offsets.offset2, mipLevel);
	float h1 = s1.a > 0.001 ? s1.a : dot(s1.rgb, LUMINANCE_WEIGHTS);
	float h2 = s2.a > 0.001 ? s2.a : dot(s2.rgb, LUMINANCE_WEIGHTS);
	float w1 = offsets.weights.x * offsets.weights.x * (1.0 + HEIGHT_INFLUENCE * h1);
	float w2 = offsets.weights.y * offsets.weights.y * (1.0 + HEIGHT_INFLUENCE * h2);
	return lerp(s2, s1, w1 * rcp(w1 + w2));
}

inline float4 StochasticEffectParallax(Texture2D tex, SamplerState samp, float2 uv, float mipLevel, StochasticOffsets offsets, float2 dx, float2 dy)
{
	float4 s1 = tex.SampleLevel(samp, uv + offsets.offset1, mipLevel);
	float4 s2 = tex.SampleLevel(samp, uv + offsets.offset2, mipLevel);
	if (offsets.weights.z < 0.1)
		return (s1 * offsets.weights.x + s2 * offsets.weights.y) * rcp(offsets.weights.x + offsets.weights.y);
	float4 s3 = tex.SampleLevel(samp, uv + offsets.offset3, mipLevel);
	return s1 * offsets.weights.x + s2 * offsets.weights.y + s3 * offsets.weights.z;
}

inline float4 SampleTerrain(bool enabled, Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsets, float layerWeight)
{
	[branch] if (enabled)
		return StochasticEffect(tex, samp, uv, offsets, layerWeight);
	return tex.SampleBias(samp, uv, SharedData::MipBias);
}

#endif  // TERRAIN_VARIATION_HLSLI