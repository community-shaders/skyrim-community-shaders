#include "ScreenSpaceReflections/ssr_common.hlsli"

RWTexture2D<float4> ColorCombinedOutput : register(u0);

[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID)
{
    float4 color = float4(DiffuseTexture[DTid.xy].xyz + SpecularTexture[DTid.xy].xyz, 1.0f);
    ColorCombinedOutput[DTid.xy] = color;
}