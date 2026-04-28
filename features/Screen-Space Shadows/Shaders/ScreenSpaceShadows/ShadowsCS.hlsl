#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"

// Interleaved Gradient Noise (Jorge Jimenez) — pseudo-random in [0, 1) per pixel.
float InterleavedGradientNoise(float2 pixelCoord)
{
	return frac(52.9829189 * frac(0.06711056 * pixelCoord.x + 0.00583715 * pixelCoord.y));
}


// Depth pyramid bindings.
//   srcDepth            (t0) — moments used for VSM samples along the ray.
//                              May be the blurred copy when BlurDepthPyramid is on,
//                              otherwise the raw prefiltered values.
//   srcDepthPrefiltered (t1) — raw prefiltered moments, never blurred.  Used for
//                              accurate per-pixel view-space position reconstruction
//                              at the receiver, so the ray origin isn't smeared.
Texture2D<float2> srcDepth            : register(t0);
Texture2D<float2> srcDepthPrefiltered : register(t1);
RWTexture2D<unorm float> shadowOutput : register(u0);

// R32G32_FLOAT does NOT support linear filtering in D3D11 — using a linear
// sampler silently returns (0, 0) and breaks every depth read.  Use point.
SamplerState samplerLinearClamp : register(s0);
SamplerState samplerPointClamp  : register(s1);

// Sample count is a compile-time define so the loop bound is a constant
// (the compiler can fold/unroll) and we sidestep the float3+uint cbuffer
// packing pitfall that produced random sample counts.  C++ recompiles this
// shader whenever the sample count derived from settings changes.
#ifndef MIP0_SAMPLE_COUNT
#	define MIP0_SAMPLE_COUNT 16
#endif

cbuffer SSSCB : register(b1)
{
	float2 FrameDim;
	float2 RcpTexDim;

	float2 TexDim;
	float2 DynamicRes;

	float SurfaceThickness;
	float ShadowContrast;
	float RayLength;
	uint CurrentMip;

	float3 LightWorldDir;
};

// Reconstruct view-space position from texUV in [0, DynamicRes] space and linear depth.
float3 ScreenToViewPos(float2 texUV, float linearDepth, uint eyeIdx)
{
#ifdef VR
	float2 uv01 = float2(
		(texUV.x - float(eyeIdx) * DynamicRes.x * 0.5) / (DynamicRes.x * 0.5),
		texUV.y / DynamicRes.y);
#else
	float2 uv01 = texUV / DynamicRes;
#endif
	float P00 = FrameBuffer::CameraProj[eyeIdx][0][0];
	float P11 = FrameBuffer::CameraProj[eyeIdx][1][1];
	float3 ret;
	ret.x = (2.0 / P00 * uv01.x - 1.0 / P00) * linearDepth;
	ret.y = (-2.0 / P11 * uv01.y + 1.0 / P11) * linearDepth;
	ret.z = linearDepth;
	return ret;
}

// Project a view-space position into texture UV [0, DynamicRes].
float2 ViewPosToTexUV(float3 posVS, uint eyeIdx)
{
	float2 screenUV = FrameBuffer::ViewToUV(posVS, true, eyeIdx);  // per-eye [0, 1]
#ifdef VR
	return float2(
		float(eyeIdx) * DynamicRes.x * 0.5 + screenUV.x * DynamicRes.x * 0.5,
		screenUV.y * DynamicRes.y);
#else
	return screenUV * DynamicRes;
#endif
}

static const float VSM_MIN_VARIANCE = 0.00001;
static const float VSM_BLEEDING_REDUCTION = 0.2;

// Chebyshev upper bound on P(X >= t)
// moments.x = mean(z), moments.y = mean(z^2)
float ComputeVSM(float2 moments, float depth)
{
	float variance = max(moments.y - moments.x * moments.x, VSM_MIN_VARIANCE);
	float d = depth - moments.x;
	float pMax = variance / (variance + d * d);
	return (depth <= moments.x) ? 1.0 : pMax;
}

// Chebyshev upper bound on P(X <= t)  (lower-tail form)
// moments.x = mean(z), moments.y = mean(z^2)
float ComputeVSM_Lower(float2 moments, float depth)
{
    float variance = max(moments.y - moments.x * moments.x, VSM_MIN_VARIANCE);
    float d = moments.x - depth;
    float pMax = variance / (variance + d * d);
    return (depth >= moments.x) ? 1.0 : pMax;
}

[numthreads(8, 8, 1)] void main(uint2 dtid
								: SV_DispatchThreadID) {
	uint mipScale = 1u << CurrentMip;
	uint2 effectiveFrameDim = uint2(FrameDim) >> CurrentMip;

#if defined(VR) && defined(RIGHT)
	static const uint eyeIndex = 1;
	uint2 actualDtid = uint2(dtid.x + effectiveFrameDim.x / 2u, dtid.y);
	if (actualDtid.x >= effectiveFrameDim.x || actualDtid.y >= effectiveFrameDim.y)
		return;
#elif defined(VR)
	static const uint eyeIndex = 0;
	uint2 actualDtid = dtid;
	if (dtid.x >= effectiveFrameDim.x / 2u || dtid.y >= effectiveFrameDim.y)
		return;
#else
	static const uint eyeIndex = 0;
	uint2 actualDtid = dtid;
	if (any(float2(dtid) >= float2(effectiveFrameDim)))
		return;
#endif

	float2 startUV = (float2(actualDtid) + 0.5) * RcpTexDim * float(mipScale);
	
	// Use the raw prefiltered depth here so the receiver's view-space position
	// is reconstructed from its actual per-pixel depth, not a blurred neighbour.
	float2 startMoment = srcDepthPrefiltered.SampleLevel(samplerPointClamp, startUV, CurrentMip);
	float startDepth = startMoment.x;

	float shadow = 1.0;

	// 0.0 depth is the sentinel written by PrefilterDepthsCS for sky / invalid pixels.
	if (startDepth > 0.0) {
		float3 startPosVS = ScreenToViewPos(startUV, startDepth, eyeIndex);
		float3 lightViewDir = normalize(mul((float3x3)FrameBuffer::CameraView[eyeIndex], LightWorldDir));

		float mipRayLength = RayLength * float(1u << CurrentMip);
		float3 endPosVS = startPosVS + lightViewDir * mipRayLength;
		float2 endUV = ViewPosToTexUV(endPosVS, eyeIndex);

		float jitter = InterleavedGradientNoise(float2(actualDtid));

		uint numSamples = uint(16) << CurrentMip;                          // 4, 8, 16, 32, 64

		[loop] for (uint i = 1; i <= numSamples; i++)
		{
			float t = float(i + jitter) / float(numSamples);
			float2 sampleUV = lerp(startUV, endUV, t);
			float3 rayPosVS = lerp(startPosVS, endPosVS, t);

			if (saturate(sampleUV.x) == sampleUV.x && saturate(sampleUV.y) == sampleUV.y)
			{
				float2 moments = srcDepth.SampleLevel(samplerPointClamp, sampleUV, CurrentMip);	

				float shadowFront = ComputeVSM(moments.xy, rayPosVS.z);
				float shadowBack  = ComputeVSM_Lower(moments.xy, rayPosVS.z - SurfaceThickness);

				// Lit if either: occluder is behind receiver (shadowFront high)
				//             or occluder is in front of back plane (shadowBack high)
				shadow *= 1.0 - (1.0 - shadowFront) * (1.0 - shadowBack);
			}
		}
	}

	shadowOutput[actualDtid] = shadow;
}
