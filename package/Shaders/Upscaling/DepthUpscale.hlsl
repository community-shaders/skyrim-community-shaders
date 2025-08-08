struct VS_OUTPUT
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

#if defined(VSHADER)

VS_OUTPUT main(uint vertexId : SV_VertexID)
{
	VS_OUTPUT vsout;

	// Generate fullscreen triangle using vertex ID
	// Vertex 0: (-1, -1) -> (0, 1) UV
	// Vertex 1: (-1,  3) -> (0, -1) UV
	// Vertex 2: ( 3, -1) -> (2, 1) UV
	vsout.Position.x = (float)(vertexId / 2) * 4.0 - 1.0;
	vsout.Position.y = (float)(vertexId % 2) * 4.0 - 1.0;
	vsout.Position.z = 0.0;
	vsout.Position.w = 1.0;

	vsout.TexCoord.x = (float)(vertexId / 2) * 2.0;
	vsout.TexCoord.y = 1.0 - (float)(vertexId % 2) * 2.0;

	return vsout;
}

#endif

#if defined(PSHADER)
#include "Common/FrameBuffer.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float Depth : SV_Depth;
};

SamplerState LinearSampler : register(s0);
Texture2D<float> DepthTex : register(t0);

#if defined(VR)
SamplerState PointSampler : register(s1);
Texture2D<uint> StencilTex : register(t1);
#endif

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;
	float2 uv = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);

#if defined(VR)
	uint4 stencilSamples = StencilTex.GatherRed(PointSampler, uv);

	// Choose the minimum stencil value
	uint maxStencil = min(min(stencilSamples.x, stencilSamples.y), min(stencilSamples.z, stencilSamples.w));

	// Only write depth/stencil that is inside the viewable area
	if (maxStencil > 0x00)
		discard;
#endif

	// Upscale depth using linear sampling
	float depth = DepthTex.SampleLevel(LinearSampler, uv, 0);
	psout.Depth = depth;

	return psout;
}

#endif