#include "Upscaling/UpscaleVS.hlsl"

#if defined(PSHADER)
#include "Common/FrameBuffer.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 RefractionNormals : SV_TARGET;
	float Depth : SV_Depth;
};

SamplerState LinearSampler : register(s0);

#	if defined(VR)
SamplerState PointSampler : register(s1);
#	endif

Texture2D<float4> RefractionNormals : register(t0);
Texture2D<float> DepthTex : register(t1);

#	if defined(VR)
Texture2D<uint> StencilTex : register(t2);
#	endif

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;
	float2 uv = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);

#	if defined(VR)
	uint4 stencilSamples = StencilTex.GatherRed(PointSampler, uv);

	// Choose the minimum stencil value
	uint maxStencil = min(min(stencilSamples.x, stencilSamples.y), min(stencilSamples.z, stencilSamples.w));

	// Only write depth/stencil that is inside the viewable area
	if (maxStencil > 0x00)
		discard;
#	endif

	// Upscale using linear sampling
	psout.RefractionNormals = RefractionNormals.SampleLevel(LinearSampler, uv, 0);
	psout.Depth = DepthTex.SampleLevel(LinearSampler, uv, 0);

	return psout;
}

#endif