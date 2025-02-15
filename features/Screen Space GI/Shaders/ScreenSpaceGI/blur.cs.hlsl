// FAST DENOISING WITH SELF-STABILIZING RECURRENT BLURS
// 	https://developer.download.nvidia.com/video/gputechconf/gtc/2020/presentations/s22699-fast-denoising-with-self-stabilizing-recurrent-blurs.pdf

#include "Common/FastMath.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/Math.hlsli"
#include "Common/Random.hlsli"
#include "Common/VR.hlsli"
#include "ScreenSpaceGI/common.hlsli"

Texture2D<half> srcDepth : register(t0);
Texture2D<half4> srcNormalRoughness : register(t1);
Texture2D<unorm float> srcAccumFrames : register(t2);  // maybe half-res
Texture2D<float4> srcIlY : register(t3);               // maybe half-res
Texture2D<float2> srcIlCoCg : register(t4);            // maybe half-res

RWTexture2D<unorm float> outAccumFrames : register(u0);
RWTexture2D<float4> outIlY : register(u1);
RWTexture2D<float2> outIlCoCg : register(u2);

// samples = 8, min distance = 0.5, average samples on radius = 2
static const float3 g_Poisson8[8] = {
	float3(-0.4706069, -0.4427112, +0.6461146),
	float3(-0.9057375, +0.3003471, +0.9542373),
	float3(-0.3487388, +0.4037880, +0.5335386),
	float3(+0.1023042, +0.6439373, +0.6520134),
	float3(+0.5699277, +0.3513750, +0.6695386),
	float3(+0.2939128, -0.1131226, +0.3149309),
	float3(+0.7836658, -0.4208784, +0.8895339),
	float3(+0.1564120, -0.8198990, +0.8346850)
};

float GaussianWeight(float r)
{
	return exp(-0.66 * r * r);
}

// http://marc-b-reynolds.github.io/quaternions/2016/07/06/Orthonormal.html
float3x3 getBasis(float3 N)
{
	float sz = sign(N.z);
	float a = 1.0 / (sz + N.z);
	float ya = N.y * a;
	float b = N.x * ya;
	float c = N.x * sz;

	float3 T = float3(c * N.x * a - 1.0, sz * b, c);
	float3 B = float3(b, N.y * ya - sz, N.y);

	// Note: due to the quaternion formulation, the generated frame is rotated by 180 degrees,
	// s.t. if N = (0, 0, 1), then T = (-1, 0, 0) and B = (0, -1, 0).
	return float3x3(T, B, N);
}

// D - Dominant reflection direction
float2x3 getKernelBasis(float3 D, float3 N, float roughness = 1.0, float anisoFade = 1.0)
{
	float3x3 basis = getBasis(N);

	float3 T = basis[0];
	float3 B = basis[1];

	float NoD = dot(N, D);
	if (NoD < 0.999) {
		float3 R = reflect(-D, N);
		T = normalize(cross(N, R));
		B = cross(R, T);

		float skewFactor = lerp(0.5 + 0.5 * roughness, 1.0, NoD);
		skewFactor = lerp(skewFactor, 1.0, anisoFade);

		B /= skewFactor;
	}

	return float2x3(T, B);
}

// TODO: spinning blur
float2x2 getRotationMatrix(float noise)
{
	float2 sin_cos;
	sincos(noise * Math::PI * 2, sin_cos.y, sin_cos.x);
	return float2x2(sin_cos.x, sin_cos.y, -sin_cos.y, sin_cos.x);
}

[numthreads(8, 8, 1)] void main(const uint2 dtid
								: SV_DispatchThreadID) {
	const float2 frameScale = FrameDim * RcpTexDim;

	float radius = BlurRadius;
#ifdef TEMPORAL_DENOISER
	float accumFrames = srcAccumFrames[dtid];
	radius = lerp(radius, 2, 1 / (1 + accumFrames * 255));
#endif
	const uint numSamples = 8;

	const float2 uv = (dtid + .5) * RCP_OUT_FRAME_DIM;
	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);
	const float2 screenPos = Stereo::ConvertFromStereoUV(uv, eyeIndex);

	float depth = READ_DEPTH(srcDepth, dtid);
	float3 pos = ScreenToViewPosition(screenPos, depth, eyeIndex);
	float3 normal = GBuffer::DecodeNormal(FULLRES_LOAD(srcNormalRoughness, dtid, uv, samplerLinearClamp).xy);

	const float2 pixelDirRBViewspaceSizeAtCenterZ = depth.xx * (eyeIndex == 0 ? NDCToViewMul.xy : NDCToViewMul.zw) * RCP_OUT_FRAME_DIM;
	const float worldRadius = radius * pixelDirRBViewspaceSizeAtCenterZ.x;
	float2x3 TvBv = getKernelBasis(normal, normal);  // D = N
	float halfAngle = Math::HALF_PI;

	TvBv[0] *= worldRadius;
	TvBv[1] *= worldRadius;
#ifdef TEMPORAL_DENOISER
	halfAngle *= 1 - lerp(0, 0.8, sqrt(accumFrames / (float)MaxAccumFrames));
#endif

	const float4 ilY = srcIlY[dtid];
	const float2 ilCoCg = srcIlCoCg[dtid];

	float4 ySum = ilY;
	float2 coCgSum = ilCoCg;
#if defined(TEMPORAL_DENOISER)
	float fSum = accumFrames;
#endif
	float wSum = 1;
	for (uint i = 0; i < numSamples; i++) {
		float w = GaussianWeight(g_Poisson8[i].z);

		float2 poissonOffset = g_Poisson8[i].xy;

		float3 posOffset = TvBv[0] * poissonOffset.x + TvBv[1] * poissonOffset.y;
		float4 screenPosSample = mul(FrameBuffer::CameraProj[eyeIndex], float4(pos + posOffset, 1));
		screenPosSample.xy /= screenPosSample.w;
		screenPosSample.y = -screenPosSample.y;
		screenPosSample.xy = screenPosSample.xy * .5 + .5;

		// old method without kernel transform
		// float2 pxOffset = radius * poissonOffset.xy;
		// float2 pxSample = dtid + .5 + pxOffset;
		// float2 uvSample = (floor(pxSample) + 0.5) * RCP_OUT_FRAME_DIM;  // Snap to the pixel centre
		// float2 screenPosSample = Stereo::ConvertFromStereoUV(uvSample, eyeIndex);

		if (any(screenPosSample.xy < 0) || any(screenPosSample.xy > 1))
			continue;

		float2 uvSample = Stereo::ConvertToStereoUV(screenPosSample.xy, eyeIndex);
		uvSample = (floor(uvSample * OUT_FRAME_DIM) + 0.5) * RCP_OUT_FRAME_DIM;  // Snap to the pixel centre

		float depthSample = srcDepth.SampleLevel(samplerPointClamp, uvSample * frameScale, RES_MIP);
		float3 posSample = ScreenToViewPosition(screenPosSample.xy, depthSample, eyeIndex);

		float4 normalRoughnessSample = srcNormalRoughness.SampleLevel(samplerPointClamp, uvSample * frameScale, 0);
		float3 normalSample = GBuffer::DecodeNormal(normalRoughnessSample.xy);

		// geometry weight
		w *= saturate(1 - abs(dot(normal, posSample - pos)) * DistanceNormalisation);
		// normal weight
		w *= 1 - saturate(FastMath::acosFast4(saturate(dot(normalSample, normal))) / halfAngle);

		w = max(w, 0.01);

		if (w > 1e-8) {
			ySum += srcIlY.SampleLevel(samplerPointClamp, uvSample * OUT_FRAME_SCALE, 0) * w;
			coCgSum += srcIlCoCg.SampleLevel(samplerPointClamp, uvSample * OUT_FRAME_SCALE, 0) * w;
#if defined(TEMPORAL_DENOISER)
			fSum += srcAccumFrames.SampleLevel(samplerPointClamp, uvSample * OUT_FRAME_SCALE, 0) * w;
#endif
			wSum += w;
		}
	}

	outIlY[dtid] = ySum / wSum;
	outIlCoCg[dtid] = coCgSum / wSum;
#if defined(TEMPORAL_DENOISER)
	outAccumFrames[dtid] = fSum / wSum;
#endif
}