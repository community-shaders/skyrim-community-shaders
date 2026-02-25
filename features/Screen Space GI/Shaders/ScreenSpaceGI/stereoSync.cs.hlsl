// Stereo Sync - Bilateral blend of SSGI buffers between eyes
//
// Reprojects each pixel to the other eye and blends AO/IL based on depth
// agreement with back-check validation. Runs after the SSGI blur to reduce
// per-eye GI disparities.
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

RWTexture2D<float> outAo : register(u0);
RWTexture2D<float4> outIlY : register(u1);
RWTexture2D<float2> outIlCoCg : register(u2);

static const float kDepthSigma = 0.01;
static const float kMaxBlend = 0.5;
static const float kBackCheckThreshold = 8.0;

[numthreads(8, 8, 1)] void main(uint2 dtid : SV_DispatchThreadID) {
	const float2 outFrameDim = OUT_FRAME_DIM;
	if (any(dtid >= uint2(outFrameDim)))
		return;

	const float2 frameScale = FrameDim * RcpTexDim;
	float2 uv = (dtid + 0.5) / outFrameDim;

	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);

	// SSGI working depth is linear view-space Z.
	// 0.0 = mask (outside lens area). FP_Z = first-person hands threshold (~18.0).
	float depth = srcDepth.SampleLevel(samplerPointClamp, uv * frameScale, RES_MIP);
	if (depth < FP_Z) {
		outAo[dtid] = srcAo[dtid];
		outIlY[dtid] = srcIlY[dtid];
		outIlCoCg[dtid] = srcIlCoCg[dtid];
		return;
	}

	// Convert linear depth to raw depth (NDC Z) for reprojection matrix math.
	// raw = (CameraData.x - CameraData.w / depth) / CameraData.z
	// where x=n*f, w=f, z=f-n
	float rawDepth = (SharedData::CameraData.x - SharedData::CameraData.w / depth) / SharedData::CameraData.z;

	Stereo::StereoBilateralResult r = Stereo::ReprojectToOtherEye(uv, rawDepth, eyeIndex, outFrameDim);

	if (!r.valid) {
		outAo[dtid] = srcAo[dtid];
		outIlY[dtid] = srcIlY[dtid];
		outIlCoCg[dtid] = srcIlCoCg[dtid];
		return;
	}

	float otherLinearDepth = srcDepth.SampleLevel(samplerPointClamp, r.otherStereoUV * frameScale, RES_MIP);
	if (otherLinearDepth < FP_Z) {
		outAo[dtid] = srcAo[dtid];
		outIlY[dtid] = srcIlY[dtid];
		outIlCoCg[dtid] = srcIlCoCg[dtid];
		return;
	}
	float otherRawDepth = (SharedData::CameraData.x - SharedData::CameraData.w / otherLinearDepth) / SharedData::CameraData.z;

	// Use raw depth for back-check reprojection (required) and bilateral weight (consistent with StereoBlendCS)
	Stereo::FinalizeStereoBlend(r, uv, rawDepth, otherRawDepth, eyeIndex, outFrameDim, kDepthSigma, kMaxBlend, kBackCheckThreshold);

	outAo[dtid] = lerp(srcAo[dtid], srcAo[r.otherPx], r.blendWeight);
	outIlY[dtid] = lerp(srcIlY[dtid], srcIlY[r.otherPx], r.blendWeight);
	outIlCoCg[dtid] = lerp(srcIlCoCg[dtid], srcIlCoCg[r.otherPx], r.blendWeight);
}

#endif  // VR
