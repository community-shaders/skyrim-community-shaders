#include "ScreenSpaceRayTracing/ssrt_common.hlsli"
#include "NRD/NRDReblurSH.hlsli"

Texture2D<float4> SSRTDiffuseTexture    : register(t0);
Texture2D<float2> AverageDirectionTexture : register(t1);
Texture2D<float>  halfResNDCDepth        : register(t2);
Texture2D<float>  quarterResDepth        : register(t3);

RWTexture2D<float4> outSH0 : register(u0);  // IN_DIFF_SH0
RWTexture2D<float4> outSH1 : register(u1);  // IN_DIFF_SH1

cbuffer SSRTCB : register(b1)
{
    uint  MaxSteps;
    uint  MaxMips;
    uint  UseDynamicCubemapsAsFallback;
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

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 outputRes = (uint2)(FrameDim / 2);
    if (any(DTid.xy >= outputRes)) return;

    // Map 1/2-res pixel to 1/4-res neighborhood center
    int2 centerCoord = DTid.xy / 2;

    float3 accRadiance  = 0;
    float3 accDirection = 0;
    float  accAO        = 0;
    float  accWeight    = 0;

    // 3x3 gather in 1/4-res
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            int2 tapCoord = centerCoord + int2(x, y);
            if (any(tapCoord < 0) || any(tapCoord >= (int2)(FrameDim / 4))) continue;

            float4 radiance = SSRTDiffuseTexture[tapCoord];
            float2 encDir   = AverageDirectionTexture[tapCoord];
            float3 dir      = Octahedral::Decode(encDir);

            accRadiance  += radiance.xyz;
            accDirection += dir;
            accAO        += radiance.w;
            accWeight    += 1.0;
        }
    }

    if (accWeight < 1.0)
    {
        outSH0[DTid.xy] = 0;
        outSH1[DTid.xy] = 0;
        return;
    }

    float  invW      = 1.0 / accWeight;
    float3 radiance  = accRadiance  * invW;
    float3 direction = normalize(accDirection);  // average direction
    float  normHitDist = saturate(accAO * invW); // AO acts as proxy for normHitDist

    float4 sh1;
    float4 sh0 = REBLUR_FrontEnd_PackSh(radiance, normHitDist, direction, sh1, true);

    outSH0[DTid.xy] = sh0;
    outSH1[DTid.xy] = sh1;
}
