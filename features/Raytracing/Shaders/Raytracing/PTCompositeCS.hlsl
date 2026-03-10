#include "Raytracing/Includes/Common.hlsli"

Texture2D<float4> PathTracedInput : register(t0);
RWTexture2D<float4> MainOutput    : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 id : SV_DispatchThreadID)
{
    if (any(id >= DynamicResolution))
        return;
    
    float4 main = MainOutput[id.xy];
    float4 pt = PathTracedInput[id.xy];
    
    float3 final = lerp(main.rgb, pt.rgb, pt.a);
    
    MainOutput[id.xy] = float4(final, main.a);
}