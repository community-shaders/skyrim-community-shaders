#include "ScreenSpaceReflections/ssr_common.hlsli"

RWTexture2D<float> DepthOutput : register(u0);

Texture2D<float> DepthTexture : register(t4);

[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID)
{
    DepthOutput[DTid.xy] = DepthTexture[DTid.xy];
}