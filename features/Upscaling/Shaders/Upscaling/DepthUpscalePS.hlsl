// Conservative depth buffer upscaling for VR depth-based culling
//
// When upscaling (FSR/DLSS) is active, the depth buffer is rendered at a lower
// resolution than the display. Skyrim VR's depth-based culling (OBBOcclusionTesting)
// reads from the depth buffer to determine object visibility, but with a mismatched
// resolution, objects may be incorrectly culled (appearing to flicker in/out of view).
//
// This shader upscales the low-resolution depth buffer to full resolution using a
// conservative approach that prevents incorrect culling. For reversed-Z depth
// (smaller = closer), we take the minimum to ensure objects are only culled if
// truly behind other geometry at all sub-pixel locations.
//
// Uses a hybrid approach: bilinear interpolation blended with min-depth sampling.
//
// Based on depth upscaling approach by vrnord
// https://github.com/vrnord/skyrim-community-shaders-VR-DLSS

#include "Upscaling/UpscaleVS.hlsl"

#if defined(PSHADER)
#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float Depth : SV_Depth;
};

Texture2D<float> DepthLowRes : register(t0);

SamplerState LinearSampler : register(s0);

cbuffer DepthUpscaleCB : register(b0)
{
	float2 SourceResolution;
	float2 TargetResolution;
	float2 ResolutionScale;
	float2 TexelSize;
};

// Sample minimum depth from 2x2 neighborhood using GatherRed.
// For reversed-Z (smaller = closer), minimum ensures conservative culling.
float SampleMinDepth2x2(float2 uv)
{
	float4 depthQuad = DepthLowRes.GatherRed(LinearSampler, uv);
	return min(min(depthQuad.x, depthQuad.y), min(depthQuad.z, depthQuad.w));
}

// Sample minimum depth from 3x3 neighborhood for higher upscale ratios.
float SampleMinDepth3x3(float2 uv)
{
	float2 texelPos = uv * SourceResolution;
	int2 centerCoord = int2(floor(texelPos));

	float minDepth = 1.0;

	[unroll]
	for (int y = -1; y <= 1; y++) {
		[unroll]
		for (int x = -1; x <= 1; x++) {
			int2 sampleCoord = centerCoord + int2(x, y);
			sampleCoord = clamp(sampleCoord, int2(0, 0), int2(SourceResolution) - 1);
			float sampleDepth = DepthLowRes.Load(int3(sampleCoord, 0));
			minDepth = min(minDepth, sampleDepth);
		}
	}

	return minDepth;
}

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float2 fullResUV = input.TexCoord;
	float2 lowResUV = fullResUV * ResolutionScale;
	lowResUV = saturate(lowResUV);

	float upscaleRatio = TargetResolution.x / SourceResolution.x;

	float bilinearDepth = DepthLowRes.SampleLevel(LinearSampler, lowResUV, 0);

	float minDepth;
	if (upscaleRatio > 1.5) {
		minDepth = SampleMinDepth3x3(lowResUV);
	} else {
		minDepth = SampleMinDepth2x2(lowResUV);
	}

	// Blend between smooth and conservative depth.
	// Higher bias = more conservative (safer culling, potential popping).
	// Lower bias = smoother (better visual, potential incorrect culling).
	const float conservativeBias = 0.35;

	psout.Depth = lerp(bilinearDepth, minDepth, conservativeBias);

	return psout;
}

#endif
