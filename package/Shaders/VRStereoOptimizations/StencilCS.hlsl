// VR Stereo Optimizations - Stencil Classification Compute Shader
//
// Classifies BOTH eyes over the full SBS buffer. Each pixel is tagged as:
//   MODE_DISOCCLUDED    - Must be fully shaded (sky, HMD mask, parallax-occluded)
//   MODE_EDGE           - Depth edge boundary (dist 1) or inner/foreground band; fully shaded + bilateral blend
//   MODE_MAIN           - Standard pixel eligible for reprojection / bilateral blend
//   MODE_FULL_BLEND     - Near-camera geometry: both eyes fully shaded for 2x supersampling
//
// Dispatched over full SBS resolution (FrameDim.x x FrameDim.y).

#include "Common/SharedData.hlsli"
#include "Common/VR.hlsli"
#include "VRStereoOptimizations/cbuffers.hlsli"

Texture2D<float> DepthTexture : register(t0);

RWTexture2D<uint> ModeTextureRW : register(u0);

// Sentinel for the edge-detection search: means "no discontinuity found yet".
static const uint kEdgeDistNone = 0xFFFFFFFFu;

[numthreads(8, 8, 1)] void main(uint2 dtid : SV_DispatchThreadID) {
	if (any(dtid >= uint2(FrameDim)))
		return;

	// Determine which eye this pixel belongs to
	float2 uv = (float2(dtid) + 0.5) / FrameDim;
	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);

	// Read depth directly in SBS coords
	float centerDepth = DepthTexture[dtid];

#ifdef DEBUG_DEPTH_MAP
	// DIAGNOSTIC: Visualize what depth values StencilCS sees.
	// Green (MODE_EDGE) = depth >= 1.0 (HMD mask threshold)
	// Magenta (MODE_EDGE_NEIGHBOUR) = depth < EPSILON_DEPTH_SKY (sky threshold)
	// No tint (MODE_MAIN) = normal geometry with valid depth
	if (centerDepth >= 1.0) {
		ModeTextureRW[dtid] = MODE_EDGE;
		return;
	}
	if (centerDepth < EPSILON_DEPTH_SKY) {
		ModeTextureRW[dtid] = MODE_EDGE_NEIGHBOUR;
		return;
	}
	ModeTextureRW[dtid] = MODE_MAIN;
	return;
#endif

	// Sky/unrendered pixels (depth >= 1.0 at z-prepass time = depth buffer clear value)
	// and HMD mask pixels both have depth >= 1.0 here. Treat them the same as sky:
	// let edge detection run so geometry-vs-sky boundaries get classified.
	// HMD mask pixels are in lens corners with no nearby geometry, so they'll
	// fall through to MODE_DISOCCLUDED at the end.
	bool isSky = (centerDepth < EPSILON_DEPTH_SKY) || (centerDepth >= 1.0);
	float linCenter = isSky ? DEPTH_SKY_SENTINEL : SharedData::GetScreenDepth(centerDepth);

	// Near-camera supersampling: geometry closer than FullBlendDistance gets full
	// shading in both eyes for bilateral blend (2x supersampling in VRPostProcess).
	if (!isSky && linCenter < FullBlendDistance) {
		ModeTextureRW[dtid] = MODE_FULL_BLEND;
		return;
	}

	// --- Disocclusion detection via reprojection (runs for all non-sky pixels) ---
	// Early return: disoccluded pixels are always MODE_DISOCCLUDED regardless of edge proximity.
	// This ensures MinEdgeDistance never affects disocclusion classification.
	if (!isSky) {
		Stereo::StereoBilateralResult reproj = Stereo::ReprojectToOtherEye(
			uv,
			centerDepth,
			eyeIndex,
			FrameDim);

		bool isDisoccluded = false;
		if (!reproj.valid) {
			isDisoccluded = true;
		} else {
			float otherDepth = DepthTexture[reproj.otherPx];
			// Raw reversed-Z depth comparison for disocclusion detection.
			// Using raw depth avoids concentric semicircle artifacts that occur
			// with linearized depth due to precision band boundaries in the
			// hyperbolic depth-to-linear conversion.
			float maxRaw = max(max(centerDepth, otherDepth), EPSILON_DIVISION);
			float rawRelDiff = abs(centerDepth - otherDepth) / maxRaw;
			isDisoccluded = (rawRelDiff > DisocclusionThreshold);

			// Directional disocclusion: catches silhouette edge pixels where both eyes sample
			// similar linearized depth but Eye 0's color is wrong for Eye 1.  These slip through
			// the symmetric rawRelDiff check above.  The condition fires when Eye 0 is at similar
			// or slightly closer depth than Eye 1 (scale < 1.0), marking them disoccluded so Eye 1
			// renders natively.  ForwardOcclusionScale=0.5 triggers when Eye 0 is less than 2x Eye 1's
			// linearized depth; lower values are more aggressive, 0 = disabled.
			if (!isDisoccluded && eyeIndex == 1 && ForwardOcclusionScale > 0.0) {
				bool otherIsSky = (otherDepth < EPSILON_DEPTH_SKY) || (otherDepth >= 1.0);
				if (!otherIsSky) {
					float linOther = SharedData::GetScreenDepth(otherDepth);
					isDisoccluded = (linOther * ForwardOcclusionScale < linCenter);
				}
			}
		}

		if (isDisoccluded) {
			ModeTextureRW[dtid] = MODE_DISOCCLUDED;
			return;
		}
	}

	// Depth gate: skip edge detection for nearby geometry (saves perf, distant AA matters more)
	// Sky pixels always run edge detection — they need to expand the edge band outward.
	// Disocclusion detection (above) is independent of this gate and always runs.
	bool skipEdgeDetection = !isSky && (linCenter < MinEdgeDistance);

	// --- Edge detection with two-tier classification ---
	// MODE_EDGE:           immediate neighbor (distance 1) has depth discontinuity, OR
	//                      inner/foreground band (distance <= kInnerWidth).
	// kInnerWidth=4 provides enough margin at high VR resolutions (~8k wide) to catch
	// disocclusion boundary pixels that are just outside the immediate-neighbor band.
	static const uint kInnerWidth = 4;
	int2 offsets[4] = { int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1) };

	uint nearestEdgeDist = kEdgeDistNone;  // nearest distance at which a discontinuity was found
	bool nearestWeAreOuter = false;        // whether we are on the background side at that nearest hit

	// Use the larger of inner/outer widths for the search
	uint maxWidth = kInnerWidth;

	if (!skipEdgeDetection) {
		[loop] for (uint d = 1; d <= maxWidth; d++)
		{
			[unroll] for (int i = 0; i < 4; i++)
			{
				int2 rawNeighbor = int2(dtid) + offsets[i] * (int)d;
				uint2 neighborCoord = Stereo::ClampToEyeBounds(rawNeighbor, eyeIndex, FrameDim);

				float neighborDepth = DepthTexture[neighborCoord];
				bool neighborIsSky = (neighborDepth < EPSILON_DEPTH_SKY) || (neighborDepth >= 1.0);
				float linNeighbor = neighborIsSky ? DEPTH_SKY_SENTINEL : SharedData::GetScreenDepth(neighborDepth);
				float maxLin = max(max(linCenter, linNeighbor), EPSILON_DEPTH_SKY);
				float relDepthDiff = abs(linCenter - linNeighbor) / maxLin;

				if (relDepthDiff > EdgeDepthThreshold && d < nearestEdgeDist) {
					nearestEdgeDist = d;
					nearestWeAreOuter = (linNeighbor < linCenter);  // neighbor closer to camera = we are background
				}
			}
		}

	}  // !skipEdgeDetection

	if (nearestEdgeDist != kEdgeDistNone) {
		// Classify based on distance and side
		if (nearestEdgeDist == 1) {
			// Immediate neighbor discontinuity: always MODE_EDGE regardless of side
			ModeTextureRW[dtid] = MODE_EDGE;
			return;
		} else if (!nearestWeAreOuter && nearestEdgeDist <= kInnerWidth) {
			// Inner/foreground band beyond distance 1
			ModeTextureRW[dtid] = MODE_EDGE;
			return;
		}
	}

	// Sky pixels that aren't near edges -> disoccluded (reprojection is meaningless for sky)
	if (isSky) {
		ModeTextureRW[dtid] = MODE_DISOCCLUDED;
		return;
	}

	// Standard pixel
	ModeTextureRW[dtid] = MODE_MAIN;
}
