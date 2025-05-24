// Implements stochastic noise sampling for terrain textures to reduce tiling artifacts and improve visual quality.
// Based on paper "Procedural Stochastic Textures by Tiling and Blending" by Thomas Deliot & Eric Heitz.
// https://eheitzresearch.wordpress.com/722-2/

#ifndef TERRAIN_VARIATION_HLSLI
#define TERRAIN_VARIATION_HLSLI

#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"

// Structure to hold stochastic sampling offsets and weights
struct StochasticOffsets
{
	float2 offset1;
	float2 offset2;
	float2 offset3;
	float3 weights;
};

// Compute terrain variation blend factor
// Returns blend factor between 0.0 (no stochastic) and 1.0 (full stochastic)
inline float ComputeTerrainVariationBlend(float viewDistance)
{
	if (!SharedData::terrainVariationSettings.enableTilingFix)
		return 0.0;

	float blendFactor = saturate((viewDistance - 1200.0) / 1000.0);
	return smoothstep(0.0, 1.0, blendFactor);
}

// Hash function for stochastic sampling
inline float2 hash2D2D(float2 s)
{
		s = s * float2(1271.5151, 3337.8237);
		return frac(sin(s.x + s.y) * float2(43758.5453, 28637.1369));
}

// Compute offsets for stochastic sampling
inline StochasticOffsets ComputeStochasticOffsets(float2 UV)
{
	float2 skewUV = mul(float2x2(1.0, 0.0, -0.57735027, 1.15470054), UV * 3.464);
	float2 vxID = floor(skewUV);
	float3 barry = float3(frac(skewUV), 0.0);
	barry.z = 1.0 - barry.x - barry.y;

	float4x3 BW_vx = (barry.z > 0) ?
	                     float4x3(float3(vxID, 0), float3(vxID + float2(0, 1), 0), float3(vxID + float2(1, 0), 0), barry.zyx) :
	                     float4x3(float3(vxID + float2(1, 1), 0), float3(vxID + float2(1, 0), 0), float3(vxID + float2(0, 1), 0), float3(-barry.z, 1.0 - barry.y, 1.0 - barry.x));

	StochasticOffsets offsets;
	offsets.offset1 = hash2D2D(BW_vx[0].xy);
	offsets.offset2 = hash2D2D(BW_vx[1].xy);
	offsets.offset3 = hash2D2D(BW_vx[2].xy);
	offsets.weights = BW_vx[3];
	return offsets;
}

// Main stochastic sampling function
inline float4 StochasticEffect(float rnd, float mipLevel, Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsets, float2 dx, float2 dy, float distance = 0.0)
{
	// If feature is disabled, return standard sample
	if (!SharedData::terrainVariationSettings.enableTilingFix)
		return tex.SampleLevel(samp, uv, mipLevel);	// Compute terrain variation blend factor (0.0 = no stochastic, 1.0 = full stochastic)
	float terrainVariationBlend = ComputeTerrainVariationBlend(distance);
	float4 standardSample = tex.SampleLevel(samp, uv, mipLevel);

	// If no terrain variation blend, return standard sample
	if (terrainVariationBlend <= 0.0) {
		return standardSample;
	}

	// Get stochastic samples only when needed (at distance where parallax fades out)
	// Use SampleLevel since we're at distance where mipmap issues don't matter
	float4 sample1 = tex.SampleLevel(samp, uv + offsets.offset1, mipLevel);
	float4 sample2 = tex.SampleLevel(samp, uv + offsets.offset2, mipLevel);
	float4 sample3 = tex.SampleLevel(samp, uv + offsets.offset3, mipLevel);

	// Weight samples according to offsets
	float4 stochasticSample = sample1 * offsets.weights.x +
	                          sample2 * offsets.weights.y +
	                          sample3 * offsets.weights.z;

	// Smooth blend: lerp from standard to stochastic based on distance
	return lerp(standardSample, stochasticSample, terrainVariationBlend);
}
#define StochasticSample(rnd, mipLevel, tex, samp, uv, dist) StochasticEffect(rnd, mipLevel, tex, samp, uv, ComputeStochasticOffsets(uv), ddx(uv), ddy(uv), dist).rgb

#endif  // TERRAIN_VARIATION_HLSLI