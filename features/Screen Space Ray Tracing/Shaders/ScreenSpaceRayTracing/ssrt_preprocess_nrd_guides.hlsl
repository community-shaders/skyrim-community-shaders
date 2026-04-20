// Preprocesses NRD guide textures at 1/2 resolution via 2x2 downsampling.
// Input: fullres NDC depth (t0), fullres GBuffer normal+roughness (t1), fullres motion vectors (t2).
// Output: halfres viewZ R16F (u0), halfres NRD normal+roughness R8G8B8A8 (u1), halfres motion R16G16F (u2).

#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/GBuffer.hlsli"
#include "NRD/NRDReblurSH.hlsli"

Texture2D<float>  srcNDCDepth    : register(t0);
Texture2D<float3> srcNormalRough : register(t1);
Texture2D<float2> srcMotion      : register(t2);

RWTexture2D<float>  outViewZ          : register(u0);
RWTexture2D<float4> outNormalRoughness : register(u1);
RWTexture2D<float2> outMotion          : register(u2);

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
    uint2 halfRes = (uint2)(FrameDim / 2);
    if (any(DTid.xy >= halfRes)) return;

    uint2 base = DTid.xy * 2;

    // --- ViewZ: minimum of 2x2 quad (closest surface prevents sky leak into tile classification) ---
    float4 depths = float4(
        srcNDCDepth[base],
        srcNDCDepth[base + uint2(1, 0)],
        srcNDCDepth[base + uint2(0, 1)],
        srcNDCDepth[base + uint2(1, 1)]);
    float4 viewZs = SharedData::GetScreenDepths(depths);
    outViewZ[DTid.xy] = min(min(viewZs.x, viewZs.y), min(viewZs.z, viewZs.w));

    // --- Normal + roughness: decode all 4 samples, average in view-space, rotate to world-space ---
    float3 nr0 = srcNormalRough[base];
    float3 nr1 = srcNormalRough[base + uint2(1, 0)];
    float3 nr2 = srcNormalRough[base + uint2(0, 1)];
    float3 nr3 = srcNormalRough[base + uint2(1, 1)];

    float3 normalVS = normalize(
        GBuffer::DecodeNormal(nr0.xy) +
        GBuffer::DecodeNormal(nr1.xy) +
        GBuffer::DecodeNormal(nr2.xy) +
        GBuffer::DecodeNormal(nr3.xy));

    float roughness = ((1.0 - nr0.z) + (1.0 - nr1.z) + (1.0 - nr2.z) + (1.0 - nr3.z)) * 0.25;

    float3 normalWS = normalize(mul((float3x3)FrameBuffer::CameraViewInverse[0], normalVS));
    outNormalRoughness[DTid.xy] = NRD_FrontEnd_PackNormalAndRoughness(normalWS, roughness, 0.0);

    // --- Motion vectors: 2x2 average ---
    outMotion[DTid.xy] = (srcMotion[base] + srcMotion[base + uint2(1, 0)] +
                          srcMotion[base + uint2(0, 1)] + srcMotion[base + uint2(1, 1)]) * 0.25;
}
