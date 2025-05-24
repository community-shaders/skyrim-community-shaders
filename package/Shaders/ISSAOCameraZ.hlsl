#include "Common/DummyVSTexCoord.hlsl"
#include "Common/FrameBuffer.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float ClippedDepth : SV_Target0;
};

#if defined(PSHADER)
SamplerState DepthSampler : register(s0);

Texture2D<float4> DepthTex : register(t0);

cbuffer PerGeometry : register(b2)
{
	float4 g_ClipInfos : packoffset(c0);
	float4 g_ScreenInfos : packoffset(c1);
};

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float2 screenPosition = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);
	float depth = DepthTex.Sample(DepthSampler, screenPosition).x;

	psout.ClippedDepth = clamp(g_ClipInfos.x / (g_ClipInfos.y * depth + g_ClipInfos.z), 0, g_ClipInfos.w);

	return psout;
}
#endif
