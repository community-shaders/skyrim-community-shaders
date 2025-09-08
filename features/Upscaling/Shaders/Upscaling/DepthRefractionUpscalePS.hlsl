#include "Upscaling/UpscaleVS.hlsl"

#if defined(PSHADER)
#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 RefractionNormals : SV_TARGET0;
	float SAOCameraZ : SV_TARGET1;
	float Depth : SV_Depth;
};

SamplerState LinearSampler : register(s0);

Texture2D<float4> RefractionNormals : register(t0);
Texture2D<float> DepthTex : register(t1);

#	if defined(VR)
Texture2D<uint> StencilTex : register(t2);
#	endif

cbuffer JitterCB : register(b0)
{
	float2 jitter;
};

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float2 originalUV = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);

	// Remove jitter offset to get the correct sampling coordinates
	float2 uv = originalUV - (jitter * SharedData::BufferDim.zw);
	
	// Clamp within bounds
	uv = clamp(uv, 0.0, FrameBuffer::DynamicResolutionParams1.xy);

#	if defined(VR)
	uint4 stencilSamples = StencilTex.GatherRed(LinearSampler, uv);

	// Choose the minimum stencil value
	uint minStencil = min(min(stencilSamples.x, stencilSamples.y), min(stencilSamples.z, stencilSamples.w));

	// Only write depth/stencil that is inside the viewable area
	if (minStencil > 0x00)
		discard;
#	endif

	// Upscale using linear sampling
	psout.RefractionNormals = RefractionNormals.SampleLevel(LinearSampler, uv, 0);
	psout.Depth = DepthTex.SampleLevel(LinearSampler, uv, 0);
	psout.SAOCameraZ = psout.Depth;

	return psout;
}

#endif