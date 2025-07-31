#include "Common/SharedData.hlsli"
#include "Common/Math.hlsli"
#include "Common/Color.hlsli"

Texture2D<float3> OriginalTexture : register(t0);
RWTexture2D<float3> LuminanceRW : register(u0);
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
    uint2 fineDim;
    LuminanceRW.GetDimensions(fineDim.x, fineDim.y);
    
    if (dispatchID.x >= fineDim.x || dispatchID.y >= fineDim.y)
        return;

    float2 vUv = (float2(dispatchID.xy) + 0.5) / float2(fineDim);

    float3 inpColor = OriginalTexture.SampleLevel(LinearSampler, vUv, 0) * exposureFusionParams.x;
    
    float highlights = sqrt(dot(saturate(ACESFilmicToneMapping(inpColor * exposureFusionParams.z)), float3(0.1,0.7,0.2)));
    float midtones = sqrt(dot(saturate(ACESFilmicToneMapping(inpColor)), float3(0.1,0.7,0.2)));
    float shadows = sqrt(dot(saturate(ACESFilmicToneMapping(inpColor * exposureFusionParams.y)), float3(0.1,0.7,0.2)));
    
    LuminanceRW[dispatchID.xy] = float3(highlights, midtones, shadows);
}