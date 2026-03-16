/**
 * @file DepthUpscalePS.hlsl
 * @brief Point-sampled depth buffer upscaling for VR depth-based culling
 *
 * When upscaling (FSR/DLSS) is active, the depth buffer is rendered at a lower
 * resolution than the display. Skyrim VR's depth-based culling (OBBOcclusionTesting)
 * reads from the depth buffer to determine object visibility, but with a mismatched
 * resolution, objects may be incorrectly culled (appearing to flicker in/out of view).
 *
 * This shader upscales the low-resolution depth buffer to full resolution using
 * pure point sampling. Previous conservative blending (GatherRed + lerp toward
 * min depth) caused HAM mask bleed: depth == 0 values from the hidden area mesh
 * leaked into valid depth through the 2x2 neighborhood blend, creating artifacts
 * at the mask boundary after DRS upscaling.
 *
 * Based on depth upscaling approach by vrnord
 * https://github.com/vrnord/skyrim-community-shaders-VR-DLSS
 */

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

cbuffer DepthUpscaleCB : register(b0)
{
	float2 SourceDim;    // Full texture dimensions (texels)
	float2 InvSourceDim; // 1.0 / SourceDim
	float2 Scale;        // resolutionScale (render/display ratio)
	float2 Pad;
};

/**
 * @brief Main pixel shader entry point
 *
 * Pure point-sampled depth upscaling. Maps display-res pixel position to
 * render-res texel and loads directly — no blending, no mask bleed.
 */
PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	// Map full-res UV to render-res UV (same transform as the engine's
	// GetDynamicResolutionAdjustedScreenPosition).
	float2 uv = Scale * input.TexCoord;

	// Per-eye clamping for SBS stereo: prevent sampling across the center seam.
	bool isRight = input.TexCoord.x >= 0.5;
	float halfScale = 0.5 * Scale.x;
	uv.x = clamp(uv.x, isRight ? halfScale : 0.0, isRight ? Scale.x : halfScale);
	uv.y = clamp(uv.y, 0.0, Scale.y);

	// Nearest texel coordinate — pure point sampling, no blending
	int2 texel = int2(floor(uv * SourceDim));
	psout.Depth = DepthLowRes.Load(int3(texel, 0));

	return psout;
}

#endif
