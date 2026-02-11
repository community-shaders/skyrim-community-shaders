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
	float useWideKernel;
	float pad0;
};

float SampleMinDepth2x2(float2 uv)
{
	float4 depthQuad = DepthTex.GatherRed(LinearSampler, uv);
	return min(min(depthQuad.x, depthQuad.y), min(depthQuad.z, depthQuad.w));
}

float SampleMinDepth3x3(float2 uv)
{
	uint width;
	uint height;
	DepthTex.GetDimensions(width, height);

	float2 texelPos = uv * float2(width, height);
	int2 centerCoord = int2(floor(texelPos));

	float minDepth = 1.0f;

	[unroll]
	for (int y = -1; y <= 1; y++) {
		[unroll]
		for (int x = -1; x <= 1; x++) {
			int2 sampleCoord = centerCoord + int2(x, y);
			sampleCoord = clamp(sampleCoord, int2(0, 0), int2(width - 1, height - 1));
			float sampleDepth = DepthTex.Load(int3(sampleCoord, 0));
			minDepth = min(minDepth, sampleDepth);
		}
	}

	return minDepth;
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
	float bilinearDepth = DepthTex.SampleLevel(LinearSampler, uv, 0);

	float depthOut = bilinearDepth;
#	if defined(VR)
	float conservativeDepth = (useWideKernel > 0.5f) ? SampleMinDepth3x3(uv) : SampleMinDepth2x2(uv);
	depthOut = conservativeDepth;
#	endif

	psout.Depth = depthOut;
	// Keep SAO camera Z smooth to avoid over-occlusion; depth culling uses SV_Depth.
	psout.SAOCameraZ = bilinearDepth;

	return psout;
}

#endif
