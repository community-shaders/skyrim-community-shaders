// Applies reprojected multi-bounce GI directly into MAIN (irradiance-space RT) as the first
// SSRT prepass step. Reads last frame's denoised NRD SH and resolves against current-pixel
// normal/V/roughness, then adds the contribution in gamma/irradiance space so downstream
// lighting remains in the expected color space.
// Requires typed UAV load support on MAIN's format (R11G11B10F or R16G16B16A16F).

#include "ScreenSpaceRayTracing/ssrt_common.hlsli"
#include "NRD/NRDReblurSH.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/Color.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/FrameBuffer.hlsli"

Texture2D<float4> PrevSH0       : register(t0);   // last-frame OUT_DIFF_SH0
Texture2D<float4> PrevSH1       : register(t1);   // last-frame OUT_DIFF_SH1
// t2 = NormalRoughnessTexture (declared in ssrt_common.hlsli)
Texture2D<float>  DepthTexture          : register(t3);
Texture2D<float4> MotionVectorTexture   : register(t4);
Texture2D<float>  PrevViewZ     : register(t5);   // last-frame texNRDViewZ
Texture2D<float3> AlbedoTexture   : register(t6);

RWTexture2D<float4> outMain : register(u0);

SamplerState samplerPointClamp : register(s0);

cbuffer SSRTCB : register(b1)
{
    uint MaxSteps;
    uint MaxMips;
    uint UseDynamicCubemapsAsFallback;
    float Thickness;
    float NormalBias;
    float BRDFBias;
    float OcclusionStrength;
    float _pad0;

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

[numthreads(8, 8, 1)] void main(uint2 DTid : SV_DispatchThreadID)
{
    if (any(DTid >= (uint2)FrameDim)) return;

    if (SharedData::ssrtSettings.EnablePrevGIReprojection == 0) return;

    const uint2 pixCoord = DTid;
    const float2 uv = (pixCoord + 0.5) * RcpFrameDim;

    uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);
    float depth = DepthTexture.SampleLevel(samplerPointClamp, uv * FrameBuffer::DynamicResolutionParams1.xy, 0);

    // Reconstruct prev UV from motion vectors.
    float2 thisScreen = (uv - 0.5f) * float2(2.0f, -2.0f);
    float2 velocity = MotionVectorTexture.SampleLevel(samplerPointClamp, uv * FrameBuffer::DynamicResolutionParams1.xy, 0).xy;
    float2 prevUV = (thisScreen + velocity * float2(2.f, -2.f)) * float2(0.5f, -0.5f) + 0.5f;

    if (any(prevUV < 0.0) || any(prevUV > 1.0)) return;

    float2 prevSamp = prevUV * FrameBuffer::DynamicResolutionParams1.xy;
    float prevViewZ = PrevViewZ.SampleLevel(samplerPointClamp, prevSamp, 0);

    // Disocclusion: reject reprojection when current-vs-prev linear depth diverges.
    if (abs(SharedData::GetScreenDepth(depth) - prevViewZ) >= 10) return;

    NRD_SG sg = REBLUR_BackEnd_UnpackSh(
        PrevSH0.SampleLevel(samplerPointClamp, prevSamp, 0),
        PrevSH1.SampleLevel(samplerPointClamp, prevSamp, 0));

    float3 nr = NormalRoughnessTexture[pixCoord].xyz;
    float3 normalVS = GBuffer::DecodeNormal(nr);
    float3 normalWS = normalize(mul((float3x3)FrameBuffer::CameraViewInverse[eyeIndex], normalVS));

	float4 positionWS = float4(2 * float2(uv.x, -uv.y + 1) - 1, depth, 1);
	positionWS = mul(FrameBuffer::CameraViewProjInverse[eyeIndex], positionWS);
	positionWS.xyz = positionWS.xyz / positionWS.w;

    float3 V = -normalize(positionWS.xyz);

    float3 il = NRD_SG_ResolveDiffuse(sg, normalWS, V, 1.0 - nr.z);

    float3 linAlbedo = Color::IrradianceToLinear(AlbedoTexture[pixCoord]);

    float3 color = Color::IrradianceToLinear(outMain[pixCoord].xyz);
    color += il * linAlbedo; 
    color = Color::IrradianceToGamma(color);

    outMain[pixCoord] = float4(color, 1.0);
}
