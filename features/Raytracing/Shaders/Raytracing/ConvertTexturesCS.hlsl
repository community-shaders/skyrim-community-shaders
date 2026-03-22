#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"
#include "Raytracing/Includes/Common.hlsli"

Texture2D<unorm half4> NormalSmoothness : register(t0);
Texture2D<unorm half4> Albedo : register(t1);
Texture2D<unorm half4> GNMAO : register(t2);

RWTexture2D<snorm half4> NormalRoughness : register(u0);
RWTexture2D<half3> DiffuseAlbedo : register(u1);

SamplerState Sampler : register(s0);

[numthreads(8, 8, 1)] void main(uint2 id : SV_DispatchThreadID) {
	if (any(id >= DynamicResolution))
		return;

	const float2 uv = float2(id.xy + 0.5f) / Resolution;

	const unorm half3 normalSmoothness = NormalSmoothness.SampleLevel(Sampler, uv, 0).xyz;
	const snorm half3 normalWS = normalize(ViewToWorldVector(GBuffer::DecodeNormal(normalSmoothness.xy), FrameBuffer::CameraViewInverse[0]));
	NormalRoughness[id] = half4(normalWS, 1.0f - normalSmoothness.z);

#if DLSS_RR
	const float4 albedo = Albedo.SampleLevel(Sampler, uv, 0);
	const float metallic = GNMAO.SampleLevel(Sampler, uv, 0).z;

	DiffuseAlbedo[id] = float4(Color::SrgbToLinear(albedo.rgb) * (1.0f - metallic), albedo.a);
#endif
}