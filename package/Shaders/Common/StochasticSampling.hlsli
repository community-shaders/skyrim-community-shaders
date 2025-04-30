// Stochastic sampling implementation for texture tiling reduction
// Based on paper "Procedural Stochastic Textures by Tiling and Blending" by Thomas Deliot & Eric Heitz.
//https://eheitzresearch.wordpress.com/722-2/

#ifndef STOCHASTIC_SAMPLING_HLSLI
#define STOCHASTIC_SAMPLING_HLSLI

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

// Create default stochastic offsets (for when feature is disabled)
inline StochasticOffsets CreateDefaultStochasticOffsets()
{
	StochasticOffsets defaults;
	defaults.offset1 = float2(0, 0);
	defaults.offset2 = float2(0, 0);
	defaults.offset3 = float2(0, 0);
	defaults.weights = float3(1, 0, 0);  // Only use the first sample
	return defaults;
}

// Compute a distance factor for scaling stochastic effect strength (0-1)
// Returns 0 for distances <= startDistance, 1 for distances >= maxDistance,
// and a linear interpolation in between
inline float ComputeDistanceFactor(float distance)
{
	float factor = 0.0;
#if defined(PSHADER) || defined(CSHADER) || defined(COMPUTESHADER)
	if (SharedData::terrainVariationSettings.EnableTilingFix) {
		float startDist = SharedData::terrainVariationSettings.startDistance;
		float maxDist = SharedData::terrainVariationSettings.maxDistance;
		// Ensure we don't divide by zero
		float range = max(0.001, maxDist - startDist);
		factor = saturate((distance - startDist) / range);
	}
#endif
	return factor;
}

// Sample texture with stochastic offsets
inline float4 SampleWithOffsets(Texture2D tex, SamplerState samplerTex, float2 UV, StochasticOffsets offsets, float2 dx, float2 dy, float distanceFactor = 1.0)
{
	// Check if terrain variation is enabled in settings
	bool useStochasticSampling = false;

// Only in pixel/compute shaders can we access the feature buffer
#if defined(PSHADER) || defined(CSHADER) || defined(COMPUTESHADER)
	// The EnableTilingFix flag comes directly from TerrainVariation::Settings::enabled in C++
	useStochasticSampling = SharedData::terrainVariationSettings.EnableTilingFix;
#endif

	// Apply stochastic sampling if enabled
	if (useStochasticSampling) {
		// Scale offsets by the distance factor
		float2 scaledOffset1 = offsets.offset1 * distanceFactor;
		float2 scaledOffset2 = offsets.offset2 * distanceFactor;
		float2 scaledOffset3 = offsets.offset3 * distanceFactor;
		
		float4 sample1 = tex.SampleGrad(samplerTex, UV + scaledOffset1, dx, dy);
		float4 sample2 = tex.SampleGrad(samplerTex, UV + scaledOffset2, dx, dy);
		float4 sample3 = tex.SampleGrad(samplerTex, UV + scaledOffset3, dx, dy);
		return sample1 * offsets.weights.x + sample2 * offsets.weights.y + sample3 * offsets.weights.z;
	} else {
		// Fall back to standard sampling when the feature is disabled
		return tex.SampleGrad(samplerTex, UV, dx, dy);
	}
}

// Universal wrapper function that handles both standard and stochastic sampling
inline float4 TerrainTextureSample(Texture2D tex, SamplerState samplerTex, float2 UV, StochasticOffsets offsets, float2 dx, float2 dy, float distanceFactor = 1.0)
{
	return SampleWithOffsets(tex, samplerTex, UV, offsets, dx, dy, distanceFactor);
}

// Main StochasticSample2D function that does the actual sampling
float4 StochasticSample2D(Texture2D<float4> tex, SamplerState samp, float2 uv, StochasticOffsets offsets, float2 dx, float2 dy, float distanceFactor = 1.0)
{
	// Scale offsets by the distance factor
	float2 scaledOffset1 = offsets.offset1 * distanceFactor;
	float2 scaledOffset2 = offsets.offset2 * distanceFactor;
	float2 scaledOffset3 = offsets.offset3 * distanceFactor;
	
	// Sample texture with offsets using explicit gradients for correct mip level selection
	float4 sample1 = tex.SampleGrad(samp, uv + scaledOffset1, dx, dy);
	float4 sample2 = tex.SampleGrad(samp, uv + scaledOffset2, dx, dy);
	float4 sample3 = tex.SampleGrad(samp, uv + scaledOffset3, dx, dy);

	// Blend samples using barycentric weights
	return sample1 * offsets.weights.x +
	       sample2 * offsets.weights.y +
	       sample3 * offsets.weights.z;
}

#endif  // __STOCHASTIC_SAMPLING_HLSLI__