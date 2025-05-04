// TerrainVariation.hlsli
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

// Compute a distance factor for scaling stochastic effect strength (0-1)
// Returns 0 for distances <= startDistance, 1 for distances >= maxDistance
inline float ComputeDistanceFactor(float distance)
{
	float factor = 0.0;
#if defined(PSHADER) || defined(CSHADER) || defined(COMPUTESHADER)
	if (SharedData::terrainVariationSettings.enableTilingFix) {
		float startDist = SharedData::terrainVariationSettings.startDistance;
		float maxDist = SharedData::terrainVariationSettings.maxDistance;
		float range = max(0.001, maxDist - startDist);
		factor = saturate((distance - startDist) / range);
	}
#endif
	return factor;
}

// Hash function for stochastic sampling
inline float2 hash2D2D(float2 s)
{
	return frac(sin(fmod(float2(dot(s, float2(127.1, 311.7)), dot(s, float2(269.5, 183.3))), 3.14159)) * 43758.5453);
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

// Main stochastic sampling function - combined implementation that replaces all previous variations
inline float4 StochasticEffect(Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsets, float2 dx, float2 dy, float distance = 0.0)
{
	// Check if terrain variation is enabled in settings
	bool useStochasticSampling = false;

#if defined(PSHADER) || defined(CSHADER) || defined(COMPUTESHADER)
	useStochasticSampling = SharedData::terrainVariationSettings.enableTilingFix;
#endif

	// If feature is disabled, return standard sample
	if (!useStochasticSampling) {
#if defined(PSHADER) || defined(CSHADER) || defined(COMPUTESHADER)
		return tex.SampleBias(samp, uv, SharedData::MipBias);
#else
		return tex.Sample(samp, uv);
#endif
	}

	// Calculate distance factor (0 when close, 1 when far)
	float distanceFactor = ComputeDistanceFactor(distance);

	// If too close, use standard sampling
	if (distanceFactor < 0.001f) {
#if defined(PSHADER) || defined(CSHADER) || defined(COMPUTESHADER)
		return tex.SampleBias(samp, uv, SharedData::MipBias);
#else
		return tex.Sample(samp, uv);
#endif
	}

	// Get stochastic samples
	float4 sample1 = tex.SampleGrad(samp, uv + offsets.offset1, dx, dy);
	float4 sample2 = tex.SampleGrad(samp, uv + offsets.offset2, dx, dy);
	float4 sample3 = tex.SampleGrad(samp, uv + offsets.offset3, dx, dy);

	// Weight samples according to offsets
	float4 stochasticSample = sample1 * offsets.weights.x +
	                          sample2 * offsets.weights.y +
	                          sample3 * offsets.weights.z;

// Get standard sample for blending
#if defined(PSHADER) || defined(CSHADER) || defined(COMPUTESHADER)
	float4 standardSample = tex.SampleBias(samp, uv, SharedData::MipBias);
#else
	float4 standardSample = tex.Sample(samp, uv);
#endif

	// Blend between standard and stochastic based on distance
	return lerp(standardSample, stochasticSample, distanceFactor);
}
#define StochasticSample(tex, samp, uv, dist) StochasticEffect(tex, samp, uv, ComputeStochasticOffsets(uv), ddx(uv), ddy(uv), dist).rgb

#endif  // TERRAIN_VARIATION_HLSLI