#include "Common/Color.hlsli"
#include "Common/DummyVSTexCoord.hlsl"
#include "Common/FrameBuffer.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color : SV_Target0;
};

#if defined(PSHADER)
SamplerState Src0Sampler : register(s0);
SamplerState Src1Sampler : register(s1);

Texture2D<float4> Src0Tex : register(t0);
Texture2D<float4> Src1Tex : register(t1);

cbuffer PerGeometry : register(b2)
{
	float4 Tint : packoffset(c0);
};

float2 GetRefractedTexCoord(float2 texCoordOriginal, float3 normalOriginal)
{
	float2 texCoord = texCoordOriginal + float2(-1, 1) * (2 * (0.05 * normalOriginal.z) * (normalOriginal.xy - 0.5));
	float2 texCoordClamped = texCoord > 0.85 ? lerp(0.85, texCoord, 0.78) : texCoord;
	texCoordClamped = texCoord < 0.15 ? lerp(0.15, texCoord, 0.78) : texCoordClamped;
	return FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(lerp(texCoord, texCoordClamped, normalOriginal.z));
}

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float2 texCoordOriginal = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);

	float4 normalOriginal = Src1Tex.Sample(Src1Sampler, texCoordOriginal);
	float4 colorOriginal = Src0Tex.Sample(Src0Sampler, texCoordOriginal);

	float2 texCoordRefracted = GetRefractedTexCoord(input.TexCoord, normalOriginal.xyz);

	float refractedMask = Src1Tex.Sample(Src1Sampler, texCoordRefracted).w;
	float4 colorRefracted = Src0Tex.Sample(Src0Sampler, texCoordRefracted);
	float4 colorResulting = refractedMask != 0 ? colorRefracted : colorOriginal;

	if (normalOriginal.w > 0.8 && normalOriginal.w < 1) {
		psout.Color.xyz = lerp(colorResulting.xyz, Tint.xyz * Color::RGBToLuminance2(colorRefracted.xyz), Tint.w);
	} else {
		psout.Color.xyz = colorResulting.xyz;
	}

	psout.Color.w = colorResulting.w;

	return psout;
}
#endif
