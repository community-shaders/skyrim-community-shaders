#include "Common/Color.hlsli"

Texture2D<float4> MainInputTexture      : register(t0);
Texture2D<float4> DiffuseAlbedoTexture  : register(t1);
Texture2D<float4> DiffuseGITexture      : register(t2);
Texture2D<float4> SpecularGITexture     : register(t3);

RWTexture2D<float4> MainOutputTexture   : register(u0);

cbuffer AccumulationCB : register(b2)
{
    uint AccumulatedFrames;
    float3 _padding;
}

[numthreads(8, 8, 1)]
void main(uint2 id : SV_DispatchThreadID)
{
#if defined(ACCUMULATION)
    float3 previousAccumulated = MainInputTexture[id].rgb;
    float3 currentPathTraced = DiffuseAlbedoTexture[id].rgb;

    float3 outputColor = lerp(previousAccumulated, currentPathTraced, 1.0 / (AccumulatedFrames + 1));
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
