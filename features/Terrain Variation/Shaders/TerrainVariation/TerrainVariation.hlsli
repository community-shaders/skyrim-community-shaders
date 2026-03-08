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
static const float HEIGHT_BLEND_CONTRAST = 12.0;
static const float HEIGHT_INFLUENCE = 0.3;
static const float3 LUMINANCE_WEIGHTS = float3(0.2126, 0.7152, 0.0722);
// Distance-based mip curve: smoothly increases mip at distance to save bandwidth
static const float DISTANCE_MIP_SCALE = 0.25;  // Fraction of base mip added at distance

// --------------------- STRUCTURES --------------------- //
struct StochasticOffsets
{
	float2 offset1;
	float2 offset2;
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

	// Sort vertices by weight descending before hashing — only hash top 2.
	if (barry.y > barry.x) {
		float2 tV = v0; v0 = v1; v1 = tV;
		float tW = barry.x; barry.x = barry.y; barry.y = tW;
	}
	if (barry.z > barry.x) {
		float2 tV = v0; v0 = v2; v2 = tV;
		float tW = barry.x; barry.x = barry.z; barry.z = tW;
	}
	if (barry.z > barry.y) {
		float2 tV = v1; v1 = v2; v2 = tV;
		float tW = barry.y; barry.y = barry.z; barry.z = tW;
	}

	StochasticOffsets o;
	o.offset1 = hash2D2D(v0);
	o.offset2 = hash2D2D(v1);
	o.weights = barry;
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

// 2-sample height-blended stochastic sampling — branchless, no wavefront divergence.
// Sorting in ComputeStochasticOffsets guarantees offset1/offset2 are the two
// highest-weight barycentric vertices, so dropping offset3 loses minimal quality.
inline float4 StochasticEffect(Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsets)
{
	float mipLevel = tex.CalculateLevelOfDetail(samp, uv) + SharedData::MipBias;
	float4 s1 = tex.SampleLevel(samp, uv + offsets.offset1, mipLevel);
	float4 s2 = tex.SampleLevel(samp, uv + offsets.offset2, mipLevel);

	float contrastFactor = HEIGHT_BLEND_CONTRAST * (1.0 - HEIGHT_INFLUENCE);
	float w1 = pow(saturate(offsets.weights.x), contrastFactor);
	float w2 = pow(saturate(offsets.weights.y), contrastFactor);

	float h1 = s1.a > 0.001 ? s1.a : dot(s1.rgb, LUMINANCE_WEIGHTS);
	float h2 = s2.a > 0.001 ? s2.a : dot(s2.rgb, LUMINANCE_WEIGHTS);

	w1 *= (1.0 + HEIGHT_INFLUENCE * h1);
	w2 *= (1.0 + HEIGHT_INFLUENCE * h2);

	return lerp(s2, s1, w1 * rcp(w1 + w2));
}

// 2-sample parallax sampling with cheap height blend to prevent seams.
inline float4 StochasticEffectParallax(Texture2D tex, SamplerState samp, float2 uv, float mipLevel, StochasticOffsets offsets, float2 dx, float2 dy)
{
	float adjustedMip = mipLevel * (1.0 + DISTANCE_MIP_SCALE);
	float4 s1 = tex.SampleLevel(samp, uv + offsets.offset1, adjustedMip);
	float4 s2 = tex.SampleLevel(samp, uv + offsets.offset2, adjustedMip);

	float h1 = s1.a > 0.001 ? s1.a : dot(s1.rgb, LUMINANCE_WEIGHTS);
	float h2 = s2.a > 0.001 ? s2.a : dot(s2.rgb, LUMINANCE_WEIGHTS);

	float w1 = offsets.weights.x * offsets.weights.x * (1.0 + HEIGHT_INFLUENCE * h1);
	float w2 = offsets.weights.y * offsets.weights.y * (1.0 + HEIGHT_INFLUENCE * h2);

	return lerp(s2, s1, w1 * rcp(w1 + w2));
}

inline float4 SampleTerrain(bool enabled, Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsets)
{
	[branch] if (enabled)
		return StochasticEffect(tex, samp, uv, offsets);
	return tex.SampleBias(samp, uv, SharedData::MipBias);
}

#endif  // TERRAIN_VARIATION_HLSLI