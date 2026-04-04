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
Texture2D<float4> ReflectanceTexture : register(t3);  // .w = POM pixelOffset from Lighting pass
SamplerState LinearSampler : register(s0);

#	include "VRStereoOptimizations/modes.hlsli"

// Hardware bilinear color sample from reprojected pixel coordinates.
// Converts integer pixel coords to proper full-texture UV for SampleLevel,
// clamped to the active DRS viewport to prevent sampling stale data.
// Motion vectors stay as integer Load() — filtering them breaks DLSS.
float4 SampleReprojectedColor(float2 stereoUV, float2 frameDim)
{
	uint texW, texH;
	ColorTexture.GetDimensions(texW, texH);
	float2 texSize = float2(texW, texH);
	float2 minUV = 0.5 / texSize;
	float2 maxUV = (frameDim - 0.5) / texSize;
	stereoUV = clamp(stereoUV, minUV, maxUV);
	return ColorTexture.SampleLevel(LinearSampler, stereoUV, 0);
}
#endif

cbuffer StereoBlendCB : register(b1)
{
	float2 FrameDim;
	float2 RcpFrameDim;
	float DepthSigma;
	float MaxBlendFactor;
	float ColorDiffThreshold;
	float DebugEdgeTint;
	uint DebugMode;  // 0 = normal, 1 = depth map diagnostic, 2 = full blend depth visualizer, 3 = POM depth heatmap
	float FullBlendDistance;
	float POMDepthScale;
	float _pad;
};

static const float kEdgeDepthThreshold = 0.05;        // NDC depth difference above which a pixel is considered a depth discontinuity and excluded from stereo blend
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

	// Debug mode 3: POM depth data visualizer — show Reflectance.w as color
	if (DebugMode == 3) {
		float pomVal = ReflectanceTexture[dtid].w;
		float4 c = ColorTexture[dtid];
		if (pomVal > 1e-2) {
			// POM pixel: red-to-green gradient based on parallaxAmount
			// Red = peak (high pomVal, closer to camera), Green = valley (low pomVal, farther), Yellow = geometry plane
			float3 pomColor = float3(pomVal, 1.0 - pomVal, 0);
			OutputRW[dtid] = float4(lerp(c.rgb, pomColor, 0.7), c.a);
		}
		// Non-POM pixels (pomVal ~ 0) left untouched
		return;
	}

	// MODE_DISOCCLUDED: fully shaded, leave untouched
	if (pixelMode == MODE_DISOCCLUDED)
		return;

	// MODE_FULL_BLEND: bilateral blend for 2x supersampling
	if (pixelMode == MODE_FULL_BLEND) {
		float4 center = ColorTexture[dtid];

		// Check for POM depth offset at this pixel
		// pixelOffset = parallaxAmount (0-1) from ExtendedMaterials, 0.5 = geometry plane.
		// Values > 0.5 are peaks (closer to camera), < 0.5 are valleys (farther from camera).
		// Correction: high pomVal should push depth closer (smaller linear depth),
		// so we use (0.5 - pomOffset) to get a negative correction for peaks.
		// Non-POM pixels store 0.0, so threshold > 1e-2 distinguishes them.
		float reprojDepthFB = centerDepth;
		float pomOffsetFB = ReflectanceTexture[dtid].w;
		if (pomOffsetFB > 1e-2 && POMDepthScale > 0) {
			float linDepthFB = SharedData::GetScreenDepth(centerDepth);
			float depthCorrectionFB = (0.5 - pomOffsetFB) * POMDepthScale;
			float newLinDepthFB = max(linDepthFB + depthCorrectionFB, 1e-4);
			reprojDepthFB = (SharedData::CameraData.x - SharedData::CameraData.w / newLinDepthFB) / SharedData::CameraData.z;
		}

		// Reproject to the other eye
		Stereo::StereoBilateralResult r = Stereo::ReprojectToOtherEye(uv, reprojDepthFB, eyeIndex, FrameDim);
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

		float4 otherColor = SampleReprojectedColor(r.otherStereoUV, FrameDim);
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
		// Eye 0 (left eye): fully shaded for all modes — only apply debug tint to edge pixels
		if (DebugEdgeTint > 0 && pixelMode == MODE_EDGE) {
			float4 c = ColorTexture[dtid];
			OutputRW[dtid] = float4(lerp(c.rgb, float3(0, 1, 0), DebugEdgeTint), c.a);
		}
		return;
	}

	// Eye 1 (right eye): reproject all non-disoccluded, non-full-blend pixels
	// (MAIN, EDGE) from Eye 0 (left eye). In VR stereo rendering, Eye 0 is
	// fully shaded; Eye 1 pixels marked as reprojectable by StencilCS are
	// filled with reprojected color from Eye 0 to save GPU work.
	// StencilCS already performed the authoritative disocclusion check with the correct
	// depth buffer state — no redundant depth agreement check here.
	float reprojDepth = centerDepth;

	// First-pass reprojection to find Eye 0 source pixel
	Stereo::StereoBilateralResult r = Stereo::ReprojectToOtherEye(uv, reprojDepth, eyeIndex, FrameDim);
	if (!r.valid)
		return;

	// Save first-pass result as fallback before POM adjustment
	Stereo::StereoBilateralResult firstPassR = r;

	// Read POM offset from Eye 0 source's reflectance.w
	// pixelOffset = parallaxAmount (0-1) from ExtendedMaterials, 0.5 = geometry plane.
	// Values > 0.5 are peaks (closer to camera), < 0.5 are valleys (farther from camera).
	// Correction: high pomVal should push depth closer (smaller linear depth),
	// so we use (0.5 - pomOffset) to get a negative correction for peaks.
	// Non-POM pixels store 0.0, so threshold > 1e-2 distinguishes them.
	float pomOffset = ReflectanceTexture[r.otherPx].w;
	if (pomOffset > 1e-2) {
		// Re-reproject with POM-adjusted depth centered at geometry plane
		float linearDepth = SharedData::GetScreenDepth(centerDepth);
		float depthCorrection = (0.5 - pomOffset) * POMDepthScale;
		float newLinearDepth = max(linearDepth + depthCorrection, 1e-4);
		reprojDepth = (SharedData::CameraData.x - SharedData::CameraData.w / newLinearDepth) / SharedData::CameraData.z;
		r = Stereo::ReprojectToOtherEye(uv, reprojDepth, eyeIndex, FrameDim);
		if (!r.valid)
			r = firstPassR;  // Fall back to non-POM reprojection
	}

	// Skip if the Eye 0 source pixel is sky/unrendered (depth at clear value).
	// At DeferredPasses time, sky hasn't rendered yet — source would have clear color.
	// Let the sky/water pass fill these pixels later instead.
	float sourceDepth = DepthTexture[r.otherPx];
	if (sourceDepth >= 1.0 || sourceDepth < 1e-5) {
		// POM adjustment landed on sky — try the original first-pass source
		if (r.otherPx.x != firstPassR.otherPx.x || r.otherPx.y != firstPassR.otherPx.y) {
			float fallbackDepth = DepthTexture[firstPassR.otherPx];
			if (fallbackDepth < 1.0 && fallbackDepth >= 1e-5) {
				r = firstPassR;
			} else {
				return;
			}
		} else {
			return;
		}
	}

	OutputRW[dtid] = SampleReprojectedColor(r.otherStereoUV, FrameDim);
	MotionRW[dtid] = MotionRW[r.otherPx];

#else  // Normal bilateral blend path

#	ifdef EYE0_ONLY
	// Only process Eye 0 (left half) - Eye 1 left untouched
	float2 uvCheck = (dtid + 0.5) * RcpFrameDim;
	if (Stereo::GetEyeIndexFromTexCoord(uvCheck) == 1)
		return;
#	endif

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
		// Source edge detection: skip at depth discontinuities (arm/world silhouettes,
		// object edges). Saves VP reprojection work and prevents halo artifacts.
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

#	ifdef DEBUG_BACKCHECK
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
#	elif defined(DEBUG_BLEND_WEIGHT)
	// Blend weight heatmap: only pixels with actual blend activity are colorized.
	// Untouched pixels pass through unmodified.
	float w = saturate(r.blendWeight / max(MaxBlendFactor, 1e-5));
	if (w > 1e-3) {
		float3 heatmap = Color::TurboColormap(w);
		OutputRW[dtid] = float4(lerp(centerColor.rgb, saturate(heatmap), 0.8), centerColor.a);
	} else {
		OutputRW[dtid] = centerColor;
	}
#	elif defined(DEBUG_EDGE_DETECTION)
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
#	else
	OutputRW[dtid] = blendedColor;
#	endif

#endif  // STEREO_OVERWRITE
}
