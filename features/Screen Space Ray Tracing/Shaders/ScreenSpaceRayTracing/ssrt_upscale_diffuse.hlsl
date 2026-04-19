#include "ScreenSpaceRayTracing/ssrt_common.hlsli"

Texture2D<float4> SSRTDiffuseTexture : register(t0);
Texture2D<float2> AverageDirectionTexture : register(t1);
Texture2D<float> halfResDepth : register(t2);

RWTexture2D<float4> outSH0 : register(u0);
RWTexture2D<float4> outSH1 : register(u1);
RWTexture2D<float4> outSH2 : register(u2);
RWTexture2D<float4> outSH3 : register(u3);

cbuffer SSRTCB : register(b1)
{
    uint MaxSteps;
    uint MaxMips;
    uint UseDynamicCubemapsAsFallback;
    float Thickness;
    float NormalBias;
    float BRDFBias;
    float OcclusionStrength;
    float CubemapNormalization;

    float2 TexDim;
    float2 RcpTexDim;
    float2 FrameDim;
    float2 RcpFrameDim;
};

// SH2 basis functions
float4 SH2Basis(float3 dir)
{
    return float4(0.282095f, 0.488603f * dir.y, 0.488603f * dir.z, 0.488603f * dir.x);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 outputRes = FrameDim / 2;
    if (any(DTid.xy >= outputRes)) return;

    float centerDepth = halfResDepth[DTid.xy];

    // We are at 1/2 res. Map to 1/4 res neighborhood.
    // 1/2 res pixel (x,y) -> 1/4 res pixel (x/2, y/2)
    int2 centerCoord = DTid.xy / 2;

    float3 accSH0 = 0;
    float3 accSH1 = 0;
    float3 accSH2 = 0;
    float3 accSH3 = 0;
    float accWeight = 0;
    float accAO = 0;

    // 3x3 neighborhood in 1/4 resolution
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            int2 tapCoord = centerCoord + int2(x, y);
            if (any(tapCoord < 0) || any(tapCoord >= (int2)(FrameDim / 4))) continue;

            float4 radiance = SSRTDiffuseTexture[tapCoord];
            float2 encDir = AverageDirectionTexture[tapCoord];
            
            if (radiance.w <= 0) continue;

            float tapDepth = halfResDepth[tapCoord * 2]; // Match 1/4 tap to 1/2 res depth
            float depthWeight = exp(-abs(centerDepth - tapDepth) * 0.1); // Simple bilateral depth weight

            float3 dir = Octahedral::Decode(encDir);
            float3 color = radiance.xyz;
            float weight = depthWeight;

            float4 sh = SH2Basis(dir);
            
            accSH0 += color * sh.x * weight;
            accSH1 += color * sh.y * weight;
            accSH2 += color * sh.z * weight;
            accSH3 += color * sh.w * weight;
            accWeight += weight;
            accAO += radiance.w * weight;
        }
    }

    if (accWeight > 0)
    {
        float invWeight = 1.0 / accWeight;
        outSH0[DTid.xy] = float4(accSH0 * invWeight, accAO * invWeight);
        outSH1[DTid.xy] = float4(accSH1 * invWeight, 1.0);
        outSH2[DTid.xy] = float4(accSH2 * invWeight, 1.0);
        outSH3[DTid.xy] = float4(accSH3 * invWeight, 1.0);
    }
    else
    {
        outSH0[DTid.xy] = 0;
        outSH1[DTid.xy] = 0;
        outSH2[DTid.xy] = 0;
        outSH3[DTid.xy] = 0;
    }
}
