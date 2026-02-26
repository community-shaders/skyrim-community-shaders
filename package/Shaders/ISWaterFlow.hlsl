#include "Common/DummyVSTexCoord.hlsl"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color: SV_Target0;
};

#if defined(PSHADER)
SamplerState FlowmapSampler : register(s0);

Texture2D<float4> FlowmapTex : register(t0);

cbuffer PerGeometry : register(b2)
{
	float2 CellOffset : packoffset(c0.x);
	float2 LastCenter : packoffset(c0.z);
	float2 Center : packoffset(c1.x);
	float Radius : packoffset(c1.z);
	float Magnitude : packoffset(c1.w);
	float Strength : packoffset(c2.x);
	float Falloff : packoffset(c2.y);
	float Scale : packoffset(c2.z);
	float2 FlowVector : packoffset(c3);
};

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float centerDistance = length(Center - frac(float2(-CellOffset.x + input.TexCoord.x, CellOffset.y + input.TexCoord.y)));
	float4 flowmapValue = FlowmapTex.Sample(FlowmapSampler, input.TexCoord);
	if (centerDistance < Radius) {
		float flowStrength = Strength * (1 - pow(centerDistance / Radius, 4));
		flowmapValue.xy = normalize(normalize(flowmapValue.xy * 2 - 1) + flowStrength * FlowVector) * 0.5 + 0.5;
		flowmapValue.z = saturate(Scale * flowStrength + flowmapValue.z);
		flowmapValue.w = max(0.1, min(1, 0.002 * (flowmapValue.w * 500 + Magnitude * flowStrength)));
	}
	psout.Color = flowmapValue;

	return psout;
}
#endif
