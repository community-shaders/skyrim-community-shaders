#include "Common/Color.hlsli"

Texture2D<float4> GeometryNormalDepthTexture: register(t0, space0);
Texture2D<float4> AlbedoTexture             : register(t1, space0);
Texture2D<float4> ReflectanceTexture        : register(t2, space0);

RWTexture2D<float4> FinalTexture            : register(u0);
RWTexture2D<float4> DiffuseOutputTexture    : register(u1);
//RWTexture2D<float4> SpecularOutputTexture   : register(u2);
RWTexture2D<float> SpecHitDistanceTexture   : register(u3);
RWTexture2D<unorm float> DepthTexture       : register(u4);

[numthreads(8, 8, 1)]
void main(uint2 id : SV_DispatchThreadID)
{
    float4 diffuseGI = DiffuseOutputTexture[id];
    FinalTexture[id] = float4(FinalTexture[id].rgb + Color::LinearToGamma(diffuseGI.rgb), 1.0f);
    SpecHitDistanceTexture[id] = diffuseGI.a;
    DepthTexture[id] = GeometryNormalDepthTexture[id].a;
}