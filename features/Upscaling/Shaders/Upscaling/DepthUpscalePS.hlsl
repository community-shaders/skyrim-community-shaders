/**
 * @file DepthUpscalePS.hlsl
 * @brief Conservative depth buffer upscaling for VR depth-based culling
 *
 * When upscaling (FSR/DLSS) is active, the depth buffer is rendered at a lower
 * resolution than the display. Skyrim VR's depth-based culling (OBBOcclusionTesting)
 * reads from the depth buffer to determine object visibility, but with a mismatched
 * resolution, objects may be incorrectly culled (appearing to flicker in/out of view).
 *
 * This shader upscales the low-resolution depth buffer to full resolution using a
 * conservative approach that prevents incorrect culling. The key insight is that
 * for culling to be "safe", we need to preserve the closest depth values - an object
 * should only be culled if it's truly behind other geometry at ALL sub-pixel locations.
 *
 * The shader uses a hybrid approach:
 * - Bilinear interpolation for smooth depth gradients (reduces artifacts on edges)
 * - Minimum depth from 2x2 neighborhood for conservative culling
 * - Configurable blending between the two approaches

 * - Depth upscaling approach by vrnord
 * - https://github.com/vrnord/skyrim-community-shaders-VR-DLSS

 */

#include "Upscaling/UpscaleVS.hlsl"

#if defined(PSHADER)
#	include "Common/FrameBuffer.hlsli"
#	include "Common/SharedData.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float Depth : SV_Depth;
};

// Low-resolution depth buffer (copy of main depth at render resolution)
Texture2D<float> DepthLowRes : register(t0);

// Linear sampler for bilinear interpolation
SamplerState LinearSampler : register(s0);

cbuffer DepthUpscaleCB : register(b0)
{
	float2 SourceResolution;  // Low-res depth buffer dimensions
	float2 TargetResolution;  // Full-res target dimensions
	float2 ResolutionScale;   // SourceResolution / TargetResolution
	float2 TexelSize;         // 1.0 / SourceResolution
};

/**
 * @brief Sample minimum depth from 2x2 neighborhood using GatherRed
 *
 * GatherRed efficiently fetches a 2x2 texel quad in a single operation.
 * For reversed-Z depth (smaller = closer), we take the minimum to ensure
 * conservative culling - objects will only be culled if behind ALL 4 samples.
 *
 * @param uv UV coordinates in low-resolution space
 * @return Minimum depth value from the 2x2 neighborhood
 */
float SampleMinDepth2x2(float2 uv)
{
	// GatherRed returns: (0,1), (1,1), (1,0), (0,0) relative to sample point
	float4 depthQuad = DepthLowRes.GatherRed(LinearSampler, uv);
	return min(min(depthQuad.x, depthQuad.y), min(depthQuad.z, depthQuad.w));
}

/**
 * @brief Sample minimum depth from 3x3 neighborhood for higher upscale ratios
 *
 * For aggressive upscale ratios (e.g., Performance mode at 0.5x), a larger
 * neighborhood provides more robust coverage.
 *
 * @param uv UV coordinates in low-resolution space
 * @return Minimum depth value from the 3x3 neighborhood
 */
float SampleMinDepth3x3(float2 uv)
{
	float2 texelPos = uv * SourceResolution;
	int2 centerCoord = int2(floor(texelPos));

	float minDepth = 1.0;  // Far plane in reversed-Z

	[unroll] for (int y = -1; y <= 1; y++)
	{
		[unroll] for (int x = -1; x <= 1; x++)
		{
			int2 sampleCoord = centerCoord + int2(x, y);
			sampleCoord = clamp(sampleCoord, int2(0, 0), int2(SourceResolution) - 1);
			float sampleDepth = DepthLowRes.Load(int3(sampleCoord, 0));
			minDepth = min(minDepth, sampleDepth);
		}
	}

	return minDepth;
}

/**
 * @brief Main pixel shader entry point
 *
 * Performs conservative depth upscaling by blending bilinear interpolation
 * (for smooth gradients) with minimum depth (for safe culling).
 */
PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	// Transform UV from full-res to low-res space
	float2 fullResUV = input.TexCoord;
	float2 lowResUV = fullResUV * ResolutionScale;
	lowResUV = saturate(lowResUV);

	// Calculate upscale ratio to determine filter size
	float upscaleRatio = TargetResolution.x / SourceResolution.x;

	// Bilinear interpolation for smooth base depth
	float bilinearDepth = DepthLowRes.SampleLevel(LinearSampler, lowResUV, 0);

	// Conservative minimum depth
	float minDepth;
	if (upscaleRatio > 1.5) {
		// Use 3x3 for aggressive upscaling (Performance/Ultra Performance modes)
		minDepth = SampleMinDepth3x3(lowResUV);
	} else {
		// Use 2x2 for moderate upscaling (Quality/Balanced modes)
		minDepth = SampleMinDepth2x2(lowResUV);
	}

	// Blend between smooth and conservative
	// Higher bias = more conservative (safer culling, potential popping)
	// Lower bias = smoother (better visual, potential incorrect culling)
	// 0.35 provides good balance for typical VR scenarios
	const float conservativeBias = 0.35;

	psout.Depth = lerp(bilinearDepth, minDepth, conservativeBias);

	return psout;
}

#endif