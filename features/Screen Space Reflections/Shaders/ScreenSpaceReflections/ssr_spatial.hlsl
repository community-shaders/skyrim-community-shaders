#include "ScreenSpaceReflections/ssr_common.hlsli"

Texture2D<float4> HistoryTexture : register(t0);
Texture2D<float4> SSRColorTexture : register(t3);
Texture2D<float> DepthTexture : register(t4);

RWTexture2D<float4> FilteredOutput : register(u0);

cbuffer DenoiserCB : register(b2)
{
    float invMaxAccumulatedFrames;
    uint atrousIterations;
    float colorPhi;
    float normalPhi;
    float depthPhi;
    float3 pad;
};

float GaussianBlur(uint2 id)
{
    float sum = 0.f;
    float kernelSum = 0.f;
    const float kernel[2][2] =
    {
        { 1.0 / 4.0, 1.0 / 8.0 },
        { 1.0 / 8.0, 1.0 / 16.0 }
    };
    
    const int radius = 1;
    
    for (int y = -radius; y <= radius; y++)
    {
        for (int x = -radius; x <= radius; x++)
        {
            const int2 p = id + int2(x, y);
            const bool inside = (p.x >= 0 && p.y >= 0) && (p.x < SharedData::BufferDim.x * FrameBuffer::DynamicResolutionParams1.x && p.y < SharedData::BufferDim.y * FrameBuffer::DynamicResolutionParams1.y);

            if (inside)
            {
                const float k = kernel[abs(x)][abs(y)];
                kernelSum += k;
                sum += SSRColorTexture[p].w * k;
            }
        }
    }

    return sum / kernelSum;
}

static const float kernelWeights[3] = { 1.0, 2.0 / 3.0, 1.0 / 6.0 };

#define VAR_EPSILON 0.00001f

// Spatiotemporal Variance-Guided Filter
[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 screen_size = SharedData::BufferDim.xy * FrameBuffer::DynamicResolutionParams1.xy;
    if (DTid.x >= screen_size.x || DTid.y >= screen_size.y)
        return;

    float2 uv = float2(DTid.xy + 0.5) * SharedData::BufferDim.zw * FrameBuffer::DynamicResolutionParams2.xy;

    float3 blendedColor = 0;
    float4 historyColor = HistoryTexture[DTid.xy];
    float4 ssrColor = SSRColorTexture[DTid.xy];
    float depthCenter = DepthTexture[DTid.xy];

    float3 normalVS;
    float roughness;
    GetNormalRoughness(DTid.xy, normalVS, roughness);
    roughness = clamp(roughness, 0.001f, 1.0f);

    float luminanceCenter = Color::RGBToLuminance(ssrColor.rgb);
    float variance = GaussianBlur(DTid.xy);

    if (depthCenter > 0)
    {
        float phiLuminance = max(colorPhi * sqrt(abs(variance) + VAR_EPSILON), VAR_EPSILON);
        float phiNormal = normalPhi;
#if defined(SSSR_SPECULAR)
        phiLuminance *= roughness;
        phiNormal /= roughness;
#endif
        float phiDepth = depthPhi * (atrousIterations + 1);
        float weightSum = 0.f;

        for (int ky = -2; ky <= 2; ky++)
        {
            for (int kx = -2; kx <= 2; kx++)
            {
                int2 samplePos = int2(DTid.xy) + int2(kx, ky) * (atrousIterations + 1);
                bool inside = (samplePos.x >= 0 && samplePos.y >= 0) && (samplePos.x < screen_size.x && samplePos.y < screen_size.y);
                if (inside)
                {
                    float4 sampleSSRColor = SSRColorTexture[samplePos];
                    float sampleDepth = DepthTexture[samplePos];
                    if (sampleDepth > 0)
                    {
                        float3 sampleNormalVS;
                        float sampleRoughness;
                        GetNormalRoughness(samplePos, sampleNormalVS, sampleRoughness);

                        float luminanceP = Color::RGBToLuminance(sampleSSRColor.rgb);
                        float weight = CalculateWeight(depthCenter, sampleDepth, phiDepth, normalVS, sampleNormalVS, phiNormal, luminanceCenter, luminanceP, phiLuminance) * kernelWeights[abs(kx)] * kernelWeights[abs(ky)];

                        blendedColor += sampleSSRColor.rgb * weight;
                        weightSum += weight;
                    }
                }
            }
        }
        if (weightSum > 0.f)
        {
            blendedColor /= weightSum;
        }
        else
        {
            blendedColor = ssrColor.rgb;
        }
    }

    FilteredOutput[DTid.xy] = float4(blendedColor, 1.0);
}