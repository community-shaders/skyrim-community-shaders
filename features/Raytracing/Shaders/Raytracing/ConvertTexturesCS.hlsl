#include "Common/Color.hlsli"
#include "Raytracing/Includes/Common.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"

Texture2D<unorm half4> NormalGlossiness     : register(t0);
Texture2D<unorm half4> Albedo               : register(t1);
Texture2D<unorm half4> GNMAO                : register(t2);

RWTexture2D<snorm half4> NormalRoughness    : register(u0);
RWTexture2D<unorm half4> Diffuse            : register(u1);

[numthreads(8, 8, 1)]
void main(uint2 id : SV_DispatchThreadID)
{
    const unorm half3 normalGlossiness = NormalGlossiness[id].xyz;
    const snorm half3 normalWS = normalize(ViewToWorldVector(GBuffer::DecodeNormal(normalGlossiness.xy), FrameBuffer::CameraViewInverse[0]));
    NormalRoughness[id] = half4(normalWS, 1.0f - normalGlossiness.z);

    float metallic, ao;
    UnpackMAO(GNMAO[id].z, metallic, ao);
    Diffuse[id] = float4(Color::GammaToTrueLinear(Albedo[id].rgb) * (1.0f - metallic), 1.0f);
}