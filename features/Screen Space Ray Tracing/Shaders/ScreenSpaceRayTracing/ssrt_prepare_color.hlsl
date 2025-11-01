#include "ScreenSpaceRayTracing/ssrt_common.hlsli"

Texture2D<float4> DiffuseTexture : register(t0);
Texture2D<float3> SpecularTexture : register(t1);

RWTexture2D<float4> ColorCombinedOutput : register(u0);

[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID)
{
    float4 color = float4(Color::LinearToGamma(Color::GammaToLinear(DiffuseTexture[DTid.xy].xyz) + SpecularTexture[DTid.xy].xyz), 1.0f);
    ColorCombinedOutput[DTid.xy] = color;
}