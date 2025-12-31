#include "Raytracing/Denoiser/SVGF/Common.hlsli"

Texture2D<float4>   HistoryTexture : register(t0);
Texture2D<float4>   MomentsTexture : register(t1);

RWTexture2D<float4> VarianceOutput : register(u0);

[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint2 screenSize = Resolution;
    if (DTid.x >= screenSize.x || DTid.y >= screenSize.y)
        return;

    float2 uv = float2(DTid.xy + 0.5) * ResolutionRcp;

    float4 ssrColor = SSRColorTexture[DTid.xy];
    float3 blendedColor = ssrColor.xyz;
    float depthCenter = DepthTexture[DTid.xy];
    VarianceOutput[DTid.xy] = ssrColor;

    float history = MomentsTexture[DTid.xy].z;

    if (history <= 2) {
        float3 normalWS;
        float roughness;
        GetNormalRoughness(DTid.xy, normalWS, roughness);

        float luminanceCenter = Color::RGBToLuminance(ssrColor.xyz);
        float weightedColor = 1.f;
        float3 colorSum = ssrColor.xyz;
        float2 momentsSum = MomentsTexture[DTid.xy].xy;

        const float normalPhi = Frame.NormalPhi;
        const float colorPhi = Frame.ColorPhi;
        
        const int radius = 3;
        for (int y = -radius; y <= radius; y++)
        {
            for (int x = -radius; x <= radius; x++)
            {
                if (x == 0 && y == 0)
                    continue;

                const int2 p = int2(DTid.xy) + int2(x, y);
                const bool inside = (p.x >= 0 && p.y >= 0) && (p.x < screenSize.x && p.y < screenSize.y);

                if (inside)
                {
                    float4 neighborSSRColor = SSRColorTexture[p];
                    float3 neighborNormalWS;
                    float neighborRoughness;
                    GetNormalRoughness(p, neighborNormalWS, neighborRoughness);
                    float neighborLuminance = Color::RGBToLuminance(neighborSSRColor.xyz);
                    float depthNeighbor = DepthTexture[p];

                    float weight = CalculateWeight(depthCenter, depthNeighbor, length(float2(x, y)), normalWS, neighborNormalWS, normalPhi, luminanceCenter, neighborLuminance, colorPhi);

                    weightedColor += weight;
                    colorSum += neighborSSRColor.xyz * weight;
                    momentsSum += MomentsTexture[p].xy * weight;
                }
            }
        }

        weightedColor = max(weightedColor, 1e-5f);
        blendedColor = colorSum / weightedColor;
        momentsSum /= weightedColor;

        float variance = momentsSum.y - (momentsSum.x * momentsSum.x);
        variance *= 2 / history;
        VarianceOutput[DTid.xy] = float4(blendedColor, variance);
    }
}