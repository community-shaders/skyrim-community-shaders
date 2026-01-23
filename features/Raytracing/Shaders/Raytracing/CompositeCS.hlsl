#include "Common/Color.hlsli"

Texture2D<float4> MainInputTexture      : register(t0);
Texture2D<float4> DiffuseAlbedoTexture  : register(t1);
Texture2D<float4> DiffuseGITexture      : register(t2);
Texture2D<float4> SpecularGITexture     : register(t3);

RWTexture2D<float4> MainOutputTexture   : register(u0);

cbuffer AccumulationCB : register(b2)
{
    float AccumulationWeight;  // 1.0 / (accumulatedFrames + 1)
    float3 _padding;
}

[numthreads(8, 8, 1)]
void main(uint2 id : SV_DispatchThreadID)
{
#if defined(ACCUMULATION)
    // Accumulation denoiser mode: accumulate path tracing results only
    // MainInputTexture (t0) = previous accumulated path tracing result (from dedicated accumulation buffer)
    // DiffuseAlbedoTexture (t1) = current frame's path tracing result
    float3 previousAccumulated = MainInputTexture[id].rgb;  // Previous accumulated PT result (t0)
    float3 currentPathTraced = DiffuseAlbedoTexture[id].rgb;  // Current frame's PT result (t1)
    
    // Weighted average: newAccum = prevAccum * (1 - weight) + current * weight
    float3 outputColor = lerp(previousAccumulated, currentPathTraced, AccumulationWeight);
#elif defined(COMPOSITE)
    float3 outputColor = Color::GammaToTrueLinear(MainInputTexture[id].rgb);

#   if defined(DIFFUSE)
    outputColor += DiffuseAlbedoTexture[id].rgb * DiffuseGITexture[id].rgb;
#   endif // DIFFUSE

#   if defined(SPECULAR)
    outputColor += SpecularGITexture[id].rgb;
#   endif // SPECULAR
#else
    float3 outputColor = DiffuseGITexture[id].rgb;
#endif // COMPOSITE

#if defined(GAMMA_OUTPUT)
    outputColor = Color::TrueLinearToGamma(outputColor);
#endif // GAMMA_OUTPUT

    MainOutputTexture[id] = float4(outputColor, 1.0f);
}
