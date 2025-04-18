// Stochastic sampling implementation for texture tiling reduction
// Based on papers "Rendering Antialiased Edges with Barycentric Textures" and "Tiled Stochastic Texture" by Thomas Deliot & Eric Heitz.

#ifndef STOCHASTIC_SAMPLING_HLSLI
#define STOCHASTIC_SAMPLING_HLSLI

#include "Common/SharedData.hlsli"
#include "Common/Random.hlsli"

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
    defaults.weights = float3(1, 0, 0); // Only use the first sample
    return defaults;
}

// Sample texture with stochastic offsets
inline float4 SampleWithOffsets(Texture2D tex, SamplerState samplerTex, float2 UV, StochasticOffsets offsets, float2 dx, float2 dy)
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
        float4 sample1 = tex.SampleGrad(samplerTex, UV + offsets.offset1, dx, dy);
        float4 sample2 = tex.SampleGrad(samplerTex, UV + offsets.offset2, dx, dy);
        float4 sample3 = tex.SampleGrad(samplerTex, UV + offsets.offset3, dx, dy);
        return sample1 * offsets.weights.x + sample2 * offsets.weights.y + sample3 * offsets.weights.z;
    }
    else {
        // Fall back to standard sampling when the feature is disabled
        return tex.SampleGrad(samplerTex, UV, dx, dy);
    }
}

// Universal wrapper function that handles both standard and stochastic sampling
inline float4 TerrainTextureSample(Texture2D tex, SamplerState samplerTex, float2 UV, StochasticOffsets offsets, float2 dx, float2 dy)
{
    return SampleWithOffsets(tex, samplerTex, UV, offsets, dx, dy);
}

// Main StochasticSample2D function that does the actual sampling
float4 StochasticSample2D(Texture2D<float4> tex, SamplerState samp, float2 uv, StochasticOffsets offsets, float2 dx, float2 dy)
{
    // Sample texture with offsets using explicit gradients for correct mip level selection
    float4 sample1 = tex.SampleGrad(samp, uv + offsets.offset1, dx, dy);
    float4 sample2 = tex.SampleGrad(samp, uv + offsets.offset2, dx, dy);
    float4 sample3 = tex.SampleGrad(samp, uv + offsets.offset3, dx, dy);
    
    // Blend samples using barycentric weights
    return sample1 * offsets.weights.x + 
           sample2 * offsets.weights.y + 
           sample3 * offsets.weights.z;
}

#endif // __STOCHASTIC_SAMPLING_HLSLI__