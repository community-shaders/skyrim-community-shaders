// Implements stochastic noise sampling for terrain textures to reduce tiling artifacts and improve visual quality.
// Based on paper "Procedural Stochastic Textures by Tiling and Blending" by Thomas Deliot & Eric Heitz.
// https://eheitzresearch.wordpress.com/722-2/

#ifndef TERRAIN_VARIATION_HLSLI
#define TERRAIN_VARIATION_HLSLI

#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"

// Height blend operator settings - DO NOT CHANGE THESE VALUES.
static const float HEIGHT_BLEND_CONTRAST = 16.0; // Controls sharpness of height-based transitions
static const float CULLING_THRESHOLD = 0.001;    // Minimum weight threshold for sample culling
static const float HEIGHT_INFLUENCE = 0.3;      // How much height affects blending (0=pure stochastic, 1=pure height)

// Structure to hold stochastic sampling offsets and weights
struct StochasticOffsets
{
	float2 offset1;
	float2 offset2;
	float2 offset3;
	float3 weights;
};

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
	// Early return if terrain variation is disabled
	[branch] if (!SharedData::terrainVariationSettings.enableTilingFix) {
		return tex.SampleBias(samp, uv, SharedData::MipBias);
	}
	
	// First determine the weights based only on the offset weights (without height influence)
	float3 blendWeights = offsets.weights;
	
	// Apply contrast and saturation to the initial blend weights
	blendWeights = pow(saturate(blendWeights), HEIGHT_BLEND_CONTRAST * (1.0 - HEIGHT_INFLUENCE));
	
	// Renormalize the weights
	float totalWeight = blendWeights.x + blendWeights.y + blendWeights.z;
	blendWeights = (totalWeight > 0.0) ? blendWeights / totalWeight : float3(0.33, 0.33, 0.34);
	
	// Storage for samples and accumulated results
	float4 stochasticSample = float4(0, 0, 0, 0);
	float culledWeightSum = 0.0;
	float4 sample;
	
	// Only sample textures when their weight exceeds the culling threshold
	// This avoids fetching textures that would be discarded anyway
	[branch] if (blendWeights.x > CULLING_THRESHOLD) {
		sample = tex.SampleLevel(samp, uv + offsets.offset1, mipLevel);
		
		// Adjust weight based on height if needed
		float height = sample.a > 0 ? sample.a : dot(sample.rgb, float3(0.2126, 0.7152, 0.0722));
		float weight = lerp(blendWeights.x, height * blendWeights.x, HEIGHT_INFLUENCE);
		
		stochasticSample += sample * weight;
		culledWeightSum += weight;
	}
	
	[branch] if (blendWeights.y > CULLING_THRESHOLD) {
		sample = tex.SampleLevel(samp, uv + offsets.offset2, mipLevel);
		
		// Adjust weight based on height if needed
		float height = sample.a > 0 ? sample.a : dot(sample.rgb, float3(0.2126, 0.7152, 0.0722));
		float weight = lerp(blendWeights.y, height * blendWeights.y, HEIGHT_INFLUENCE);
		
		stochasticSample += sample * weight;
		culledWeightSum += weight;
	}
	
	[branch] if (blendWeights.z > CULLING_THRESHOLD) {
		sample = tex.SampleLevel(samp, uv + offsets.offset3, mipLevel);
		
		// Adjust weight based on height if needed
		float height = sample.a > 0 ? sample.a : dot(sample.rgb, float3(0.2126, 0.7152, 0.0722));
		float weight = lerp(blendWeights.z, height * blendWeights.z, HEIGHT_INFLUENCE);
		
		stochasticSample += sample * weight;
		culledWeightSum += weight;
	}
	
	// Renormalize after culling
	if (culledWeightSum > 0.0) {
		stochasticSample /= culledWeightSum;
	} else {
		// If all samples were culled, get a single sample as fallback
		stochasticSample = tex.SampleLevel(samp, uv + offsets.offset1, mipLevel); // Fallback
	}
	return stochasticSample;
}

// Runtime macro that checks enableTilingFix and falls back to regular sampling when disabled
#define StochasticSample(rnd, mipLevel, tex, samp, uv) \
	(SharedData::terrainVariationSettings.enableTilingFix ? \
		StochasticEffect(rnd, mipLevel, tex, samp, uv, ComputeStochasticOffsets(uv), ddx(uv), ddy(uv)).rgb : \
		tex.SampleLevel(samp, uv, mipLevel).rgb)

#endif  // TERRAIN_VARIATION_HLSLI