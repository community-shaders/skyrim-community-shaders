#include "Upscaling/UpscaleVS.hlsl"

#if defined(PSHADER)
#	include "Common/FrameBuffer.hlsli"
#	include "Common/SharedData.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 RefractionNormals: SV_TARGET0;
	float SAOCameraZ: SV_TARGET1;
	float Depth: SV_Depth;
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
	float useWideKernel;
	float pad0;
};

float SampleMinDepth2x2(float2 uv)
{
	float4 depthQuad = DepthTex.GatherRed(LinearSampler, uv);
	return min(min(depthQuad.x, depthQuad.y), min(depthQuad.z, depthQuad.w));
}

float Min4(float4 v)
{
	return min(min(v.x, v.y), min(v.z, v.w));
}

float SampleMinDepthWideGather(float2 uv)
{
	// Gather-only wide footprint (4 offset 2x2 GatherRed calls) to save performance.
	float d0 = Min4(DepthTex.GatherRed(LinearSampler, uv, int2(-1, -1)));
	float d1 = Min4(DepthTex.GatherRed(LinearSampler, uv, int2(1, -1)));
	float d2 = Min4(DepthTex.GatherRed(LinearSampler, uv, int2(-1, 1)));
	float d3 = Min4(DepthTex.GatherRed(LinearSampler, uv, int2(1, 1)));
	return min(min(d0, d1), min(d2, d3));
}

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float2 originalUV = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);

	// Remove jitter offset to get the correct sampling coordinates
	float2 uv = originalUV - (jitter * SharedData::BufferDim.zw);

	// Clamp within dynamic-resolution bounds (VR: preserve per-eye bounds).
	uv = FrameBuffer::ClampDynamicResolutionAdjustedScreenPosition(uv, input.TexCoord);

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

#	if defined(VR)
	float bilinearDepth = psout.Depth;
	if (useWideKernel > 0.5f) {
		psout.Depth = SampleMinDepthWideGather(uv);
	} else {
		psout.Depth = SampleMinDepth2x2(uv);
	}
	// Keep SAO camera Z smooth to avoid over-occlusion; depth culling uses SV_Depth.
	psout.SAOCameraZ = bilinearDepth;
#	else
	psout.SAOCameraZ = psout.Depth;
#	endif

	return psout;
}

#endif
