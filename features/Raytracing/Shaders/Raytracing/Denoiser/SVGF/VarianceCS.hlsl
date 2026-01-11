#include "Raytracing/Denoiser/SVGF/Common.hlsli"

Texture2D<float4>   HistoryTexture  : register(t0);
Texture2D<float4>   MomentsTexture  : register(t1);
Texture2D<float4>	TemporalTexture : register(t3);
Texture2D<float2>   DepthTexture	: register(t4); // Viewspace Depth in R, Depth Width in G

RWTexture2D<float4> VarianceOutput  : register(u0);

#define RADIUS (3)

[numthreads(8, 8, 1)] void main(uint2 DTid : SV_DispatchThreadID)
{
    const uint2 screenSize = Resolution;

    if (any(DTid.xy >= screenSize))
        return;

    float2 uv = float2(DTid.xy + 0.5) * ResolutionRcp;

    const float4 temporalColor = TemporalTexture[DTid.xy];
    const float2 depthWidthCenter = DepthTexture[DTid.xy].xy;

    /*if (depthCenter <= FP_Z || depthCenter > SKY_Z)
    {
        VarianceOutput[DTid.xy] = temporalColor;
        return;
    }*/

    const float3 moments = MomentsTexture[DTid.xy].xyz;
    const float history = moments.z;

    const float historyThreshold = float(Frame.HistoryThreshold);

    if (history <= historyThreshold) {
        float3 normalWS;
        float roughness;
        GetNormalRoughness(DTid.xy, normalWS, roughness);

        float luminanceCenter = Color::RGBToLuminance(temporalColor.xyz);

        float weightSum = 0.f;
        float3 colorSum = temporalColor.xyz;
        float2 momentsSum = moments.xy;

        const float normalPhi = Frame.NormalPhi;
        const float colorPhi = Frame.ColorPhi;
        const float phiDepth = RADIUS * depthWidthCenter.y * Frame.DepthPhi;

        for (int y = -RADIUS; y <= RADIUS; y++)
        {
            for (int x = -RADIUS; x <= RADIUS; x++)
            {
                /*if (x == 0 && y == 0)
                    continue;*/

                const int2 samplePos = int2(DTid.xy) + int2(x, y);

                if (all(samplePos >= 0) && all(samplePos < screenSize))
                {
                    float4 neighborTemporalColor = TemporalTexture[samplePos];

                    float3 neighborNormalWS;
                    float neighborRoughness;
                    GetNormalRoughness(samplePos, neighborNormalWS, neighborRoughness);
                    float neighborLuminance = Color::RGBToLuminance(neighborTemporalColor.xyz);
                    float depthNeighbor = DepthTexture[samplePos].x;

                    float weight = CalculateWeight(depthWidthCenter.x, depthNeighbor, phiDepth, normalWS, neighborNormalWS, normalPhi, luminanceCenter, neighborLuminance, colorPhi);

                    weightSum += weight;
                    colorSum += neighborTemporalColor.xyz * weight;
                    momentsSum += MomentsTexture[samplePos].xy * weight;
                }
            }
        }

        weightSum = max(weightSum, VAR_EPSILON);

        colorSum /= weightSum;
        momentsSum /= weightSum;

        float variance = max(0.0f, momentsSum.y - momentsSum.x * momentsSum.x);
        variance *= historyThreshold / max(history, 1.0f);

        VarianceOutput[DTid.xy] = float4(colorSum, variance);
    }
    else
    {
        VarianceOutput[DTid.xy] = temporalColor;
    }
}