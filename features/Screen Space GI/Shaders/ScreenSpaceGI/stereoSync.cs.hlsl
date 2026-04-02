// Stereo Sync - Bilateral blend of SSGI buffers between eyes
//
// Reprojects each pixel to the other eye and blends AO/IL based on depth
// agreement. Runs after the SSGI blur to reduce per-eye GI disparities.
//
// Based on: Shi, Billeter, Eisemann 2022, "Stereo-consistent screen-space
// ambient occlusion" https://eprints.whiterose.ac.uk/id/eprint/187713/

#include "Common/FrameBuffer.hlsli"
#include "Common/VR.hlsli"
#include "ScreenSpaceGI/common.hlsli"

#ifdef VR

Texture2D<float> srcDepth : register(t0);
Texture2D<float> srcAo : register(t1);
Texture2D<float4> srcIlY : register(t2);
Texture2D<float2> srcIlCoCg : register(t3);

#	if defined(VR_STEREO_OPT)
Texture2D<uint> StereoOptModeTexture : register(t16);
#	endif

RWTexture2D<float> outAo : register(u0);
RWTexture2D<float4> outIlY : register(u1);
RWTexture2D<float2> outIlCoCg : register(u2);

static const float kDepthSigma = 0.01;       // Bilateral depth tolerance (NDC): surfaces within this range are considered the same and blended
static const float kMaxBlend = 0.5;          // Maximum stereo blend weight; 0.5 gives equal weighting between eyes
static const float kEdgeRelThreshold = 0.5;  // Relative linear-depth difference above which a pixel is a depth discontinuity (50% change)
static const float kMaskDepth = 0.01;        // Linear depth sentinel: values below this are outside the HMD lens area
static const int kEdgeMargin = 2;            // Neighbor offset (pixels) for destination edge + mask boundary check

// Writes all output channels from the source buffers (passthrough / no-blend path).
void Passthrough(uint2 dtid)
{
	outAo[dtid] = srcAo[dtid];
	outIlY[dtid] = srcIlY[dtid];
	outIlCoCg[dtid] = srcIlCoCg[dtid];
}

// Samples four depth neighbors in a cross pattern (±step.x, ±step.y) around centerUV,
// scaled by texScale to map from output UV space to texture sample coords.
// centerUV is clamped to eyeIndex's half of the stereo buffer before offsetting
// to prevent neighbor reads from crossing the x=0.5 seam into the other eye.
float4 SampleCrossDepths(float2 centerUV, float2 step, float2 texScale, uint eyeIndex)
{
	float2 uv = Stereo::ClampToEyeUV(centerUV, eyeIndex);
	return float4(
		srcDepth.SampleLevel(samplerPointClamp, (uv + float2(step.x, 0)) * texScale, RES_MIP),
		srcDepth.SampleLevel(samplerPointClamp, (uv + float2(-step.x, 0)) * texScale, RES_MIP),
		srcDepth.SampleLevel(samplerPointClamp, (uv + float2(0, step.y)) * texScale, RES_MIP),
		srcDepth.SampleLevel(samplerPointClamp, (uv + float2(0, -step.y)) * texScale, RES_MIP));
}

[numthreads(8, 8, 1)] void main(uint2 dtid : SV_DispatchThreadID) {
	const float2 outFrameDim = OUT_FRAME_DIM;
	if (any(dtid >= uint2(outFrameDim)))
		return;

	const float2 frameScale = FrameDim * RcpTexDim;
	float2 uv = (dtid + 0.5) / outFrameDim;

	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);

#	if defined(VR_STEREO_OPT)
	if (eyeIndex == 1) {
		uint2 fullResPx = uint2(uv * FrameDim);
		uint mode = StereoOptModeTexture[fullResPx];
		if (mode == 1 || mode == 2) {
			Passthrough(dtid);
			return;
		}
	}
#	endif

	// SSGI working depth is linear view-space Z.
	// 0.0 = mask (outside lens area). FP_Z = first-person hands threshold (~18.0).
	float depth = srcDepth.SampleLevel(samplerPointClamp, uv * frameScale, RES_MIP);
	if (depth < FP_Z) {
		Passthrough(dtid);
		return;
	}

	// Source edge detection: skip stereo sync at depth discontinuities.
	// Uses a relative threshold since depth is linear view-space (not NDC).
	// Placed before rawDepth conversion and reprojection to save VP matrix work
	// for edge pixels.
	float2 pixelStep = 1.0 / outFrameDim;
	float4 srcNeighborDepths = SampleCrossDepths(uv, pixelStep, frameScale, eyeIndex);
	if (Stereo::MaxDepthDiff(depth, srcNeighborDepths) / max(depth, 1.0) > kEdgeRelThreshold) {
		Passthrough(dtid);
		return;
	}

	// Convert linear depth to raw depth (NDC Z) for reprojection matrix math.
	// raw = (CameraData.x - CameraData.w / depth) / CameraData.z
	// where x=n*f, w=f, z=f-n
	float rawDepth = (SharedData::CameraData.x - SharedData::CameraData.w / depth) / SharedData::CameraData.z;

	Stereo::StereoBilateralResult r = Stereo::ReprojectToOtherEye(uv, rawDepth, eyeIndex, outFrameDim);

	if (!r.valid) {
		Passthrough(dtid);
		return;
	}

	float otherLinearDepth = srcDepth.SampleLevel(samplerPointClamp, r.otherStereoUV * frameScale, RES_MIP);
	if (otherLinearDepth < FP_Z) {
		Passthrough(dtid);
		return;
	}

	// Destination edge detection: skip if the reprojected pixel is near the HMD mask
	// boundary or at a depth discontinuity in the other eye. Due to VR parallax the
	// arm silhouette appears at a different screen position per eye, so the reprojection
	// can cross a boundary invisible from this eye's perspective.
	float2 marginStep = float(kEdgeMargin) / outFrameDim;
	float4 otherNeighborDepths = SampleCrossDepths(r.otherStereoUV, marginStep, frameScale, 1 - eyeIndex);
	if (any(otherNeighborDepths < kMaskDepth) ||
		Stereo::MaxDepthDiff(otherLinearDepth, otherNeighborDepths) / max(otherLinearDepth, 1.0) > kEdgeRelThreshold) {
		Passthrough(dtid);
		return;
	}

	float otherRawDepth = (SharedData::CameraData.x - SharedData::CameraData.w / otherLinearDepth) / SharedData::CameraData.z;

	// Back-check disabled: source + destination edge detection covers the occlusion
	// boundary cases it was guarding, saving 2 VP matrix multiplies per blended pixel.
	Stereo::FinalizeStereoBlend(r, uv, rawDepth, otherRawDepth, eyeIndex, outFrameDim, kDepthSigma, kMaxBlend, 0.0);

	outAo[dtid] = lerp(srcAo[dtid], srcAo[r.otherPx], r.blendWeight);
	outIlY[dtid] = lerp(srcIlY[dtid], srcIlY[r.otherPx], r.blendWeight);
	outIlCoCg[dtid] = lerp(srcIlCoCg[dtid], srcIlCoCg[r.otherPx], r.blendWeight);
}

#endif  // VR
