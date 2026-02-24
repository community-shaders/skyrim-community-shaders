// Implements stochastic noise sampling for terrain textures to reduce tiling artifacts and improve visual quality.
// Based on paper "Procedural Stochastic Textures by Tiling and Blending" by Thomas Deliot & Eric Heitz.
// https://eheitzresearch.wordpress.com/722-2/

#ifndef TERRAIN_VARIATION_HLSLI
#define TERRAIN_VARIATION_HLSLI

#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"

// --------------------- CONSTANTS AND STRUCTURES --------------------- //
// Height blend operator settings - DO NOT CHANGE THESE VALUES.
static const float HEIGHT_BLEND_CONTRAST = 12.0;  // Controls sharpness of height-based transitions (reduced from 16.0 for performance)
static const float HEIGHT_INFLUENCE = 0.3;        // How much height affects blending (0=pure stochastic, 1=pure height)
// Pre-computed constants to avoid runtime calculations
static const float2x2 SKEW_MATRIX = float2x2(1.0, 0.0, -0.57735027, 1.15470054);
static const float WORLD_SCALE = 332.54;
// Blending constants
static const float3 DEFAULT_WEIGHTS = float3(0.33, 0.33, 0.34);
static const float3 LUMINANCE_WEIGHTS = float3(0.2126, 0.7152, 0.0722);
// Hash constants
static const float2 HASH_MULTIPLIER = float2(1271.5151, 3337.8237);
// Performance optimization constants
static const float MIP_LEVEL_INCREASE = 0.5;      // Additional mip level increase for distance optimization
static const float MIP_BUMP_SCALE = 1.41421356;   // exp2(MIP_LEVEL_INCREASE) = gradient scale for +0.5 mip bump
static const float DISTANCE_SAMPLE_REDUCTION = 2.0; // Mip level where we reduce to 2 samples
static const float FAR_DISTANCE_THRESHOLD = 4.0;  // Mip level where we use single sample with higher mip level
// Importance sampling thresholds (height blend operator culling per Jason Booth technique)
static const float IMPORTANCE_RATIO = 6.0;            // Dominance ratio for importance culling — higher = more conservative
static const float MEDIUM_IMPORTANCE_THRESHOLD = 0.12; // Raw barycentric threshold for medium-distance 2→1 sample culling

// Structure to hold stochastic sampling offsets and weights
struct StochasticOffsets
{
	float2 offset1;
	float2 offset2;
	float2 offset3;
	float3 weights;
};

// --------------------- FUNCTION DECLARATIONS --------------------- //
float4 StochasticSampleLOD(float rnd, Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsetsLOD);
float4 StochasticEffect(Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsets, float mipLevel);
float4 StochasticEffectParallax(Texture2D tex, SamplerState samp, float2 uv, float mipLevel, StochasticOffsets offsets);
// Unified terrain sampling: stochastic when enabled, standard SampleBias fallback
inline float4 SampleTerrain(bool enabled, Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsets, float mipLevel)
{
	[branch] if (enabled)
		return StochasticEffect(tex, samp, uv, offsets, mipLevel);
	return tex.SampleBias(samp, uv, SharedData::MipBias);
}

// --------------------- COMPUTE FUNCTIONS --------------------- //

// Hash function for stochastic sampling
inline float2 hash2D2D(float2 s)
{
	// More efficient hash using frac and multiply operations
	s = frac(s * HASH_MULTIPLIER);
	s += dot(s, s.yx + 19.19);
	return frac((s.xx + s.yy) * s.yx);
}

inline float2 hashLOD(float2 p)
{
	p = frac(p * 0.318);
	return frac(p.x + p.y * float2(17.0, 23.0));
}

inline float3 NormalizeWeights(float3 weights)
{
	return weights * rcp(max(dot(weights, 1.0), 1e-6));
}

inline float2 NormalizeWeights2(float2 weights)
{
	return weights * rcp(max(weights.x + weights.y, 1e-6));
}

// Common barycentric coordinate calculation for stochastic sampling
inline float4x3 ComputeBarycentricVerts(float2 landscapeUV)
{
    float2 scaledUV = landscapeUV * (WORLD_SCALE);
    float2 skewUV = mul(SKEW_MATRIX, scaledUV);
    float2 vxID = floor(skewUV);
    float2 frac_uv = frac(skewUV);

    float barry_z = 1.0 - frac_uv.x - frac_uv.y;
    float3 barry = float3(frac_uv, barry_z);

    return (barry.z > 0) ?
        float4x3(float3(vxID, 0), float3(vxID + float2(0, 1), 0), float3(vxID + float2(1, 0), 0), barry.zyx) :
        float4x3(float3(vxID + float2(1, 1), 0), float3(vxID + float2(1, 0), 0), float3(vxID + float2(0, 1), 0), float3(-barry.z, 1.0 - barry.y, 1.0 - barry.x));
}

inline StochasticOffsets ComputeStochasticOffsets(float2 landscapeUV)
{
    float4x3 BW_vx = ComputeBarycentricVerts(landscapeUV);

    StochasticOffsets offsets;
    offsets.offset1 = hash2D2D(BW_vx[0].xy);
    offsets.offset2 = hash2D2D(BW_vx[1].xy);
    offsets.offset3 = hash2D2D(BW_vx[2].xy);
    offsets.weights = BW_vx[3];

    // Sort by descending barycentric weight so the dominant sample is always first.
    // This enables importance sampling: fetch the strongest sample first, skip weak ones.
    if (offsets.weights.y > offsets.weights.x) {
        float2 tO = offsets.offset1; offsets.offset1 = offsets.offset2; offsets.offset2 = tO;
        float tW = offsets.weights.x; offsets.weights.x = offsets.weights.y; offsets.weights.y = tW;
    }
    if (offsets.weights.z > offsets.weights.x) {
        float2 tO = offsets.offset1; offsets.offset1 = offsets.offset3; offsets.offset3 = tO;
        float tW = offsets.weights.x; offsets.weights.x = offsets.weights.z; offsets.weights.z = tW;
    }
    if (offsets.weights.z > offsets.weights.y) {
        float2 tO = offsets.offset2; offsets.offset2 = offsets.offset3; offsets.offset3 = tO;
        float tW = offsets.weights.y; offsets.weights.y = offsets.weights.z; offsets.weights.z = tW;
    }

    return offsets;
}

inline StochasticOffsets ComputeStochasticOffsetsLOD(float2 landscapeUV)
{
	// Precomputed scaling: (WORLD_SCALE / 0.010416667) * 8.0 = ~255437
	static const float LOD_SCALE = 255437.0;

	float2 scaledUV = landscapeUV * LOD_SCALE;
	float2 cellID = floor(scaledUV);

	StochasticOffsets offsetsLOD;
	// Generate both offsets from single hash to reduce calls
	float2 hash1 = hashLOD(cellID);
	float2 hash2 = hashLOD(cellID + 127.0);

	offsetsLOD.offset1 = hash1 * 0.08;
	offsetsLOD.offset2 = hash2 * 0.08;

	// Simplified weights since we only use 2 samples now
	offsetsLOD.weights = float3(0.65, 0.35, 0.0);

	return offsetsLOD;
}

// --------------------- HELPER FUNCTIONS --------------------- //

// Extracts height from a sample for importance weighting.
// Uses alpha channel when available (displacement data), luminance fallback otherwise.
inline float GetSampleHeight(float4 s)
{
	float lumHeight = dot(s.rgb, LUMINANCE_WEIGHTS);
	return lerp(lumHeight, s.a, step(0.001, s.a));
}

// --------------------- STOCHASTIC SAMPLING FUNCTIONS --------------------- //

// Stochastic sampling function for Terrain LOD & LOD Mask.
inline float4 StochasticSampleLOD(float rnd, Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsetsLOD)
{
	float offsetScale = 0.01;

	// Cheap pseudo-rotation using simple transforms
	float2 dir1 = float2(rnd - 0.5, frac(rnd * 1.618) - 0.5);
	float2 dir2 = float2(dir1.y, -dir1.x);

	// Apply simple scaled offsets
	float2 microOffset1 = (offsetsLOD.offset1 + dir1) * offsetScale;
	float2 microOffset2 = (offsetsLOD.offset2 + dir2) * offsetScale;
	float4 sample1 = tex.SampleBias(samp, uv + microOffset1, SharedData::MipBias);
	float4 sample2 = tex.SampleBias(samp, uv + microOffset2, SharedData::MipBias);

	// Simple 2-sample blend weighted toward first sample
	return lerp(sample2, sample1, 0.65);
}

// Main stochastic sampling function with importance-sampled height blending.
// Uses SampleGrad to preserve hardware anisotropic filtering with the original UV gradients.
// Offsets are pre-sorted by descending barycentric weight so the dominant sample is first.
// At close range, fetches are culled using the height blend operator: if the primary sample's
// height-weighted contribution dominates, secondary/tertiary fetches are skipped entirely.
// This reduces average texture fetches from 3.0 to ~1.5-1.7 per call.
inline float4 StochasticEffect(Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsets, float mipLevel)
{
	float2 uvDx = ddx(uv);
	float2 uvDy = ddy(uv);
	float biasScale = exp2(SharedData::MipBias);
	uvDx *= biasScale;
	uvDy *= biasScale;

	// Far distance: single offset sample at a bumped mip level (cheapest)
	if (mipLevel >= FAR_DISTANCE_THRESHOLD)
	{
		return tex.SampleGrad(samp, uv + offsets.offset1, uvDx * MIP_BUMP_SCALE, uvDy * MIP_BUMP_SCALE);
	}

	// Medium distance: 2-sample blend with importance culling
	if (mipLevel >= DISTANCE_SAMPLE_REDUCTION)
	{
		float4 sample1 = tex.SampleGrad(samp, uv + offsets.offset1, uvDx, uvDy);
		// Skip second sample when its barycentric weight is negligible
		if (offsets.weights.y < MEDIUM_IMPORTANCE_THRESHOLD)
			return sample1;
		float4 sample2 = tex.SampleGrad(samp, uv + offsets.offset2, uvDx, uvDy);
		float2 weights = NormalizeWeights2(saturate(offsets.weights.xy));
		return sample1 * weights.x + sample2 * weights.y;
	}

	// Close distance: importance-sampled height blend
	float contrastFactor = HEIGHT_BLEND_CONTRAST * (1.0 - HEIGHT_INFLUENCE);
	float3 barWeights = pow(saturate(offsets.weights), contrastFactor);

	// Primary sample — always fetched (highest barycentric weight after sorting)
	float4 sample1 = tex.SampleGrad(samp, uv + offsets.offset1, uvDx, uvDy);
	float h1 = GetSampleHeight(sample1);
	float effW1 = barWeights.x * (1.0 + HEIGHT_INFLUENCE * h1);

	// Upper bounds on what remaining samples could contribute assuming max height
	float maxEffW2 = barWeights.y * (1.0 + HEIGHT_INFLUENCE);
	float maxEffW3 = barWeights.z * (1.0 + HEIGHT_INFLUENCE);

	// 1-sample early-out: primary dominates both secondary and tertiary
	if (effW1 > (maxEffW2 + maxEffW3) * IMPORTANCE_RATIO)
		return sample1;

	// Secondary sample
	float4 sample2 = tex.SampleGrad(samp, uv + offsets.offset2, uvDx, uvDy);
	float h2 = GetSampleHeight(sample2);
	float effW2 = barWeights.y * (1.0 + HEIGHT_INFLUENCE * h2);

	// 2-sample early-out: primary + secondary dominate tertiary
	if ((effW1 + effW2) > maxEffW3 * IMPORTANCE_RATIO)
	{
		float2 w = NormalizeWeights2(float2(effW1, effW2));
		return sample1 * w.x + sample2 * w.y;
	}

	// Full 3-sample blend
	float4 sample3 = tex.SampleGrad(samp, uv + offsets.offset3, uvDx, uvDy);
	float h3 = GetSampleHeight(sample3);
	float effW3 = barWeights.z * (1.0 + HEIGHT_INFLUENCE * h3);
	float3 weights = NormalizeWeights(float3(effW1, effW2, effW3));
	return sample1 * weights.x + sample2 * weights.y + sample3 * weights.z;
}

// Stochastic sampling for parallax height queries.
// Always uses full 3-sample barycentric blend to maintain height field continuity
// across simplex boundaries — distance-based sample reduction causes discontinuities
// that show as triangle seams in displaced geometry.
#pragma warning(push)
#pragma warning(disable : 4000)
inline float4 StochasticEffectParallax(Texture2D tex, SamplerState samp, float2 uv, float mipLevel, StochasticOffsets offsets)
{
	if (!SharedData::terrainVariationSettings.enableTilingFix)
	{
		return tex.SampleLevel(samp, uv, mipLevel);
	}

	float adjustedMipLevel = mipLevel;
	if (mipLevel > 1.0)
	{
		adjustedMipLevel = mipLevel + (MIP_LEVEL_INCREASE * 0.5);
	}

	float4 sample1 = tex.SampleLevel(samp, uv + offsets.offset1, adjustedMipLevel);
	float4 sample2 = tex.SampleLevel(samp, uv + offsets.offset2, adjustedMipLevel);
	float4 sample3 = tex.SampleLevel(samp, uv + offsets.offset3, adjustedMipLevel);

	float3 weights = NormalizeWeights(saturate(offsets.weights));
	return sample1 * weights.x + sample2 * weights.y + sample3 * weights.z;
}
#pragma warning(pop)


#endif  // TERRAIN_VARIATION_HLSLI