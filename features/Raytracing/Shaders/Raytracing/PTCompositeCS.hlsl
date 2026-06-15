#include "Raytracing/Includes/Common.hlsli"

Texture2D<float4> MainInput : register(t0);
Texture2D<float4> MotionVectorInput : register(t1);

RWTexture2D<float4> MainOutput : register(u0);
RWTexture2D<float2> MotionVectorOutput : register(u1);

[numthreads(8, 8, 1)]
void main(uint2 id : SV_DispatchThreadID)
{
    if (any(id >= DynamicResolution))
        return;
    
    const float4 mainPT = MainInput[id.xy];
    const float blend = mainPT.a;
    
    // Main
    const float4 mainRaster = MainOutput[id.xy];
    const float3 mainFinal = lerp(mainRaster.rgb, mainPT.rgb, blend);
    MainOutput[id.xy] = float4(mainFinal, mainRaster.a);

    // Motion Vector
    const float2 mvRaster = MotionVectorOutput[id.xy];
    const float4 mvPT = MotionVectorInput[id.xy];
    const float2 mvFinal = lerp(mvRaster.rg, mvPT.rg, blend);
    MotionVectorOutput[id.xy] = mvFinal.rg;
}