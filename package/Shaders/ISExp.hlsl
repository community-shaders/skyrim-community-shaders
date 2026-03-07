#include "Common/DummyVSTexCoord.hlsl"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color: SV_Target0;
};

#if defined(PSHADER)
SamplerState OriginalSampler : register(s0);

Texture2D<float4> OriginalTex : register(t0);

cbuffer PerGeometry : register(b2)
{
	float Params : packoffset(c0);
};

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float srcValue = OriginalTex.Sample(OriginalSampler, input.TexCoord).x;
	psout.Color = exp2(1.44269502 * Params * srcValue);

	return psout;
}
#endif
