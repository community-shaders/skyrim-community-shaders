// TerrainVariation.hlsli
// Implements stochastic noise sampling for terrain textures
// to reduce tiling artifacts and improve visual quality

// This feature requires StochasticSampling.hlsli and uses the
// stochastic sampling methods to create more natural-looking terrain

#ifndef TERRAIN_VARIATION_HLSLI
#define TERRAIN_VARIATION_HLSLI

#include "Common/StochasticSampling.hlsli"

// Hash function for stochastic sampling
inline float2 hash2D2D(float2 s)
{
    return frac(sin(fmod(float2(dot(s, float2(127.1, 311.7)), dot(s, float2(269.5, 183.3))), 3.14159)) * 43758.5453);
}

// Stochastic sampling algorithm
inline float3 StochasticSample(Texture2D tex, SamplerState samplerTex, float2 UV)
{
    float2 skewUV = mul(float2x2(1.0, 0.0, -0.57735027, 1.15470054), UV * 3.464);
    float2 vxID = floor(skewUV);
    float3 barry = float3(frac(skewUV), 0.0);
    barry.z = 1.0 - barry.x - barry.y;

    float4x3 BW_vx = (barry.z > 0) ?
                         float4x3(float3(vxID, 0), float3(vxID + float2(0, 1), 0), float3(vxID + float2(1, 0), 0), barry.zyx) :
                         float4x3(float3(vxID + float2(1, 1), 0), float3(vxID + float2(1, 0), 0), float3(vxID + float2(0, 1), 0), float3(-barry.z, 1.0 - barry.y, 1.0 - barry.x));

    float2 dx = ddx(UV);
    float2 dy = ddy(UV);

    float3 sample1 = tex.SampleGrad(samplerTex, UV + hash2D2D(BW_vx[0].xy), dx, dy).rgb;
    float3 sample2 = tex.SampleGrad(samplerTex, UV + hash2D2D(BW_vx[1].xy), dx, dy).rgb;
    float3 sample3 = tex.SampleGrad(samplerTex, UV + hash2D2D(BW_vx[2].xy), dx, dy).rgb;

    return sample1 * BW_vx[3].x + sample2 * BW_vx[3].y + sample3 * BW_vx[3].z;
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

// Main stochastic sampling function with matching 6 parameter function signature
float4 StochasticSample(Texture2D<float4> tex, SamplerState samp, float2 uv, StochasticOffsets offsets, float2 dx, float2 dy)
{
    return StochasticSample2D(tex, samp, uv, offsets, dx, dy);
}

#endif // TERRAIN_VARIATION_HLSLI