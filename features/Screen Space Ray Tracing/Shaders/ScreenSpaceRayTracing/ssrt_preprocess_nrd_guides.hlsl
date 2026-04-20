// Preprocesses NRD guide textures at full resolution.
// Input: fullres NDC depth (t0), fullres GBuffer normal+roughness (t1).
// Output: fullres viewZ R32F (u0), fullres NRD normal+roughness R8G8B8A8 (u1).
// Motion vectors are bound directly from the game's kMOTION_VECTOR render target into NRD IN_MV.

#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/GBuffer.hlsli"
#include "NRD/NRDReblurSH.hlsli"

Texture2D<float>  srcNDCDepth    : register(t0);
Texture2D<float3> srcNormalRough : register(t1);

RWTexture2D<float>  outViewZ           : register(u0);
RWTexture2D<float4> outNormalRoughness : register(u1);

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
    float HitDistA;
    float HitDistB;
    float HitDistC;
    float HitDistD;
    uint FrameIndex;
};

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 pixCoord = DTid.xy;
    if (any(pixCoord >= (uint2)FrameDim)) return;

    float depth = srcNDCDepth[pixCoord];
    outViewZ[pixCoord] = SharedData::GetScreenDepth(depth);

    float3 nr = srcNormalRough[pixCoord];
    float3 normalVS = GBuffer::DecodeNormal(nr.xy);
    float roughness = 1.0 - nr.z;

    float3 normalWS = normalize(mul((float3x3)FrameBuffer::CameraViewInverse[0], normalVS));
    outNormalRoughness[pixCoord] = NRD_FrontEnd_PackNormalAndRoughness(normalWS, roughness, 0.0);
}
