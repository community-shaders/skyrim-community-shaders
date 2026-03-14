// Stereo Bilateral Blend - Post-composite stereo consistency pass for VR
//
// Full-image depth-aware bilateral blend with back-check validation that
// reprojects each pixel to the other eye and blends based on depth agreement.
// Source and destination edge detection guard silhouette boundaries before
// reprojection; the back-check provides a second layer of validation.
//
// Based on the stereo-aware bilateral filter from:
// Shi, Billeter, Eisemann 2022, "Stereo-consistent screen-space ambient occlusion"
// https://eprints.whiterose.ac.uk/id/eprint/187713/

#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/VR.hlsli"

Texture2D<float4> ColorTexture : register(t0);
Texture2D<float> DepthTexture : register(t1);

RWTexture2D<float4> OutputRW : register(u0);

#ifdef STEREO_OVERWRITE
RWTexture2D<float2> MotionRW : register(u1);
Texture2D<uint> ModeTexture : register(t2);

// Mode constants matching VRStereoOptimizations/cbuffers.hlsli
// (can't include directly — its cbuffer on b1 conflicts with StereoBlendCB)
#define MODE_DISOCCLUDED     0
#define MODE_EDGE            1
#define MODE_MAIN            2
#define MODE_EDGE_NEIGHBOUR  3
#define MODE_FULL_BLEND      4
#endif

cbuffer StereoBlendCB : register(b1)
{
	float2 FrameDim;
	float2 RcpFrameDim;
	float DepthSigma;
	float MaxBlendFactor;
	float ColorDiffThreshold;
	float DebugEdgeTint;
	uint DebugMode;       // 0 = normal, 1 = depth map diagnostic, 2 = full blend depth visualizer
	float FullBlendDistance;
	float2 _pad;
};

static const float kEdgeDepthThreshold = 0.05;       // NDC depth difference above which a pixel is considered a depth discontinuity and excluded from stereo blend
static const int kEdgeMargin = 2;                     // Neighbor offset (pixels) for destination edge + mask boundary check
static const float kDepthAgreementThreshold = 0.015;  // Relative depth difference threshold for overwrite mode disocclusion rejection

// Samples four depth neighbors in a cross pattern (±offset pixels) around center,
// clamped to eyeIndex's half of the packed stereo buffer to avoid seam contamination.
float4 SampleCrossDepths(int2 center, int offset, uint eyeIndex)
{
	return float4(
		DepthTexture[Stereo::ClampToEyeBounds(center + int2(offset, 0), eyeIndex, FrameDim)],
		DepthTexture[Stereo::ClampToEyeBounds(center + int2(-offset, 0), eyeIndex, FrameDim)],
		DepthTexture[Stereo::ClampToEyeBounds(center + int2(0, offset), eyeIndex, FrameDim)],
		DepthTexture[Stereo::ClampToEyeBounds(center + int2(0, -offset), eyeIndex, FrameDim)]);
}

[numthreads(8, 8, 1)] void main(uint2 dtid : SV_DispatchThreadID) {
	if (any(dtid >= uint2(FrameDim)))
		return;

#ifdef STEREO_OVERWRITE
	// =========================================================================
	// Mode-driven stereo merge: reads per-pixel classification from StencilCS
	// and applies appropriate action per mode and eye.
	// Mode texture is full SBS resolution — ModeTexture[dtid] maps directly.
	// =========================================================================

	float2 uv = (dtid + 0.5) * RcpFrameDim;
	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);

	float centerDepth = DepthTexture[dtid];

	// HMD mask pixels (depth >= 1.0 in reversed-Z) — always skip
	if (centerDepth >= 1.0)
		return;

	uint pixelMode = ModeTexture[dtid];

	// Debug mode 1: depth map diagnostic — show mode texture as solid colors (all pixels)
	if (DebugMode == 1) {
		float4 c = ColorTexture[dtid];
		if (pixelMode == MODE_EDGE)
			OutputRW[dtid] = float4(lerp(c.rgb, float3(0, 1, 0), 0.5), c.a);
		else if (pixelMode == MODE_EDGE_NEIGHBOUR)
			OutputRW[dtid] = float4(lerp(c.rgb, float3(1, 0, 1), 0.5), c.a);
		else if (pixelMode == MODE_DISOCCLUDED)
			OutputRW[dtid] = float4(lerp(c.rgb, float3(0, 0.5, 1), 0.3), c.a);
		else if (pixelMode == MODE_FULL_BLEND)
			OutputRW[dtid] = float4(lerp(c.rgb, float3(1, 0.5, 0), 0.5), c.a);
		return;
	}

	// Debug mode 2: full blend depth visualizer — cyan tint based on proximity to FullBlendDistance
	if (DebugMode == 2) {
		if (centerDepth < 1e-5 || centerDepth >= 1.0)
			return;
		float linDepth = SharedData::GetScreenDepth(centerDepth);
		if (linDepth < FullBlendDistance) {
			float4 c = ColorTexture[dtid];
			float proximity = saturate(1.0 - linDepth / max(FullBlendDistance, 1.0));
			OutputRW[dtid] = float4(lerp(c.rgb, float3(0, 1, 1), proximity * 0.4), c.a);
		}
		return;
	}

	// MODE_DISOCCLUDED: fully shaded, leave untouched
	if (pixelMode == MODE_DISOCCLUDED)
		return;

	// MODE_FULL_BLEND: bilateral blend for 2x supersampling
	if (pixelMode == MODE_FULL_BLEND) {
		float4 center = ColorTexture[dtid];

		// Reproject to the other eye
		Stereo::StereoBilateralResult r = Stereo::ReprojectToOtherEye(uv, centerDepth, eyeIndex, FrameDim);
		if (!r.valid) {
			// Debug tint for failed reprojection
			if (DebugEdgeTint > 0)
				OutputRW[dtid] = float4(lerp(center.rgb, float3(1, 0.5, 0), DebugEdgeTint), center.a);
			return;
		}

		// Only blend with pixels that have valid composited data in both eyes
		uint otherMode = ModeTexture[r.otherPx];
		if (otherMode != MODE_FULL_BLEND && otherMode != MODE_DISOCCLUDED)
			return;

		float4 otherColor = ColorTexture[r.otherPx];
		float otherDepth = DepthTexture[r.otherPx];

		// Depth-weighted bilateral blend
		float maxDepth = max(max(centerDepth, otherDepth), 1e-5);
		float depthAgreement = 1.0 - saturate(abs(centerDepth - otherDepth) / maxDepth / 0.02);
		float blendWeight = 0.5 * depthAgreement;

		float4 result = lerp(center, otherColor, blendWeight);

		if (DebugEdgeTint > 0)
			result.rgb = lerp(result.rgb, float3(0, 1, 1), DebugEdgeTint);

		OutputRW[dtid] = result;
		return;
	}

	if (eyeIndex == 0) {
		// Eye 0: fully shaded for all modes — only apply debug tint to edge pixels
		if (DebugEdgeTint > 0 && pixelMode == MODE_EDGE) {
			float4 c = ColorTexture[dtid];
			OutputRW[dtid] = float4(lerp(c.rgb, float3(0, 1, 0), DebugEdgeTint), c.a);
		}
		return;
	}

	// Eye 1: reproject all non-disoccluded, non-full-blend pixels (MAIN, EDGE) from Eye 0.
	// StencilCS already performed the authoritative disocclusion check with the correct
	// depth buffer state — no redundant depth agreement check here.
	Stereo::StereoBilateralResult r = Stereo::ReprojectToOtherEye(uv, centerDepth, eyeIndex, FrameDim);
	if (!r.valid)
		return;

	// Skip if the Eye 0 source pixel is sky/unrendered (depth at clear value).
	// At DeferredPasses time, sky hasn't rendered yet — source would have clear color.
	// Let the sky/water pass fill these pixels later instead.
	float sourceDepth = DepthTexture[r.otherPx];
	if (sourceDepth >= 1.0 || sourceDepth < 1e-5)
		return;

	OutputRW[dtid] = ColorTexture[r.otherPx];
	MotionRW[dtid] = MotionRW[r.otherPx];

#else  // Normal bilateral blend path

#ifdef EYE0_ONLY
	// Only process Eye 0 (left half) - Eye 1 left untouched
	float2 uvCheck = (dtid + 0.5) * RcpFrameDim;
	if (Stereo::GetEyeIndexFromTexCoord(uvCheck) == 1)
		return;
#endif

	float2 uv = (dtid + 0.5) * RcpFrameDim;
	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);

	float4 centerColor = ColorTexture[dtid];
	float centerDepth = DepthTexture[dtid];

	// Debug states:
	//   0 = mask/sky: skipped (depth == 0 or 1)
	//   1 = source edge: depth discontinuity at this pixel
	//   2 = destination edge: depth discontinuity at reprojected pixel
	//   3 = out of bounds: reprojection left the other eye's frame
	//   4 = blended, back-check passed: surfaces match in both eyes
	//   5 = blended, back-check failed: blend penalized (occlusion edge)
	uint debugState = 0;

	Stereo::StereoBilateralResult r = (Stereo::StereoBilateralResult)0;
	float4 blendedColor = centerColor;

	// depth == 0.0: VR HMD mask (pixels outside the lens area, never written by the engine)
	// depth == 1.0: sky/far plane (no real geometry, bilateral reprojection not meaningful)
	bool isSkipPixel = centerDepth < 1e-5 || centerDepth >= 1.0;
	if (!isSkipPixel) {
		// Normal bilateral blend path
		float4 srcEdgeDepths = SampleCrossDepths(dtid, 1, eyeIndex);
		if (Stereo::MaxDepthDiff(centerDepth, srcEdgeDepths) > kEdgeDepthThreshold) {
			debugState = 1;
		} else {
			r = Stereo::ReprojectToOtherEye(uv, centerDepth, eyeIndex, FrameDim);
			if (r.valid) {
				float otherDepth = DepthTexture[r.otherPx];

				float4 dstEdgeDepths = SampleCrossDepths(r.otherPx, kEdgeMargin, 1 - eyeIndex);
				if (any(dstEdgeDepths < 1e-5) || Stereo::MaxDepthDiff(otherDepth, dstEdgeDepths) > kEdgeDepthThreshold) {
					debugState = 2;
				} else {
					float4 otherColor = ColorTexture[r.otherPx];
					Stereo::FinalizeStereoBlend(r, uv, centerDepth, otherDepth, eyeIndex, FrameDim, DepthSigma, MaxBlendFactor);

					float colorDiff = abs(dot(centerColor.rgb, float3(0.2126, 0.7152, 0.0722)) -
										  dot(otherColor.rgb, float3(0.2126, 0.7152, 0.0722)));
					float colorGate = smoothstep(ColorDiffThreshold * 0.5, ColorDiffThreshold * 2.0, colorDiff);
					r.blendWeight *= colorGate;

					blendedColor = lerp(centerColor, otherColor, r.blendWeight);
					debugState = r.backCheckPassed ? 4 : 5;
				}
			} else {
				debugState = 3;
			}
		}
	}

#ifdef DEBUG_BACKCHECK
	// Debug visualization (6 states):
	//   Blue   = mask/sky: skipped
	//   Yellow = source edge: depth discontinuity at this pixel
	//   Orange = destination edge: depth discontinuity at reprojected pixel
	//   Grey   = out of bounds: other eye can't see this point
	//   Green  = back-check passed: surfaces match in both eyes
	//   Red    = back-check failed: blend penalized (occlusion edge)
	float3 debugColors[6] = {
		float3(0.1, 0.1, 0.5),  // 0: mask/sky - blue
		float3(0.8, 0.8, 0.0),  // 1: source edge - yellow
		float3(0.8, 0.4, 0.0),  // 2: destination edge - orange
		float3(0.3, 0.3, 0.3),  // 3: out of bounds - grey
		float3(0.0, 0.5, 0.0),  // 4: back-check passed - green
		float3(0.5, 0.0, 0.0)   // 5: back-check failed - red
	};
	OutputRW[dtid] = float4(lerp(centerColor.rgb, debugColors[debugState], 0.7), centerColor.a);
#elif defined(DEBUG_BLEND_WEIGHT)
	// Blend weight heatmap: only pixels with actual blend activity are colorized.
	// Untouched pixels pass through unmodified.
	float w = saturate(r.blendWeight / max(MaxBlendFactor, 1e-5));
	if (w > 1e-3) {
		float3 heatmap = Color::TurboColormap(w);
		OutputRW[dtid] = float4(lerp(centerColor.rgb, saturate(heatmap), 0.8), centerColor.a);
	} else {
		OutputRW[dtid] = centerColor;
	}
#elif defined(DEBUG_EDGE_DETECTION)
	// Edge detection visualizer: highlights pixels excluded by depth discontinuity checks.
	// Non-edge pixels show the normal blended output for scene context.
	//   Bright yellow = source edge: discontinuity at this pixel
	//   Bright orange = destination edge: discontinuity at reprojected pixel
	if (debugState == 1) {
		OutputRW[dtid] = float4(lerp(centerColor.rgb, float3(1.0, 1.0, 0.0), 0.8), centerColor.a);
	} else if (debugState == 2) {
		OutputRW[dtid] = float4(lerp(centerColor.rgb, float3(1.0, 0.5, 0.0), 0.8), centerColor.a);
	} else {
		OutputRW[dtid] = blendedColor;
	}
#else
	OutputRW[dtid] = blendedColor;
#endif

#endif  // STEREO_OVERWRITE
}
