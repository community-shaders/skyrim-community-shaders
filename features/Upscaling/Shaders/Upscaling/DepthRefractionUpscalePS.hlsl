#include "Upscaling/UpscaleVS.hlsl"

#if defined(PSHADER)
#	include "Common/FrameBuffer.hlsli"
#	include "Common/SharedData.hlsli"

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
	float useWideKernel;
	float pad0;
};

float SampleMinDepth2x2(float2 uv)
{
	float4 depthQuad = DepthTex.GatherRed(LinearSampler, uv);
	return min(min(depthQuad.x, depthQuad.y), min(depthQuad.z, depthQuad.w));
}

float SampleDepthClamped(int2 coord, int2 maxCoord)
{
	int2 c = clamp(coord, int2(0, 0), maxCoord);
	return DepthTex.Load(int3(c, 0));
}

float SampleMinDepth3x3(float2 uv)
{
	// Use cached frame buffer dimensions to avoid per-pixel texture-dimension queries.
	int2 texSize = int2(SharedData::BufferDim.xy);
	int2 maxCoord = texSize - 1;
	int2 centerCoord = int2(uv * SharedData::BufferDim.xy);

	float row0 = min(
		SampleDepthClamped(centerCoord + int2(-1, -1), maxCoord),
		min(
			SampleDepthClamped(centerCoord + int2(0, -1), maxCoord),
			SampleDepthClamped(centerCoord + int2(1, -1), maxCoord)));

	float row1 = min(
		SampleDepthClamped(centerCoord + int2(-1, 0), maxCoord),
		min(
			SampleDepthClamped(centerCoord + int2(0, 0), maxCoord),
			SampleDepthClamped(centerCoord + int2(1, 0), maxCoord)));

	float row2 = min(
		SampleDepthClamped(centerCoord + int2(-1, 1), maxCoord),
		min(
			SampleDepthClamped(centerCoord + int2(0, 1), maxCoord),
			SampleDepthClamped(centerCoord + int2(1, 1), maxCoord)));

	return min(row0, min(row1, row2));
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
	psout.Depth = (useWideKernel > 0.5f) ? SampleMinDepth3x3(uv) : SampleMinDepth2x2(uv);
	// Keep SAO camera Z smooth to avoid over-occlusion; depth culling uses SV_Depth.
	psout.SAOCameraZ = bilinearDepth;
#	else
	psout.SAOCameraZ = psout.Depth;
#	endif

	return psout;
}

#endif
