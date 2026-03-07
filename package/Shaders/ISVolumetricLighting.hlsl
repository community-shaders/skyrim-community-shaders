#include "Common/DummyVSTexCoord.hlsl"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color: SV_Target0;
};

#if defined(PSHADER)
SamplerState DepthSampler : register(s0);
SamplerState ShadowMapSampler : register(s1);

Texture2D<float4> DepthTex : register(t0);
Texture2D<float4> ShadowMapTex : register(t1);

cbuffer PerGeometry : register(b2)
{
	float4 g_ViewProj[4] : packoffset(c0);
	float4 g_InvViewProj[4] : packoffset(c4);
	float4 g_ShadowSampleParam : packoffset(c8);
	float4 ShadowMapProj[6] : packoffset(c9);
	float4 LightColor_Intensity : packoffset(c15);
};

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	psout.Color = float4(LightColor_Intensity.w * LightColor_Intensity.xyz, LightColor_Intensity.w);

	return psout;
}
#endif
