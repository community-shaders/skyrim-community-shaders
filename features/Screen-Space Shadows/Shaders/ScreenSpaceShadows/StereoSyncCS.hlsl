// Stereo Sync - Bilateral blend of SSS shadow buffer between eyes
//
// Reprojects each pixel to the other eye and blends shadow values based on
// depth agreement with back-check validation. Runs after the raymarch pass
// to reduce per-eye shadow disparities in VR.
//
// Based on: Shi, Billeter, Eisemann 2022, "Stereo-consistent screen-space
// ambient occlusion" https://eprints.whiterose.ac.uk/id/eprint/187713/

#include "Common/FrameBuffer.hlsli"
#include "Common/VR.hlsli"

#ifdef VR

Texture2D<float> DepthTexture : register(t0);
Texture2D<unorm half> SrcShadowTexture : register(t1);

RWTexture2D<unorm half> OutShadowTexture : register(u0);

cbuffer StereoSyncCB : register(b1)
{
	float2 FrameDim;
	float2 RcpFrameDim;
};

static const float kDepthSigma = 0.01;
static const float kMaxBlend = 1.0;
static const float kBackCheckThreshold = 8.0;

[numthreads(8, 8, 1)] void main(uint2 dtid : SV_DispatchThreadID) {
	if (any(dtid >= uint2(FrameDim)))
		return;

	float2 uv = (dtid + 0.5) * RcpFrameDim;

	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);

	float depth = DepthTexture[dtid];

	// depth == 0: VR HMD mask; depth == 1: sky/far plane
	if (depth < 1e-5 || depth >= 1.0) {
		OutShadowTexture[dtid] = SrcShadowTexture[dtid];
		return;
	}

	Stereo::StereoBilateralResult r = Stereo::ReprojectToOtherEye(uv, depth, eyeIndex, FrameDim);

	if (!r.valid) {
		OutShadowTexture[dtid] = SrcShadowTexture[dtid];
		return;
	}

	float otherDepth = DepthTexture[r.otherPx];

	// Skip if other eye sees mask or sky
	if (otherDepth < 1e-5 || otherDepth >= 1.0) {
		OutShadowTexture[dtid] = SrcShadowTexture[dtid];
		return;
	}

	// Reject if reprojected pixel is near the HMD mask boundary.
	// The raymarch at edge pixels samples into the mask (depth=0 -> treated as
	// far plane), producing incorrect shadow values that cause a visible seam.
	static const int kEdgeMargin = 2;
	[unroll] for (int dx = -kEdgeMargin; dx <= kEdgeMargin; dx += kEdgeMargin)
	{
		[unroll] for (int dy = -kEdgeMargin; dy <= kEdgeMargin; dy += kEdgeMargin)
		{
			float neighborDepth = DepthTexture[r.otherPx + int2(dx, dy)];
			if (neighborDepth < 1e-5) {
				OutShadowTexture[dtid] = SrcShadowTexture[dtid];
				return;
			}
		}
	}

	Stereo::FinalizeStereoBlend(r, uv, depth, otherDepth, eyeIndex, FrameDim, kDepthSigma, kMaxBlend, kBackCheckThreshold);

	float myShadow = SrcShadowTexture[dtid];
	float otherShadow = SrcShadowTexture[r.otherPx];

	// For shadows, use min (darkest) when depths agree well.
	// If either eye detected an occluder, that shadow should be visible.
	// The bilateral blend weight gates this -- if depths disagree (different
	// surfaces), we fall back toward our own eye's value.
	//
	// blend=0: keep myShadow unchanged
	// blend>0: lerp toward min(myShadow, otherShadow)
	float combined = min(myShadow, otherShadow);
	OutShadowTexture[dtid] = lerp(myShadow, combined, r.blendWeight);
}

#endif  // VR
