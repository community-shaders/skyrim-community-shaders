#include "Raytracing/Denoiser/SVGF/Common.hlsli"

Texture2D<float4>   HistoryTexture  : register(t0);
Texture2D<float4>   MomentsTexture  : register(t1);
Texture2D<float4>	TemporalTexture : register(t3);
Texture2D<float>	DepthTexture    : register(t4);

RWTexture2D<float4> VarianceOutput : register(u0);

[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint2 screenSize = Resolution;
    if (DTid.x >= screenSize.x || DTid.y >= screenSize.y)
        return;

    float2 uv = float2(DTid.xy + 0.5) * ResolutionRcp;

    float4 temporalColor = TemporalTexture[DTid.xy];
    float3 blendedColor = temporalColor.xyz;
    float depthCenter = DepthTexture[DTid.xy];

    /*if (depthCenter <= 0.0f || depthCenter >= 1.0f)
    {
        VarianceOutput[DTid.xy] = temporalColor;
        return;
    }*/
    
    float history = MomentsTexture[DTid.xy].z;

    if (history <= 2) {
        float3 normalWS;
        float roughness;
        GetNormalRoughness(DTid.xy, normalWS, roughness);

        float luminanceCenter = Color::RGBToLuminance(temporalColor.xyz);
        float weightedColor = 1.f;
        float3 colorSum = temporalColor.xyz;
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
                    float4 neighborTemporalColor = TemporalTexture[p];
                    float3 neighborNormalWS;
                    float neighborRoughness;
                    GetNormalRoughness(p, neighborNormalWS, neighborRoughness);
                    float neighborLuminance = Color::RGBToLuminance(neighborTemporalColor.xyz);
                    float depthNeighbor = DepthTexture[p];

                    float weight = CalculateWeight(depthCenter, depthNeighbor, length(float2(x, y)), normalWS, neighborNormalWS, normalPhi, luminanceCenter, neighborLuminance, colorPhi);

                    weightedColor += weight;
                    colorSum += neighborTemporalColor.xyz * weight;
                    momentsSum += MomentsTexture[p].xy * weight;
                }
            }
        }

        weightedColor = max(weightedColor, 1e-5f);
        blendedColor = colorSum / weightedColor;
        momentsSum /= weightedColor;

        float variance = max(momentsSum.y - momentsSum.x * momentsSum.x, 0.0f);
        variance *= 2 / max(history, 1e-5f);
        VarianceOutput[DTid.xy] = float4(blendedColor, variance);
    }
    else
    {
        VarianceOutput[DTid.xy] = temporalColor;    
    }
}