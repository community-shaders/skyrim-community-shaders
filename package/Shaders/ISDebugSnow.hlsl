#include "Common/DummyVSTexCoord.hlsl"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color: SV_Target0;
};

#if defined(PSHADER)
SamplerState snowAlphaSpecSampler : register(s0);
SamplerState sceneSampler : register(s1);

Texture2D<float4> snowAlphaSpecTex : register(t0);
Texture2D<float4> sceneTex : register(t1);

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float snowAlphaSpec = snowAlphaSpecTex.Sample(snowAlphaSpecSampler, input.TexCoord).y;
	float4 sceneColor = sceneTex.Sample(sceneSampler, input.TexCoord);

	psout.Color = sceneColor + float4(snowAlphaSpec, 0, 0, 1);

	return psout;
}
#endif
