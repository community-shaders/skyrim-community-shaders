#include "Raytracing/Includes/Common.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/Color.hlsli"

Texture2D<unorm half4> NormalGlossiness     : register(t0);
Texture2D<unorm half4> Albedo               : register(t1);
Texture2D<unorm half4> GNMAO                : register(t2);
Texture2D<snorm half2> MotionVectors        : register(t3);

RWTexture2D<snorm half4> NormalRoughness    : register(u0);
RWTexture2D<unorm half4> Diffuse            : register(u1);
RWTexture2D<snorm half2> MotionVectorsOut   : register(u2);

cbuffer RenderResCB : register(b0)
{
    uint2 RenderRes;
    float2 RenderResRcp;
};

SamplerState Sampler : register(s0);

[numthreads(8, 8, 1)]
void main(uint2 id : SV_DispatchThreadID)
{
    if (any(id >= RenderRes))
        return;

    const float2 uv = float2(id.xy + 0.5f) * RenderResRcp;

    const unorm half3 normalGlossiness = NormalGlossiness.SampleLevel(Sampler, uv, 0).xyz;
    const snorm half3 normalWS = normalize(ViewToWorldVector(GBuffer::DecodeNormal(normalGlossiness.xy), FrameBuffer::CameraViewInverse[0]));
    NormalRoughness[id] = half4(normalWS, 1.0f - normalGlossiness.z);

    float metallic, ao;
    UnpackMAO(GNMAO.SampleLevel(Sampler, uv, 0).z, metallic, ao);

    const float4 albedo = Albedo.SampleLevel(Sampler, uv, 0);
    Diffuse[id] = float4(Color::GammaToTrueLinear(albedo.rgb) * (1.0f - metallic), albedo.a);

    MotionVectorsOut[id] = MotionVectors.SampleLevel(Sampler, uv, 0);
}

[numthreads(8, 8, 1)]
void main2(uint2 id : SV_DispatchThreadID)
{
    const unorm half3 normalGlossiness = NormalGlossiness[id].xyz;
    const snorm half3 normalWS = normalize(ViewToWorldVector(GBuffer::DecodeNormal(normalGlossiness.xy), FrameBuffer::CameraViewInverse[0]));
    NormalRoughness[id] = half4(normalWS, 1.0f - normalGlossiness.z);

    float metallic, ao;
    UnpackMAO(GNMAO[id].z, metallic, ao);
    Diffuse[id] = Albedo[id] * (1.0f - metallic);

    MotionVectorsOut[id] = MotionVectors[id];
}
