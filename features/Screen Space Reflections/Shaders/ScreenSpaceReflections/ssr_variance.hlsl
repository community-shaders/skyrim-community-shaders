#include "ScreenSpaceReflections/ssr_common.hlsli"

Texture2D<float4> HistoryTexture : register(t0);
Texture2D<float4> MomentsTexture : register(t1);
Texture2D<float4> SSRColorTexture : register(t3);
Texture2D<float> DepthTexture : register(t4);

RWTexture2D<float4> VarianceOutput : register(u0);

cbuffer DenoiserCB : register(b2)
{
    float invMaxAccumulatedFrames;
    uint atrousIterations;
    float colorPhi;
    float normalPhi;
    float depthPhi;
    float3 pad;
};

[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 screen_size = SharedData::BufferDim.xy * FrameBuffer::DynamicResolutionParams1.xy;
    if (DTid.x >= screen_size.x || DTid.y >= screen_size.y)
        return;

    float2 uv = float2(DTid.xy + 0.5) * SharedData::BufferDim.zw * FrameBuffer::DynamicResolutionParams2.xy;

    float4 ssrColor = SSRColorTexture[DTid.xy];
    float3 blendedColor = ssrColor.xyz;
    float depthCenter = DepthTexture[DTid.xy];
    VarianceOutput[DTid.xy] = ssrColor;

    float history = MomentsTexture[DTid.xy].z;

    if (history <= 2) {
        float3 normalVS;
        float roughness;
        GetNormalRoughness(DTid.xy, normalVS, roughness);

        float luminanceCenter = Color::RGBToLuminance(ssrColor.xyz);
        float weightedColor = 1.f;
        float3 colorSum = ssrColor.xyz;
        float2 momentsSum = MomentsTexture[DTid.xy].xy;

        const int radius = 3;
        for (int y = -radius; y <= radius; y++)
        {
            for (int x = -radius; x <= radius; x++)
            {
                if (x == 0 && y == 0)
                    continue;

                const int2 p = int2(DTid.xy) + int2(x, y);
                const bool inside = (p.x >= 0 && p.y >= 0) && (p.x < screen_size.x && p.y < screen_size.y);

                if (inside)
                {
                    float4 neighborSSRColor = SSRColorTexture[p];
                    float3 neighborNormalVS;
                    float neighborRoughness;
                    GetNormalRoughness(p, neighborNormalVS, neighborRoughness);
                    float neighborLuminance = Color::RGBToLuminance(neighborSSRColor.xyz);
                    float depthNeighbor = DepthTexture[p];

                    float weight = CalculateWeight(depthCenter, depthNeighbor, length(float2(x, y)), normalVS, neighborNormalVS, normalPhi, luminanceCenter, neighborLuminance, colorPhi);

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