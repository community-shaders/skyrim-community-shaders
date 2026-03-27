#include "Common/DummyVSTexCoord.hlsl"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color: SV_Target0;
};

#if defined(PSHADER)
SamplerState TintMask0Sampler : register(s0);
SamplerState TintMask1Sampler : register(s1);
SamplerState TintMask2Sampler : register(s2);
SamplerState TintMask3Sampler : register(s3);
SamplerState TintMask4Sampler : register(s4);
SamplerState TintMask5Sampler : register(s5);
SamplerState TintMask6Sampler : register(s6);
SamplerState TintMask7Sampler : register(s7);
SamplerState TintMask8Sampler : register(s8);
SamplerState TintMask9Sampler : register(s9);
SamplerState TintMask10Sampler : register(s10);
SamplerState TintMask11Sampler : register(s11);
SamplerState TintMask12Sampler : register(s12);
SamplerState TintMask13Sampler : register(s13);
SamplerState TintMask14Sampler : register(s14);
SamplerState TintMask15Sampler : register(s15);

Texture2D<float4> TintMask0Tex : register(t0);
Texture2D<float4> TintMask1Tex : register(t1);
Texture2D<float4> TintMask2Tex : register(t2);
Texture2D<float4> TintMask3Tex : register(t3);
Texture2D<float4> TintMask4Tex : register(t4);
Texture2D<float4> TintMask5Tex : register(t5);
Texture2D<float4> TintMask6Tex : register(t6);
Texture2D<float4> TintMask7Tex : register(t7);
Texture2D<float4> TintMask8Tex : register(t8);
Texture2D<float4> TintMask9Tex : register(t9);
Texture2D<float4> TintMask10Tex : register(t10);
Texture2D<float4> TintMask11Tex : register(t11);
Texture2D<float4> TintMask12Tex : register(t12);
Texture2D<float4> TintMask13Tex : register(t13);
Texture2D<float4> TintMask14Tex : register(t14);
Texture2D<float4> TintMask15Tex : register(t15);

cbuffer PerGeometry : register(b2)
{
	float4 Color[16] : packoffset(c0);
};

float3 BlendAlpha(Texture2D<float4> tintMaskTex, SamplerState tintMaskSamplerState, float2 texCoord, float4 blendColor, float3 color)
{
	float tintMask = tintMaskTex.Sample(tintMaskSamplerState, texCoord).x;
	return lerp(color, blendColor.xyz, blendColor.w * tintMask);
}

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float3 color = 0.5.xxx;

	color = BlendAlpha(TintMask0Tex, TintMask0Sampler, input.TexCoord, Color[0], color);
	color = BlendAlpha(TintMask1Tex, TintMask1Sampler, input.TexCoord, Color[1], color);
	color = BlendAlpha(TintMask2Tex, TintMask2Sampler, input.TexCoord, Color[2], color);
	color = BlendAlpha(TintMask3Tex, TintMask3Sampler, input.TexCoord, Color[3], color);
	color = BlendAlpha(TintMask4Tex, TintMask4Sampler, input.TexCoord, Color[4], color);
	color = BlendAlpha(TintMask5Tex, TintMask5Sampler, input.TexCoord, Color[5], color);
	color = BlendAlpha(TintMask6Tex, TintMask6Sampler, input.TexCoord, Color[6], color);
	color = BlendAlpha(TintMask7Tex, TintMask7Sampler, input.TexCoord, Color[7], color);
	color = BlendAlpha(TintMask8Tex, TintMask8Sampler, input.TexCoord, Color[8], color);
	color = BlendAlpha(TintMask9Tex, TintMask9Sampler, input.TexCoord, Color[9], color);
	color = BlendAlpha(TintMask10Tex, TintMask10Sampler, input.TexCoord, Color[10], color);
	color = BlendAlpha(TintMask11Tex, TintMask11Sampler, input.TexCoord, Color[11], color);
	color = BlendAlpha(TintMask12Tex, TintMask12Sampler, input.TexCoord, Color[12], color);
	color = BlendAlpha(TintMask13Tex, TintMask13Sampler, input.TexCoord, Color[13], color);
	color = BlendAlpha(TintMask14Tex, TintMask14Sampler, input.TexCoord, Color[14], color);
	color = BlendAlpha(TintMask15Tex, TintMask15Sampler, input.TexCoord, Color[15], color);

	psout.Color = float4(color, 1);

	return psout;
}
#endif
