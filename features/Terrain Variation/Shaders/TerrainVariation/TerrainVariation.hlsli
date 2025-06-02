// Implements stochastic noise sampling for terrain textures to reduce tiling artifacts and improve visual quality.
// Based on paper "Procedural Stochastic Textures by Tiling and Blending" by Thomas Deliot & Eric Heitz.
// https://eheitzresearch.wordpress.com/722-2/

#ifndef TERRAIN_VARIATION_HLSLI
#define TERRAIN_VARIATION_HLSLI

#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/FrameBuffer.hlsli"

// --------------------- CONSTANTS AND STRUCTURES --------------------- //
// Height blend operator settings - DO NOT CHANGE THESE VALUES.
static const float HEIGHT_BLEND_CONTRAST = 16.0;  // Controls sharpness of height-based transitions
static const float CULLING_THRESHOLD = 0.001;     // Minimum weight threshold for sample culling
static const float HEIGHT_INFLUENCE = 0.3;        // How much height affects blending (0=pure stochastic, 1=pure height)
// Pre-computed constants to avoid runtime calculations
static const float2x2 SKEW_MATRIX = float2x2(1.0, 0.0, -0.57735027, 1.15470054);
static const float WORLD_SCALE = 0.03464;
static const float INV_SQRT3 = 0.57735027;
// Blending constants
static const float3 DEFAULT_WEIGHTS = float3(0.33, 0.33, 0.34);
static const float3 LUMINANCE_WEIGHTS = float3(0.2126, 0.7152, 0.0722);
// Hash constants
static const float2 HASH_MULTIPLIER = float2(1271.5151, 3337.8237);
static const float2 HASH_SINE_MULTIPLIER = float2(43758.5453, 28637.1369);

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
	s = s * HASH_MULTIPLIER;
	return frac(sin(s.x + s.y) * HASH_SINE_MULTIPLIER);
}

inline float2 ComputeWorldUV(float3 worldPos, uint eyeIndex = 0)
{
    return (worldPos.xy + FrameBuffer::CameraPosAdjust[eyeIndex].xy) * WORLD_SCALE;
}


// Generate world-position based stochastic offsets for terrain. Fixes cell border issues.
inline StochasticOffsets ComputeStochasticOffsets(float2 precomputedWorldUV, float2 screenPos = float2(0, 0))
{   
    float2 skewUV = mul(SKEW_MATRIX, precomputedWorldUV);
    float2 vxID = floor(skewUV);
    float2 frac_uv = frac(skewUV);
    float barry_z = 1.0 - frac_uv.x - frac_uv.y;
    float3 barry = float3(frac_uv, barry_z);
    
    // Calculate vertex IDs and barycentric weights for the triangle
    float4x3 BW_vx = (barry.z > 0) ?
                     float4x3(float3(vxID, 0), float3(vxID + float2(0, 1), 0), float3(vxID + float2(1, 0), 0), barry.zyx) :
                     float4x3(float3(vxID + float2(1, 1), 0), float3(vxID + float2(1, 0), 0), float3(vxID + float2(0, 1), 0), float3(-barry.z, 1.0 - barry.y, 1.0 - barry.x));
    
    // Generate offsets and store weights
    StochasticOffsets offsets;
    offsets.offset1 = hash2D2D(BW_vx[0].xy);
    offsets.offset2 = hash2D2D(BW_vx[1].xy);
    offsets.offset3 = hash2D2D(BW_vx[2].xy);
    offsets.weights = BW_vx[3];
    
    return offsets;
}


// Main stochastic sampling function
inline float4 StochasticEffect(float rnd, float mipLevel, Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsets, float2 dx, float2 dy, float3 worldPos = float3(0,0,0))  // Used for normal/diffuse text. Luminence-based blending helps preserve details close to camera.
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

    float3 heights = float3(height1, height2, height3);
	float3 weights = blendWeights * (1.0 + HEIGHT_INFLUENCE * (heights - 1.0));

	// Blend samples with height-adjusted weights
	float4 result = sample1 * weights.x + sample2 * weights.y + sample3 * weights.z;

	// Renormalize final result
	float finalWeightSum = weights.x + weights.y + weights.z;

	return result;
}

// --------------------- SPECIAL USE CASES --------------------- //

// Same as StochasticEffect but no height/luminescence influence, so much cheaper but worse quality, doesn't matter for the use case.
inline float4 StochasticSample3(float rnd, float mipLevel, Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsets, float2 dx, float2 dy, float2 screenPos = float2(0, 0))
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

// --------------------- LOD SAMPLING FUNCTIONS --------------------- //

inline float2 hashLOD(float2 p) {
    p = frac(p * 0.3183099);
    return frac(17.0 * p.yx + p.x * p.y);
}

inline StochasticOffsets ComputeStochasticOffsetsLOD(float2 UV)
{
	// Simplified LOD offset calculation - much cheaper than full stochastic
	float2 scaledUV = UV * 8.0; // Simple scaling instead of complex skew matrix
	float2 cellID = floor(scaledUV);
	float2 localUV = frac(scaledUV);
	
	// Simple hash-based offsets without complex triangle calculations
	StochasticOffsets offsetsLOD;
	offsetsLOD.offset1 = hashLOD(cellID) * 0.08;
	offsetsLOD.offset2 = hashLOD(cellID + float2(1, 0)) * 0.08;

	// Simplified weights based on local UV position
	float3 simpleWeights = float3(0.4, 0.35, 0.25);
	offsetsLOD.weights = simpleWeights;
	
	return offsetsLOD;
}

// Stochastic sampling function for Terrain LOD & LOD Mask.
inline float4 StochasticSampleLOD(float rnd, float mipLevel, Texture2D tex, SamplerState samp, float2 uv, StochasticOffsets offsets, float2 dx, float2 dy)
{
	float offsetScale = 0.15;

	// Add simple rotation to only two offsets (using the random value to vary rotation per pixel)
	float angle1 = rnd * 6.28;     // Random base angle between 0-2π
	float angle2 = angle1 + 2.09;  // ~120° offset

	// Create rotation matrices for only two samples
	float2x2 rot1 = float2x2(cos(angle1), -sin(angle1), sin(angle1), cos(angle1));
	float2x2 rot2 = float2x2(cos(angle2), -sin(angle2), sin(angle2), cos(angle2));

	// Apply rotation to offsets before scaling them down
	float2 microOffset1 = mul(rot1, offsets.offset1) * offsetScale;
	float2 microOffset2 = mul(rot2, offsets.offset2) * offsetScale;

	// Sample with rotated micro-offsets (only two samples)
	float4 sample1 = tex.SampleLevel(samp, uv + microOffset1, mipLevel);
	float4 sample2 = tex.SampleLevel(samp, uv + microOffset2, mipLevel);
	
	// Interpolate the third sample from the first two
	float4 sample3 = lerp(sample1, sample2, 0.5);

	// Blend using the barycentric weights
	float4 result = sample1 * offsets.weights.x +
	                sample2 * offsets.weights.y +
	                sample3 * offsets.weights.z;

	return result;
}


#endif  // TERRAIN_VARIATION_HLSLI	