// Stereo Sync + Blur - Combined bilateral stereo blend and depth-weighted
// blur for VR screen-space shadows. Runs as a single compute pass after the
// raymarch to both synchronize shadow data between eyes and smooth per-pixel
// noise.
//
// Based on: Shi, Billeter, Eisemann 2022, "Stereo-consistent screen-space
// ambient occlusion" https://eprints.whiterose.ac.uk/id/eprint/187713/

#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/VR.hlsli"

#ifdef VR

Texture2D<float> SrcDepthTexture : register(t0);
Texture2D<unorm half> SrcShadowTexture : register(t1);

RWTexture2D<unorm half> OutShadowTexture : register(u0);

cbuffer StereoSyncCB : register(b1)
{
	float2 FrameDim;
	float2 RcpFrameDim;
};

static const float kDepthSigma = 0.01;          // Bilateral depth tolerance (NDC): surfaces within this range are considered the same and blended
static const float kMaxBlend = 1.0;             // Maximum stereo blend weight; reduce below 1.0 to soften the cross-eye contribution
static const float kEdgeDepthThreshold = 0.05;  // NDC depth difference above which a pixel is considered a depth discontinuity and excluded from stereo sync

float MaxDepthDiff(float center, float4 neighbors)
{
	return max(max(abs(center - neighbors.x), abs(center - neighbors.y)),
		max(abs(center - neighbors.z), abs(center - neighbors.w)));
}

// Depth-weighted 4-sample blur using a rotated Poisson disk.
// Uses dtid hash for per-pixel rotation to break structured patterns.
float BlurShadow(int2 dtid, float centerDepth)
{
	// Per-pixel rotation from interleaved gradient noise
	float noise = frac(52.9829189 * frac(0.06711056 * dtid.x + 0.00583715 * dtid.y));
	float angle = noise * 6.28318530718;
	float sn, cs;
	sincos(angle, sn, cs);
	float2x2 rot = float2x2(cs, sn, -sn, cs);

	static const float2 kOffsets[4] = {
		float2(0.382, 0.892),
		float2(0.491, 0.217),
		float2(0.938, 0.735),
		float2(0.009, 0.056),
	};

	float weight = 0;
	float shadow = 0;

	[unroll] for (uint i = 0; i < 4; i++)
	{
		float2 offset = mul(kOffsets[i], rot);
		int2 samplePx = dtid + int2(offset * 2.5);
		samplePx = clamp(samplePx, int2(0, 0), int2(FrameDim) - 1);

		float sampleDepth = SrcDepthTexture[samplePx];

		if (sampleDepth < 1e-5)
			continue;

		float attenuation = 1.0 - saturate(100.0 * abs(sampleDepth - centerDepth) / max(centerDepth, 1e-5));

		if (attenuation > 0.0) {
			shadow += SrcShadowTexture[samplePx] * attenuation;
			weight += attenuation;
		}
	}

	return weight > 0.0 ? shadow / weight : SrcShadowTexture[dtid];
}

[numthreads(8, 8, 1)] void main(uint2 dtid : SV_DispatchThreadID) {
	if (any(dtid >= uint2(FrameDim)))
		return;

	float2 uv = (dtid + 0.5) * RcpFrameDim;

	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);

	float depth = SrcDepthTexture[dtid];

	// depth == 0: VR HMD mask; depth == 1: sky/far plane
	if (depth < 1e-5 || depth >= 1.0) {
		OutShadowTexture[dtid] = SrcShadowTexture[dtid];
		return;
	}

	// Skip stereo sync for first-person geometry interior (hands/weapons).
	// Placed before the blur: arm shadow is uniform so the bilateral blur
	// would return SrcShadowTexture[dtid] unchanged anyway.
	float linearDepth = SharedData::GetScreenDepth(depth);
	if (linearDepth < VR_FP_Z) {
		OutShadowTexture[dtid] = SrcShadowTexture[dtid];
		return;
	}

	// Skip stereo sync at depth discontinuities (arm/world silhouettes, object edges).
	// Placed before the blur: the bilateral depth weighting zeroes out cross-edge
	// samples, so the blur collapses to SrcShadowTexture[dtid] at these pixels anyway.
	float4 edgeDepths = float4(
		SrcDepthTexture[dtid + int2(1, 0)],
		SrcDepthTexture[dtid + int2(-1, 0)],
		SrcDepthTexture[dtid + int2(0, 1)],
		SrcDepthTexture[dtid + int2(0, -1)]);
	if (MaxDepthDiff(depth, edgeDepths) > kEdgeDepthThreshold) {
		OutShadowTexture[dtid] = SrcShadowTexture[dtid];
		return;
	}

	// Depth-weighted blur on this eye's shadow data.
	// Only reached by world pixels that will attempt stereo sync.
	float myShadow = BlurShadow(dtid, depth);

	Stereo::StereoBilateralResult r = Stereo::ReprojectToOtherEye(uv, depth, eyeIndex, FrameDim);

	if (!r.valid) {
		OutShadowTexture[dtid] = myShadow;
		return;
	}

	float otherDepth = SrcDepthTexture[r.otherPx];

	// Skip if other eye sees mask, sky, or first-person geometry
	if (otherDepth < 1e-5 || otherDepth >= 1.0 || SharedData::GetScreenDepth(otherDepth) < VR_FP_Z) {
		OutShadowTexture[dtid] = myShadow;
		return;
	}

	// Reject if reprojected pixel is near the HMD mask boundary, or if it sits
	// at a depth discontinuity in the other eye. The source-side edge check above
	// only fires when *this* eye sees the boundary; due to VR parallax the arm
	// silhouette appears at a different screen position in each eye, so the
	// reprojection can cross a boundary invisible from this eye's perspective.
	// Reusing the same four neighbor reads covers both purposes at no extra cost.
	static const int kEdgeMargin = 2;
	float4 otherNeighbors = float4(
		SrcDepthTexture[r.otherPx + int2(-kEdgeMargin, 0)],
		SrcDepthTexture[r.otherPx + int2(kEdgeMargin, 0)],
		SrcDepthTexture[r.otherPx + int2(0, -kEdgeMargin)],
		SrcDepthTexture[r.otherPx + int2(0, kEdgeMargin)]);
	if (any(otherNeighbors < 1e-5) || MaxDepthDiff(otherDepth, otherNeighbors) > kEdgeDepthThreshold) {
		OutShadowTexture[dtid] = myShadow;
		return;
	}

	// Source + destination edge detection
	Stereo::FinalizeStereoBlend(r, uv, depth, otherDepth, eyeIndex, FrameDim, kDepthSigma, kMaxBlend, 0.0);

	float otherShadow = SrcShadowTexture[r.otherPx];

	// Use min (darkest) when depths agree: if either eye detected an
	// occluder, that shadow should be visible.
	float combined = min(myShadow, otherShadow);
	OutShadowTexture[dtid] = lerp(myShadow, combined, r.blendWeight);
}

#endif  // VR
