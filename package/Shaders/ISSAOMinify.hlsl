#include "Common/DummyVSTexCoord.hlsl"
#include "Common/FrameBuffer.hlsli"

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
	float4 g_RenderTargetResolution : packoffset(c0);
	float4 g_ContrastParams : packoffset(c1);
	float g_UseDynamicSampling : packoffset(c2);
};

float2 GetMinifiedTexCoord(float2 texCoord)
{
	return ((float2)(((int2)(g_RenderTargetResolution.yx * texCoord.yx) & 1) ^ 1) * 2 - 1) *
	           g_RenderTargetResolution.zw +
	       texCoord;
}

static const float ContrastValues[] = { 0.300000, 0.400000,
	0.500000, 0.400000, 0.300000, 0.400000,
	2.000000, 2.500000, 2.000000, 0.400000,
	0.500000, 2.500000, 3.500000, 2.500000,
	0.500000, 0.400000, 2.000000, 2.500000,
	2.000000, 0.400000, 0.300000, 0.400000,
	0.500000, 0.400000, 0.300000 };

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float2 finalTexCoord;
	if (asuint(g_UseDynamicSampling) > 0) {
		float2 drAdjustedTexCoord = FrameBuffer::DynamicResolutionParams1.xy * input.TexCoord;
		float2 minifiedTexCoord = GetMinifiedTexCoord(drAdjustedTexCoord);
		finalTexCoord = clamp(minifiedTexCoord, 0,
			FrameBuffer::DynamicResolutionParams1.xy - float2(FrameBuffer::CameraPreviousPosAdjust.w, 0));
	} else {
		finalTexCoord = GetMinifiedTexCoord(input.TexCoord);
	}

	float4 color = 0;
	if (max(g_RenderTargetResolution.x, g_RenderTargetResolution.y) == 3.0) {
		float2 sampleOffset =
			(((int2)g_RenderTargetResolution.xy & 1) * (1. / 6.) + 0.5) /
			g_RenderTargetResolution.xy;
		color.x = sourceTex.Sample(sourceSampler, finalTexCoord + sampleOffset).x;
		color.x += sourceTex.Sample(sourceSampler,
								finalTexCoord + float2(-sampleOffset.x, sampleOffset.y))
		               .x;
		color.x += sourceTex.Sample(sourceSampler, finalTexCoord - sampleOffset).x;
		color.x += sourceTex.Sample(sourceSampler,
								finalTexCoord + float2(sampleOffset.x, -sampleOffset.y))
		               .x;
		color.x *= 0.25;
	} else {
		color = sourceTex.Sample(sourceSampler, finalTexCoord);
	}

#	if defined(APPLY_CENTER_CONTRAST)
	int contrastIndex = (int)(5 * input.TexCoord.x) + (int)(5 * input.TexCoord.y) * 5;
	float contrastFactor = ContrastValues[contrastIndex] * g_ContrastParams.x;
	color *= contrastFactor;
#	endif

	psout.Color = color;

	return psout;
}
#endif
