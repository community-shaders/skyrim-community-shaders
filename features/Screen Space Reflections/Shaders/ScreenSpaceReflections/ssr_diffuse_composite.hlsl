#include "Common/Color.hlsli"
#include "Common/SharedData.hlsli"

Texture2D<float4> SSRDiffuseTexture : register(t0);
Texture2D<float4> AlbedoTexture : register(t1);

RWTexture2D<float4> ColorTextureRW : register(u0);

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID)
{
    uint width, height;
    ColorTextureRW.GetDimensions(width, height);
    if (dispatchID.x >= width || dispatchID.y >= height)
        return;

    float4 ssrDiffuse = SSRDiffuseTexture[dispatchID.xy];
    float4 albedo = AlbedoTexture[dispatchID.xy];
    float4 originalColor = ColorTextureRW[dispatchID.xy];

    float3 color = Color::LinearToGamma(ssrDiffuse.xyz * Color::GammaToLinear(albedo.xyz) + Color::GammaToLinear(originalColor.xyz));
    ColorTextureRW[dispatchID.xy] = float4(color, originalColor.w);
}