#include "Common/SharedData.hlsli"

Texture2D<float3> tDiffuse : register(t0);
Texture2D<float3> tOriginal : register(t1);
Texture2D<float3> tOriginalMip : register(t2);

RWTexture2D<float3> FinalRW : register(u0);

SamplerState LinearSampler : register(s0);

cbuffer ExposureFusionCB : register(b0)
{
    float4 exposureFusionParams; // x = exposure, y = shadows, z = highlights, w = sigma^2
}

float3 ACESFilmicToneMapping(float3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x*(a*x+b))/(x*(c*x+d)+e));
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
    uint2 texDim;
    FinalRW.GetDimensions(texDim.x, texDim.y);
    
    if (dispatchID.x >= texDim.x || dispatchID.y >= texDim.y)
        return;
    
    float2 vUv = (float2(dispatchID.xy) + 0.5) / float2(texDim);
    
    uint2 texDim2;
    tOriginalMip.GetDimensions(texDim2.x, texDim2.y);

    float4 mipPixelSize = float4(
        texDim2.x,
        texDim2.y,
        1.0 / texDim2.x,
        1.0 / texDim2.y);

    // Guided upsampling.
    // See https://bartwronski.com/2019/09/22/local-linear-models-guided-filter/
    float momentX = 0.0;
    float momentY = 0.0;
    float momentX2 = 0.0;
    float momentXY = 0.0;
    float ws = 0.0;
    for (int dy = -1; dy <= 1; dy += 1) {
        for (int dx = -1; dx <= 1; dx += 1) {
            float x = tOriginalMip.SampleLevel(LinearSampler, vUv + float2(dx, dy) * mipPixelSize.zw, 0).y;
            float y = tDiffuse.SampleLevel(LinearSampler, vUv + float2(dx, dy) * mipPixelSize.zw, 0).x;
            float w = exp(-0.5 * float(dx*dx + dy*dy) / (0.7*0.7));
            momentX += x * w;
            momentY += y * w;
            momentX2 += x * x * w;
            momentXY += x * y * w;
            ws += w;
        }
    }
    momentX /= ws;
    momentY /= ws;
    momentX2 /= ws;
    momentXY /= ws;
    float A = (momentXY - momentX * momentY) / (max(momentX2 - momentX * momentX, 0.0) + 0.00001);
    float B = momentY - A * momentX;
    
    // Apply local exposure adjustment as a crude multiplier on all RGB channels.
    // This is... generally pretty wrong, but enough for the demo purpose.
    float3 baseColor = tOriginal.SampleLevel(LinearSampler, vUv, 0) * exposureFusionParams.x;

    float3 texel = tDiffuse.SampleLevel(LinearSampler, vUv, 0).x;
    float3 texelOriginal = sqrt(max(ACESFilmicToneMapping(baseColor), 0.0));
    float luminance = dot(texelOriginal.xyz, float3(0.1,0.7,0.2)) + 0.00001;
    float finalMultiplier = max(A * luminance + B, 0.0) / luminance;
    // This is a hack to prevent super dark pixels getting boosted by a lot and showing compression artifacts.
    float lerpToUnityThreshold = 0.007;
    finalMultiplier = luminance > lerpToUnityThreshold ? finalMultiplier : 
        lerp(1.0, finalMultiplier, (luminance / lerpToUnityThreshold) * (luminance / lerpToUnityThreshold));
    float3 texelFinal = sqrt(max(ACESFilmicToneMapping(baseColor * finalMultiplier), 0.0));
    FinalRW[dispatchID.xy] = texelFinal;
}