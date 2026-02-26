#include "Common/DummyVSTexCoord.hlsl"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color: SV_Target0;
};

#if defined(PSHADER)
SamplerState SourceSampler : register(s0);

Texture2D<float4> SourceTex : register(t0);

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float4 srcColor = SourceTex.Sample(SourceSampler, input.TexCoord);
	psout.Color = srcColor;

	return psout;
}
#endif
