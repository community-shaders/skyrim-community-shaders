#include "Common/DummyVSTexCoord.hlsl"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color: SV_Target0;
};

#if defined(PSHADER)
SamplerState sourceSampler : register(s0);

Texture2D<float4> sourceTex : register(t0);

cbuffer PerGeometry : register(b2)
{
	float4 g_UpsampleParameters : packoffset(c0);
};

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float2 upsampleTexCoord = min(g_UpsampleParameters.zw, g_UpsampleParameters.xy * input.TexCoord);
	psout.Color = sourceTex.Sample(sourceSampler, upsampleTexCoord).xyzw;

	return psout;
}
#endif
