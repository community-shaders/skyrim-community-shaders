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
static const float DISTANCE_SAMPLE_REDUCTION = 2.0; // Mip level where we reduce to 2 samples
static const float FAR_DISTANCE_THRESHOLD = 4.0;  // Mip level where we use single sample with higher mip level

// Structure to hold stochastic sampling offsets and weights
struct StochasticOffsets
{
	float2 offset1;
	float2 offset2;
	float2 offset3;
	float3 weights;
};

// --------------------- FUNCTION DECLARATIONS --------------------- //
float4 StochasticSampleLOD(float rnd, Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsetsLOD, float2 dx, float2 dy);
float4 StochasticEffect(Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsets, float2 dx, float2 dy);
float4 StochasticEffectParallax(Texture2D tex, SamplerState samp, float2 uv, float mipLevel, StochasticOffsets offsets, float2 dx, float2 dy);

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
	float weightSum = weights.x + weights.y + weights.z;
	// Skip expensive division if already normalized
	if (abs(weightSum - 1.0) < 0.01) return weights;
	float rcpWeightSum = rcp(max(weightSum, 1e-6));
	return weights * rcpWeightSum;
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

// --------------------- STOCHASTIC SAMPLING FUNCTIONS --------------------- //

// Stochastic sampling function for Terrain LOD & LOD Mask.
inline float4 StochasticSampleLOD(float rnd, Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsetsLOD, float2 dx, float2 dy)
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

// Main stochastic sampling function
inline float4 StochasticEffect(Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsets, float2 dx, float2 dy)
{
	// Calculate custom mip level from original UVs.
	float mipLevel = tex.CalculateLevelOfDetail(samp, uv);

	// 3 Sample Blend
	float4 sample1 = tex.SampleLevel(samp, uv + offsets.offset1, mipLevel);
	float4 sample2 = tex.SampleLevel(samp, uv + offsets.offset2, mipLevel);
	float4 sample3 = tex.SampleLevel(samp, uv + offsets.offset3, mipLevel);

	// Full height-based blending for terrain
	float contrastFactor = HEIGHT_BLEND_CONTRAST * (1.0 - HEIGHT_INFLUENCE);
	float3 blendWeights = pow(saturate(offsets.weights), contrastFactor);

	// Height calculation - use luminance for RGB data, alpha when available
	float3 luminanceHeights = float3(
		dot(sample1.rgb, LUMINANCE_WEIGHTS),
		dot(sample2.rgb, LUMINANCE_WEIGHTS),
		dot(sample3.rgb, LUMINANCE_WEIGHTS)
	);

	float3 alphaValues = float3(sample1.a, sample2.a, sample3.a);
	float3 alphaMask = step(0.001, alphaValues);
	float3 heights = lerp(luminanceHeights, alphaValues, alphaMask);

	// Combined weight calculation and normalization
	float3 weights = NormalizeWeights(blendWeights * (1.0 + HEIGHT_INFLUENCE * heights));

	// Final blend
	return sample1 * weights.x + sample2 * weights.y + sample3 * weights.z;
}

// Stochastic sampling function without height blending for better performance
// Disable X4000 warning: FXC incorrectly reports potentially uninitialized variables due to complex control flow with early returns and conditional sampling
#pragma warning(push)
#pragma warning(disable : 4000)
inline float4 StochasticEffectParallax(Texture2D tex, SamplerState samp, float2 uv, float mipLevel, StochasticOffsets offsets, float2 dx, float2 dy)
{
	// Early exit for disabled terrain variation - avoid all other computations
	if (!SharedData::terrainVariationSettings.enableTilingFix)
	{
		return tex.SampleLevel(samp, uv, mipLevel);
	}

	// Use progressive mip level increase for better performance in parallax
	float adjustedMipLevel = mipLevel;
	if (mipLevel > 1.0)
	{
		adjustedMipLevel = mipLevel + (MIP_LEVEL_INCREASE * 0.5);
	}

	// Take three samples for blending at the adjusted mip level
	float4 sample1 = tex.SampleLevel(samp, uv + offsets.offset1, adjustedMipLevel);
	float4 sample2 = tex.SampleLevel(samp, uv + offsets.offset2, adjustedMipLevel);
	float4 sample3 = tex.SampleLevel(samp, uv + offsets.offset3, adjustedMipLevel);

	// Simple barycentric blend without height influence
	float3 weights = NormalizeWeights(saturate(offsets.weights));
	return sample1 * weights.x + sample2 * weights.y + sample3 * weights.z;
}
#pragma warning(pop)


#endif  // TERRAIN_VARIATION_HLSLI