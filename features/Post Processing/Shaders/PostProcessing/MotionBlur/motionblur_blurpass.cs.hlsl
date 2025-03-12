/**
 * Motion Blur - Blur Pass (Pass 3 of 3)
 * 
 * Final pass that applies motion blur using neighborhood velocities
 * Uses depth-aware sampling to prevent foreground/background bleeding
 */

#include "PostProcessing/common.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/MotionBlur.hlsli"

// Textures and buffers
Texture2D<float4> TexColor : register(t0);     // Color buffer
Texture2D<float4> TexVelocity : register(t1);  // Motion vectors
Texture2D<float4> TexNeighborMax : register(t2); // Neighborhood velocities (20×20 grid)
Texture2D<float> TexDepth : register(t3);      // Depth buffer
RWTexture2D<float4> RWTexOut : register(u0);   // Output

// Samplers
SamplerState LinearSampler : register(s0);
SamplerState PointSampler : register(s1);

// Constants
cbuffer MotionBlurCB : register(b0)
{
    float g_VelocityScale;   // Velocity multiplier (mapped from enum)
    int g_SampleCount;       // Number of samples
}

// Fixed parameters
static const uint GRID_SIZE = 20; // Fixed grid size
static const float g_MaxBlurRadius = 40.0f;
#define PI 3.14159265359f
#define MB_SOFTZ_INCHES 1.0f

// Extract velocity from encoded color
float2 ExtractVelocity(float4 colorSample)
{
    // Direction from R,G channels
    float2 dir;
    dir.x = (colorSample.r * 2.0f) - 1.0f;
    dir.y = (colorSample.g * 2.0f) - 1.0f;
    
    float dirLength = length(dir);
    if (dirLength > 0.001f)
        dir /= dirLength;
    
    // Magnitude from B channel
    float magnitude = colorSample.b * 20.0f;
    
    return dir * magnitude;
}

// Depth comparison - returns (foreground, background) weights
float2 DepthCmp(float centerDepth, float sampleDepth, float depthScale)
{
    return saturate(0.5f + float2(depthScale, -depthScale) * (sampleDepth - centerDepth));
}

// Spread comparison
float2 SpreadCmp(float offsetLen, float2 spreadLen, float pixelToSampleUnitsScale)
{
    return saturate(pixelToSampleUnitsScale * spreadLen - max(offsetLen - 1.0f, 0.0f));
}

// Calculate sample weight
float SampleWeight(
    float centerDepth, float sampleDepth, float offsetLen, float centerSpreadLen, 
    float sampleSpreadLen, float pixelToSampleUnitsScale, float depthScale)
{
    float2 depthCmp = DepthCmp(centerDepth, sampleDepth, depthScale);
    float2 spreadCmp = SpreadCmp(offsetLen, float2(centerSpreadLen, sampleSpreadLen), pixelToSampleUnitsScale);
    return dot(depthCmp, spreadCmp);
}

// Generate dithered offset
float GetDitheredOffset(uint2 position, float sampleIndex)
{
    float scale = 0.25f;
    uint2 positionMod = position & 1; // 2x2 checkerboard
    return (-scale + 2.0f * scale * positionMod.x) * (-1.0f + 2.0f * positionMod.y);
}

// Main function
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // Get dimensions and check bounds
    uint2 dimensions;
    TexColor.GetDimensions(dimensions.x, dimensions.y);
    
    if (DTid.x >= dimensions.x || DTid.y >= dimensions.y)
        return;
    
    uint2 pixelPos = DTid.xy;
    
    // Sample center pixel data
    float2 texCoord = (pixelPos + 0.5f) / float2(dimensions);
    float4 centerColor = TexColor.SampleLevel(LinearSampler, texCoord, 0);
    float centerDepth = TexDepth.SampleLevel(PointSampler, texCoord, 0);
    float2 centerVelocity = TexVelocity.SampleLevel(PointSampler, texCoord, 0).xy;
    
    centerVelocity *= g_VelocityScale;
    
    // Calculate tile coordinates
    uint2 gridCoord;
    gridCoord.x = (pixelPos.x * GRID_SIZE) / dimensions.x;
    gridCoord.y = (pixelPos.y * GRID_SIZE) / dimensions.y;
    
    uint2 neighborMaxDimensions;
    TexNeighborMax.GetDimensions(neighborMaxDimensions.x, neighborMaxDimensions.y);
    
    gridCoord = min(gridCoord, neighborMaxDimensions - 1);
    
    // Get max velocity from grid
    float4 neighborMaxSample = TexNeighborMax[gridCoord];
    float2 neighborMaxVelocity = ExtractVelocity(neighborMaxSample);
    
    // Determine blur direction and length
    float2 blurDir = length(neighborMaxVelocity) > 0.001f ? normalize(neighborMaxVelocity) : float2(0.0f, 0.0f);
    // Scale blur length down for higher velocity scales to prevent over-blurring
    float blurLength = min(length(neighborMaxVelocity) / sqrt(g_VelocityScale/300.0f), g_MaxBlurRadius);
    
    // Skip if no motion
    if (blurLength < 0.5f)
    {
        RWTexOut[pixelPos] = centerColor;
        return;
    }
    
    float centerVelocityLen = length(centerVelocity);

    // Initialize for sampling
    float4 sum = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float pixelToSampleUnitsScale = float(g_SampleCount) / blurLength;
    
    // Sample in pairs (mirrored)
    for (int i = 0; i < g_SampleCount / 2; i++)
    {
        // Calculate offset
        float offset = (float(i) + 0.5f) / float(g_SampleCount / 2) * blurLength;
        offset += GetDitheredOffset(pixelPos, i);
        
        // Sample pairs in opposite directions
        float2 pixelOffsetFwd = blurDir * offset;
        float2 pixelOffsetBck = -pixelOffsetFwd;
        
        float2 sampleTexCoordsFwd = (pixelPos + pixelOffsetFwd + 0.5f) / float2(dimensions);
        float2 sampleTexCoordsBck = (pixelPos + pixelOffsetBck + 0.5f) / float2(dimensions);
        
        // Sample depth and velocity
        float sampleDepthFwd = TexDepth.SampleLevel(PointSampler, sampleTexCoordsFwd, 0);
        float sampleDepthBck = TexDepth.SampleLevel(PointSampler, sampleTexCoordsBck, 0);
        
        float4 rawVelocityDepthFwd = TexVelocity.SampleLevel(PointSampler, sampleTexCoordsFwd, 0);
        float4 rawVelocityDepthBck = TexVelocity.SampleLevel(PointSampler, sampleTexCoordsBck, 0);
        
        float2 sampleVelocityFwd = rawVelocityDepthFwd.xy * g_VelocityScale;
        float2 sampleVelocityBck = rawVelocityDepthBck.xy * g_VelocityScale;
        
        float sampleVelocityLenFwd = length(sampleVelocityFwd);
        float sampleVelocityLenBck = length(sampleVelocityBck);
        
        float offsetLen = offset;
        
        // Calculate sample weights
        float weightFwd = SampleWeight(
            centerDepth, sampleDepthFwd, offsetLen, centerVelocityLen, sampleVelocityLenFwd,
            pixelToSampleUnitsScale, MB_SOFTZ_INCHES);
            
        float weightBck = SampleWeight(
            centerDepth, sampleDepthBck, offsetLen, centerVelocityLen, sampleVelocityLenBck,
            pixelToSampleUnitsScale, MB_SOFTZ_INCHES);
        
        // Sample colors and accumulate
        float4 sampleColorFwd = TexColor.SampleLevel(LinearSampler, sampleTexCoordsFwd, 0);
        float4 sampleColorBck = TexColor.SampleLevel(LinearSampler, sampleTexCoordsBck, 0);
        
        sum += weightFwd * float4(sampleColorFwd.rgb, 1.0f);
        sum += weightBck * float4(sampleColorBck.rgb, 1.0f);
    }
    
    // Normalize
    sum.rgb *= 1.0f / float(g_SampleCount);
    sum.w *= 1.0f / float(g_SampleCount);
    
    // Final blend with background
    float4 outputColor = float4(
        sum.rgb + (1.0f - sum.w) * centerColor.rgb,
        1.0f
    );
    
    // Write output
    RWTexOut[pixelPos] = outputColor;
}
