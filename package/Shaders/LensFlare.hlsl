#include "Common/FlareOcclusion.hlsli"
#include "Common/FrameBuffer.hlsli"

// Only the VISIBILITY pass is replaced; the flare sprite pass stays vanilla.

struct VS_INPUT
{
	float4 Position: POSITION0;
};

struct VS_OUTPUT
{
	float4 Position: SV_POSITION0;
};

#if defined(VSHADER)
VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;
	vsout.Position = float4(input.Position.xy, 0.0, 1.0);
	return vsout;
}
#endif

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Visibility: SV_Target0;
};

#if defined(PSHADER)
static const uint MaxLights = 16;

SamplerState DepthSampler : register(s0);
Texture2D<float4> DepthTex : register(t0);

cbuffer PerGeometry : register(b2)
{
	float4 ScreenSpaceLightPos[MaxLights];  // xy: screen UV, z: depth a tap must reach to count as unoccluded
};

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	// The target is one texel per light
	float4 light = ScreenSpaceLightPos[uint(input.Position.x)];
	uint visibleSamples = 0;
	[unroll] for (uint i = 0; i < FlareOcclusion::SampleCount; i++)
	{
		// Off-screen taps count as visible, matching the sun glare
		float2 sampleUV = light.xy + FlareOcclusion::GetSampleOffset(i);
		visibleSamples += FrameBuffer::IsOutsideFrame(sampleUV) ||
		                  DepthTex.Sample(DepthSampler, FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(sampleUV)).x >= light.z;
	}
	psout.Visibility = FlareOcclusion::GetVisibility(visibleSamples);
	return psout;
}
#endif
