// Implements stochastic noise sampling for terrain textures to reduce tiling artifacts and improve visual quality.
// Based on paper "Procedural Stochastic Textures by Tiling and Blending" by Thomas Deliot & Eric Heitz.
// https://eheitzresearch.wordpress.com/722-2/

#ifndef TERRAIN_VARIATION_HLSLI
#define TERRAIN_VARIATION_HLSLI

#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"

// Height blend operator settings
static const float HEIGHT_BLEND_CONTRAST = 16.0; // Controls sharpness of height-based transitions
static const float CULLING_THRESHOLD = 0.01;     // Minimum weight threshold for sample culling
static const float HEIGHT_INFLUENCE = 0.5;       // How much height affects blending (0=pure stochastic, 1=pure height)

// Structure to hold stochastic sampling offsets and weights
struct StochasticOffsets
{
	float2 offset1;
	float2 offset2;
	float2 offset3;
	float3 weights;
};

// Check if terrain variation is enabled
inline bool IsTerrainVariationEnabled()
{
	return SharedData::terrainVariationSettings.enableTilingFix;
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
inline float4 StochasticEffect(float rnd, float mipLevel, Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsets, float2 dx, float2 dy)
{
	// If feature is disabled, return standard sample
	[branch] if (!IsTerrainVariationEnabled())
		return tex.SampleLevel(samp, uv, mipLevel);

	bool useParallax = SharedData::extendedMaterialSettings.EnableTerrainParallax;
	float4 sample1, sample2, sample3;
	// Get stochastic samples
	[branch] if (useParallax) {
		// Parallax enabled, can use SampleLevel for better perf
		sample1 = tex.SampleLevel(samp, uv + offsets.offset1, mipLevel);
		sample2 = tex.SampleLevel(samp, uv + offsets.offset2, mipLevel);
		sample3 = tex.SampleLevel(samp, uv + offsets.offset3, mipLevel);
	} else {
		// When parallax disabled, samplelevel causes mipmap issues, it uses too low a mipmap level up close.
		sample1 = tex.SampleGrad(samp, uv + offsets.offset1, dx, dy);
		sample2 = tex.SampleGrad(samp, uv + offsets.offset2, dx, dy);
		sample3 = tex.SampleGrad(samp, uv + offsets.offset3, dx, dy);
	}

	// Extract height information from samples (use alpha channel if available, otherwise luminance)
	float3 heights = float3(
		sample1.a > 0 ? sample1.a : dot(sample1.rgb, float3(0.299, 0.587, 0.114)),
		sample2.a > 0 ? sample2.a : dot(sample2.rgb, float3(0.299, 0.587, 0.114)),
		sample3.a > 0 ? sample3.a : dot(sample3.rgb, float3(0.299, 0.587, 0.114))
	);

	// Compute height-based blend weights
	float3 heightBlendWeights = lerp(offsets.weights, heights, HEIGHT_INFLUENCE);
	heightBlendWeights = pow(saturate(heightBlendWeights), HEIGHT_BLEND_CONTRAST);
	
	// Renormalize weights
	float totalWeight = heightBlendWeights.x + heightBlendWeights.y + heightBlendWeights.z;
	heightBlendWeights = (totalWeight > 0.0) ? heightBlendWeights / totalWeight : float3(0.33, 0.33, 0.34);

	// Adaptive culling - only blend samples that contribute meaningfully
	float4 stochasticSample = float4(0, 0, 0, 0);
	float culledWeightSum = 0.0;
	
	[branch] if (heightBlendWeights.x > CULLING_THRESHOLD) {
		stochasticSample += sample1 * heightBlendWeights.x;
		culledWeightSum += heightBlendWeights.x;
	}
	
	[branch] if (heightBlendWeights.y > CULLING_THRESHOLD) {
		stochasticSample += sample2 * heightBlendWeights.y;
		culledWeightSum += heightBlendWeights.y;
	}
	
	[branch] if (heightBlendWeights.z > CULLING_THRESHOLD) {
		stochasticSample += sample3 * heightBlendWeights.z;
		culledWeightSum += heightBlendWeights.z;
	}
		// Renormalize after culling, fallback to first sample if all culled
	if (culledWeightSum > 0.0) {
		stochasticSample /= culledWeightSum;
	} else {
		stochasticSample = sample1; // Fallback
	}

	return stochasticSample;
}

#define StochasticSample(rnd, mipLevel, tex, samp, uv) StochasticEffect(rnd, mipLevel, tex, samp, uv, ComputeStochasticOffsets(uv), ddx(uv), ddy(uv)).rgb


#endif  // TERRAIN_VARIATION_HLSLI