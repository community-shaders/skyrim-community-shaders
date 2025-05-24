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
	float4 blurParams : packoffset(c0);
	float4 doubleVisParams : packoffset(c1);
};

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float2 texCoord0 = float2(max(0, -doubleVisParams.x + input.TexCoord.x),
		max(-doubleVisParams.y + input.TexCoord.y, 1 - doubleVisParams.w));
	float2 adjustedTexCoord0 = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(texCoord0);
	float3 src0Color0 = Src0Tex.Sample(Src0Sampler, adjustedTexCoord0).xyz;

	float2 texCoord1 = float2(min(doubleVisParams.z, doubleVisParams.x + input.TexCoord.x),
		min(1, doubleVisParams.y + input.TexCoord.y));
	float2 adjustedTexCoord1 = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(texCoord1);
	float3 src0Color1 = Src0Tex.Sample(Src0Sampler, adjustedTexCoord1).xyz;

	float2 adjustedTexCoord2 = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);
	float3 src1Color = Src1Tex.Sample(Src1Sampler, adjustedTexCoord2).xyz;

	float src1Factor = min(1,
		blurParams.z *
			length(float2(2 * ((doubleVisParams.z / doubleVisParams.w) * (input.TexCoord.x - 0.5)),
				2 * (input.TexCoord.y - 0.5))));

	float3 color = 0.5 * ((1 - src1Factor) * (src0Color0 + src0Color1)) + src1Factor * src1Color;

	psout.Color = float4(color, 1);

	return psout;
}
#endif
