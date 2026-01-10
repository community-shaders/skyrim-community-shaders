#include "Raytracing/Denoiser/SVGF/Common.hlsli"

Texture2D<float4>   HistoryTexture          : register(t0);
Texture2D<float2>   MotionVectorTexture     : register(t1);
Texture2D<float4>	NoisyInputTexture		: register(t3);
Texture2D<float4>   HistoryMomentsTexture   : register(t4); // moments in RG, frame count in B
Texture2D<float4>   HistoryDepthTexture     : register(t5);
Texture2D<float4>   HistoryNormalsTexture   : register(t6);
//Texture2D<float>    DepthTexture            : register(t7);

RWTexture2D<float4> FilteredOutput          : register(u0);
RWTexture2D<float4> MomentsOutput           : register(u1);
RWTexture2D<float2> DepthOutput             : register(u2); // Screen Depth in R, Viewspace Depth in G

bool IsValidHistory(in uint2 pixel, in float2 uv, in float currDepth, in float3 currNormalWS)
{
    const uint2 screenSize = Resolution;
    
    if (any(uv < 0.0f) || any(uv > 1.0f))
        return false;

    if (any(pixel >= screenSize))
        return false;

    float3 prevNormalWS;
    float roughness;
    GetNormalRoughness(HistoryNormalsTexture, pixel, prevNormalWS, roughness);

    if (dot(currNormalWS, prevNormalWS) < Frame.NormalThreshold) // cos
        return false;

    float prevDepth = HistoryDepthTexture[pixel].x;
    float depthDiff = abs(currDepth - prevDepth) / currDepth;
    
    if (depthDiff > Frame.DepthThreshold) // difference %
        return false;

    return true;
}

[numthreads(8, 8, 1)] 
void main(uint2 id : SV_DispatchThreadID)
{
    const uint2 screenSize = Resolution;
    if (any(id.xy >= screenSize))
        return;

    const float2 uv = float2(id.xy + 0.5) * ResolutionRcp;
    
    const float4 inputColor = NoisyInputTexture[id.xy];
    const float2 depth = DepthOutput[id.xy].xy;
    
    float depthCenter = depth.x;  
    //float depthCenter = DepthTexture[id.xy];
    
    float3 normalWS;
    float roughness;
    GetNormalRoughness(id.xy, normalWS, roughness);

    // Reproject UVs using motion vectors
    float2 prevUV = ReprojectUVSimple(MotionVectorTexture, uv);
    //float2 prevUV = ReprojectUV(MotionVectorTexture, uv, depthCenter, 0u);
    
    float4 prevColor = 0.f;
    float prevAccumFrames = 0.f;
    float2 prevMoments = float2(0.f, 0.f);
    uint2 prevPixel = uint2(prevUV * screenSize);
    
    bool valid = IsValidHistory(prevPixel, prevUV, depthCenter, normalWS);

    if (valid)
    {
        prevColor = HistoryTexture[prevPixel];
        
        const float3 historyMoments = HistoryMomentsTexture[prevPixel].xyz;     
        prevAccumFrames = historyMoments.z;
        prevMoments = historyMoments.xy;
    }

    float curAccumFrames = min(64.0f, valid ? prevAccumFrames + 1.0f : 1.0f);
    
    float invPrevAccumFrames = 1.0f / curAccumFrames;
    
    float alpha = valid ? max(Frame.Alpha, invPrevAccumFrames) : 1.0f;
    float momentAlpha = valid ? max(Frame.MomentsAlpha, invPrevAccumFrames) : 1.0f;
        
    float luminance = Color::RGBToLuminance(inputColor.rgb);
    float2 curMoment = float2(luminance, luminance * luminance);      
    
    float3 blendedColor = lerp(prevColor.rgb, inputColor.rgb, alpha);     
    float2 blendedMoment = lerp(prevMoments, curMoment, momentAlpha);
    
    float variance = max(0.0f, blendedMoment.y - blendedMoment.x * blendedMoment.x);
                   
    FilteredOutput[id.xy] = float4(blendedColor, variance); 
    MomentsOutput[id.xy] = float4(blendedMoment, curAccumFrames, 0.f);   
    
    // Build depth width
    float depthL = DepthOutput[id.xy + int2(-1, 0)].x;
    float depthR = DepthOutput[id.xy + int2(1, 0)].x;
    
    float depthU = DepthOutput[id.xy + int2(0, 1)].x;
    float depthD = DepthOutput[id.xy + int2(0, -1)].x;
    
    float depthW = abs(depthCenter - depthL) + abs(depthCenter - depthL) + abs(depthCenter - depthU) + abs(depthCenter - depthD);    
    
    DepthOutput[id.xy] = float2(depthCenter, depthW * 0.5f);
}