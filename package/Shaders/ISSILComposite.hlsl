#include "Common/DummyVSTexCoord.hlsl"
#include "Common/FrameBuffer.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color: SV_Target0;
};

#if defined(PSHADER)
SamplerState sourceSampler : register(s0);
SamplerState IndLightingSampler : register(s1);

Texture2D<float4> sourceTex : register(t0);
Texture2D<float4> IndLightingTex : register(t1);

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float4 sourceColor = sourceTex.Sample(sourceSampler, input.TexCoord);
	float4 ilColor = IndLightingTex.Sample(IndLightingSampler, input.TexCoord);

	psout.Color = sourceColor + ilColor;

	return psout;
}
#endif
