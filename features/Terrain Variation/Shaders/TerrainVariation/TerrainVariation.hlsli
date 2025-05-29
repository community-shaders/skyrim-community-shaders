// Implements stochastic noise sampling for terrain textures to reduce tiling artifacts and improve visual quality.
// Based on paper "Procedural Stochastic Textures by Tiling and Blending" by Thomas Deliot & Eric Heitz.
// https://eheitzresearch.wordpress.com/722-2/

#ifndef TERRAIN_VARIATION_HLSLI
#define TERRAIN_VARIATION_HLSLI

#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"

// Height blend operator settings - DO NOT CHANGE THESE VALUES.
static const float HEIGHT_BLEND_CONTRAST = 16.0;  // Controls sharpness of height-based transitions
static const float CULLING_THRESHOLD = 0.001;     // Minimum weight threshold for sample culling
static const float HEIGHT_INFLUENCE = 0.3;        // How much height affects blending (0=pure stochastic, 1=pure height)

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

// Compute single offset for stochastic sampling (optimized for single sample)
inline float2 ComputeStochasticOffsets1(float2 UV)
{
	float2 skewUV = mul(float2x2(1.0, 0.0, -0.57735027, 1.15470054), UV * 3.464);
	float2 vxID = floor(skewUV);
	float3 barry = float3(frac(skewUV), 0.0);
	barry.z = 1.0 - barry.x - barry.y;

	// Only compute the first vertex ID based on barycentric coordinates
	float2 firstVertexID = (barry.z > 0) ? vxID : (vxID + float2(1, 1));

	// Return only the first hash offset
	return hash2D2D(firstVertexID);
}

inline StochasticOffsets ComputeStochasticOffsetsLOD(float2 UV)
{
	// Use a much smaller scale factor for LOD terrain to create subtle variation
	// instead of dramatic pattern changes
	float2 skewUV = mul(float2x2(1.0, 0.0, -0.57735027, 1.15470054), UV * 1.5);
	float2 vxID = floor(skewUV);
	float3 barry = float3(frac(skewUV), 0.0);
	barry.z = 1.0 - barry.x - barry.y;

	// Create a more stable triangle pattern
	float4x3 BW_vx = (barry.z > 0) ?
	                     float4x3(float3(vxID, 0), float3(vxID + float2(0, 1), 0), float3(vxID + float2(1, 0), 0), barry.zyx) :
	                     float4x3(float3(vxID + float2(1, 1), 0), float3(vxID + float2(1, 0), 0), float3(vxID + float2(0, 1), 0), float3(-barry.z, 1.0 - barry.y, 1.0 - barry.x));

	// Generate smaller offsets for more subtle variation
	StochasticOffsets offsetsLOD;
	offsetsLOD.offset1 = hash2D2D(BW_vx[0].xy) * 0.15;  // Reduced offset magnitude
	offsetsLOD.offset2 = hash2D2D(BW_vx[1].xy) * 0.15;
	offsetsLOD.offset3 = hash2D2D(BW_vx[2].xy) * 0.15;

	// Use smoother weights with less contrast
	float3 smoothWeights = BW_vx[3];
	// Apply mild smoothing to weights
	smoothWeights = pow(smoothWeights, 0.8);
	// Renormalize
	smoothWeights /= (smoothWeights.x + smoothWeights.y + smoothWeights.z);

	offsetsLOD.weights = smoothWeights;
	return offsetsLOD;
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
inline float4 StochasticEffect(float rnd, float mipLevel, Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsets, float2 dx, float2 dy)  // Used for normal/diffuse text. Luminence-based blending helps preserve details close to camera.
{
	// Early return if terrain variation is disabled
	[branch] if (!SharedData::terrainVariationSettings.enableTilingFix)
	{
		return tex.SampleBias(samp, uv, SharedData::MipBias);
	}

	// Apply contrast to the initial blend weights (without height influence)
	float3 blendWeights = pow(saturate(offsets.weights), HEIGHT_BLEND_CONTRAST * (1.0 - HEIGHT_INFLUENCE));

	// Renormalize the weights
	float totalWeight = blendWeights.x + blendWeights.y + blendWeights.z;
	blendWeights = (totalWeight > 0.0) ? blendWeights / totalWeight : float3(0.33, 0.33, 0.34);

	// Sample all three locations
	float4 sample1, sample2, sample3;
	[branch] if (SharedData::extendedMaterialSettings.EnableTerrainParallax)
	{
		// Parallax enabled, can use SampleLevel for better perf
		sample1 = tex.SampleLevel(samp, uv + offsets.offset1, mipLevel);
		sample2 = tex.SampleLevel(samp, uv + offsets.offset2, mipLevel);
		sample3 = tex.SampleLevel(samp, uv + offsets.offset3, mipLevel);
	}
	else
	{
		// When parallax disabled, samplelevel causes mipmap issues, it uses too low a mipmap level up close.
		sample1 = tex.SampleGrad(samp, uv + offsets.offset1, dx, dy);
		sample2 = tex.SampleGrad(samp, uv + offsets.offset2, dx, dy);
		sample3 = tex.SampleGrad(samp, uv + offsets.offset3, dx, dy);
	}

	// Apply height-based weight adjustments
	float height1 = sample1.a > 0 ? sample1.a : dot(sample1.rgb, float3(0.2126, 0.7152, 0.0722));
	float height2 = sample2.a > 0 ? sample2.a : dot(sample2.rgb, float3(0.2126, 0.7152, 0.0722));
	float height3 = sample3.a > 0 ? sample3.a : dot(sample3.rgb, float3(0.2126, 0.7152, 0.0722));

	float weight1 = lerp(blendWeights.x, height1 * blendWeights.x, HEIGHT_INFLUENCE);
	float weight2 = lerp(blendWeights.y, height2 * blendWeights.y, HEIGHT_INFLUENCE);
	float weight3 = lerp(blendWeights.z, height3 * blendWeights.z, HEIGHT_INFLUENCE);

	// Blend samples with height-adjusted weights
	float4 result = sample1 * weight1 + sample2 * weight2 + sample3 * weight3;

	// Renormalize final result
	float finalWeightSum = weight1 + weight2 + weight3;
	if (finalWeightSum > 0.0) {
		result /= finalWeightSum;
	}

	return result;
}

// Cheap Stochastic Effect for single sample. Used for RMAOS.
inline float4 StochasticSample1(float rnd, float mipLevel, Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsets, float2 dx, float2 dy)  // Used for RMAOS (maybe LOD in future)
{
	return tex.SampleLevel(samp, uv + offsets.offset1, mipLevel);
}

// Same as StochasticEffect but no height/luminescence influence, so much cheaper but worse quality, doesn't matter for the use case.
inline float4 StochasticSample3(float rnd, float mipLevel, Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsets, float2 dx, float2 dy)  // Used for parallaxcoords & getheight funct.
{
	// Sample the three texture offsets using the provided mip level
	float4 sample1 = tex.SampleLevel(samp, uv + offsets.offset1, mipLevel);
	float4 sample2 = tex.SampleLevel(samp, uv + offsets.offset2, mipLevel);
	float4 sample3 = tex.SampleLevel(samp, uv + offsets.offset3, mipLevel);

	// Blend using the barycentric weights
	float4 result = sample1 * offsets.weights.x +
	                sample2 * offsets.weights.y +
	                sample3 * offsets.weights.z;

	return result;
}

// Special version for LOD mask textures that should have minimal blurring
inline float4 StochasticSampleLODMask(float rnd, float mipLevel, Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsets, float2 dx, float2 dy)
{
	float offsetScale = 0.04;

	// Add simple rotation to each offset (using the random value to vary rotation per pixel)
	float angle1 = rnd * 3.14;     // Random base angle between 0-2π
	float angle2 = angle1 + 1.04;  // ~120° offset
	float angle3 = angle1 + 2.09;  // ~240° offset

	// Create rotation matrices
	float2x2 rot1 = float2x2(cos(angle1), -sin(angle1), sin(angle1), cos(angle1));
	float2x2 rot2 = float2x2(cos(angle2), -sin(angle2), sin(angle2), cos(angle2));
	float2x2 rot3 = float2x2(cos(angle3), -sin(angle3), sin(angle3), cos(angle3));

	// Apply rotation to offsets before scaling them down
	float2 microOffset1 = mul(rot1, offsets.offset1) * offsetScale;
	float2 microOffset2 = mul(rot2, offsets.offset2) * offsetScale;
	float2 microOffset3 = mul(rot3, offsets.offset3) * offsetScale;

	// Sample with rotated micro-offsets
	float tinyMipBias = 0.05;
	float4 sample1 = tex.SampleLevel(samp, uv + microOffset1, mipLevel + tinyMipBias);
	float4 sample2 = tex.SampleLevel(samp, uv + microOffset2, mipLevel + tinyMipBias);
	float4 sample3 = tex.SampleLevel(samp, uv + microOffset3, mipLevel + tinyMipBias);

	// Blend using the barycentric weights (unchanged)
	float4 result = sample1 * offsets.weights.x +
	                sample2 * offsets.weights.y +
	                sample3 * offsets.weights.z;

	return result;
}

// Stochastic sampling function for Full Terrain LOD. Uses rotated offsets for more variation.
inline float4 StochasticSampleLOD(float rnd, float mipLevel, Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsets, float2 dx, float2 dy)
{
	float offsetScale = 0.15;

	// Add simple rotation to each offset (using the random value to vary rotation per pixel)
	float angle1 = rnd * 6.28;     // Random base angle between 0-2π
	float angle2 = angle1 + 2.09;  // ~120° offset
	float angle3 = angle1 + 4.18;  // ~240° offset

	// Create rotation matrices
	float2x2 rot1 = float2x2(cos(angle1), -sin(angle1), sin(angle1), cos(angle1));
	float2x2 rot2 = float2x2(cos(angle2), -sin(angle2), sin(angle2), cos(angle2));
	float2x2 rot3 = float2x2(cos(angle3), -sin(angle3), sin(angle3), cos(angle3));

	// Apply rotation to offsets before scaling them down
	float2 microOffset1 = mul(rot1, offsets.offset1) * offsetScale;
	float2 microOffset2 = mul(rot2, offsets.offset2) * offsetScale;
	float2 microOffset3 = mul(rot3, offsets.offset3) * offsetScale;

	// Sample with rotated micro-offsets
	float4 sample1 = tex.SampleLevel(samp, uv + microOffset1, mipLevel);
	float4 sample2 = tex.SampleLevel(samp, uv + microOffset2, mipLevel);
	float4 sample3 = tex.SampleLevel(samp, uv + microOffset3, mipLevel);

	// Blend using the barycentric weights
	float4 result = sample1 * offsets.weights.x +
	                sample2 * offsets.weights.y +
	                sample3 * offsets.weights.z;

	return result;
}

#endif  // TERRAIN_VARIATION_HLSLI