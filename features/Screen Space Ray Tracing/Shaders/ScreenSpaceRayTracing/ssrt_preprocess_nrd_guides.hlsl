// Preprocesses NRD guide textures at 1/2 resolution.
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

    // Map to fullres center pixel
    uint2 fullCoord = DTid.xy * 2;

    // --- ViewZ: linearise the NDC depth ---
    float ndcDepth = srcNDCDepth[fullCoord];
    float viewZ    = SharedData::GetScreenDepth(ndcDepth);
    outViewZ[DTid.xy] = viewZ;

    // --- Normal + roughness: decode view-space, rotate to world-space ---
    float3 normalGlossiness = srcNormalRough[fullCoord];
    float3 normalVS  = GBuffer::DecodeNormal(normalGlossiness.xy);
    float  roughness = 1.0 - normalGlossiness.z;

    // View-space normal -> world-space via the camera view-inverse matrix
    // (rotation only, so we can use 3x3 portion via mul with float3x3 cast)
    float3 normalWS = normalize(mul((float3x3)FrameBuffer::CameraViewInverse[0], normalVS));

    // Pack using NRD R10G10B10A2 encoding (materialID = 0)
    outNormalRoughness[DTid.xy] = NRD_FrontEnd_PackNormalAndRoughness(normalWS, roughness, 0.0);

    // --- Motion vectors: pass through (game stores UV-space delta) ---
    outMotion[DTid.xy] = srcMotion[fullCoord];
}
