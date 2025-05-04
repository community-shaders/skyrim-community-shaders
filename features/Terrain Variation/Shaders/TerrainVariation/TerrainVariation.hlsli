// TerrainVariation.hlsli
// Implements stochastic noise sampling for terrain textures to reduce tiling artifacts and improve visual quality.
// Utilises StochasticSampling.hlsli for the other functions.

#ifndef TERRAIN_VARIATION_HLSLI
#define TERRAIN_VARIATION_HLSLI

#include "Common/StochasticSampling.hlsli"

// Hash function for stochastic sampling
inline float2 hash2D2D(float2 s)
{
	return frac(sin(fmod(float2(dot(s, float2(127.1, 311.7)), dot(s, float2(269.5, 183.3))), 3.14159)) * 43758.5453);
}

// Stochastic sampling algorithm
inline float3 StochasticSample(Texture2D tex, SamplerState samplerTex, float2 UV, float distance = 0.0)
{
	float distanceFactor = ComputeDistanceFactor(distance);
	
	if (distanceFactor < 0.001f) {
        #if defined(PSHADER) || defined(CSHADER) || defined(COMPUTESHADER)
            return tex.SampleBias(samplerTex, UV, SharedData::MipBias).rgb;
        #else
            return tex.Sample(samplerTex, UV).rgb;
        #endif
    }
	
	float2 skewUV = mul(float2x2(1.0, 0.0, -0.57735027, 1.15470054), UV * 3.464);
	float2 vxID = floor(skewUV);
	float3 barry = float3(frac(skewUV), 0.0);
	barry.z = 1.0 - barry.x - barry.y;

	float4x3 BW_vx = (barry.z > 0) ?
	                     float4x3(float3(vxID, 0), float3(vxID + float2(0, 1), 0), float3(vxID + float2(1, 0), 0), barry.zyx) :
	                     float4x3(float3(vxID + float2(1, 1), 0), float3(vxID + float2(1, 0), 0), float3(vxID + float2(0, 1), 0), float3(-barry.z, 1.0 - barry.y, 1.0 - barry.x));

	float2 dx = ddx(UV);
	float2 dy = ddy(UV);

	float2 offset1 = hash2D2D(BW_vx[0].xy);
	float2 offset2 = hash2D2D(BW_vx[1].xy);
	float2 offset3 = hash2D2D(BW_vx[2].xy);

	float3 sample1 = tex.SampleGrad(samplerTex, UV + offset1, dx, dy).rgb;
	float3 sample2 = tex.SampleGrad(samplerTex, UV + offset2, dx, dy).rgb;
	float3 sample3 = tex.SampleGrad(samplerTex, UV + offset3, dx, dy).rgb;
	
	float3 stochasticSample = sample1 * BW_vx[3].x + sample2 * BW_vx[3].y + sample3 * BW_vx[3].z;
	
	// Get standard sample for blending
	#if defined(PSHADER) || defined(CSHADER) || defined(COMPUTESHADER)
        float3 standardSample = tex.SampleBias(samplerTex, UV, SharedData::MipBias).rgb;
    #else
        float3 standardSample = tex.Sample(samplerTex, UV).rgb;
    #endif
    
    return lerp(standardSample, stochasticSample, distanceFactor);
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

// Main stochastic sampling function.
float4 StochasticSample(Texture2D<float4> tex, SamplerState samp, float2 uv, StochasticOffsets offsets, float2 dx, float2 dy, float distance = 0.0)
{
    float distanceFactor = ComputeDistanceFactor(distance);
    
    if (distanceFactor < 0.001f) {
        #if defined(PSHADER) || defined(CSHADER) || defined(COMPUTESHADER)
            return tex.SampleBias(samp, uv, SharedData::MipBias);
        #else
            return tex.Sample(samp, uv);
        #endif
    }
    
    float4 sample1 = tex.SampleGrad(samp, uv + offsets.offset1, dx, dy);
    float4 sample2 = tex.SampleGrad(samp, uv + offsets.offset2, dx, dy);
    float4 sample3 = tex.SampleGrad(samp, uv + offsets.offset3, dx, dy);
    
    float4 stochasticSample = sample1 * offsets.weights.x + 
                             sample2 * offsets.weights.y + 
                             sample3 * offsets.weights.z;
    
    // Get standard sample for blending
    #if defined(PSHADER) || defined(CSHADER) || defined(COMPUTESHADER)
        float4 standardSample = tex.SampleBias(samp, uv, SharedData::MipBias);
    #else
        float4 standardSample = tex.Sample(samp, uv);
    #endif
    
    return lerp(standardSample, stochasticSample, distanceFactor);
}

#endif  // TERRAIN_VARIATION_HLSLI