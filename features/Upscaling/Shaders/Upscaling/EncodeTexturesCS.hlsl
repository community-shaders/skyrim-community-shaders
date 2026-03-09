#include "Common/SharedData.hlsli"

cbuffer UpscalingData : register(b0)
{
	float2 ResolutionScale;
	float DepthDisocclusion;
	float pad0;
};

cbuffer JitterCB : register(b1)
{
	float2 jitter;
};

Texture2D<float2> TAAMask : register(t0);
Texture2D<float4> NormalsWaterMask : register(t1);
Texture2D<float2> MotionVectorMask : register(t2);
Texture2D<float> DepthMask : register(t3);
Texture2D<float> PrevDepth : register(t4);

RWTexture2D<float> ReactiveMask : register(u0);
RWTexture2D<float> TransparencyCompositionMask : register(u1);
RWTexture2D<float2> MotionVectorOutput : register(u2);

SamplerState PointSampler : register(s0);

float ScreenToViewDepth(const float screenDepth)
{
	return (SharedData::CameraData.w / (-screenDepth * SharedData::CameraData.z + SharedData::CameraData.x));
}

[numthreads(8, 8, 1)] void main(uint2 dispatchID : SV_DispatchThreadID) {
	const float2 trueSamplingDim = SharedData::BufferDim.xy * ResolutionScale;
	
	// Early exit if dispatch thread is outside true sampling dimensions	
	if (any(dispatchID.xy >= uint2(trueSamplingDim)))
		return;

	float2 taaMask = TAAMask[dispatchID.xy];
	float transparencyCompositionMask = NormalsWaterMask[dispatchID.xy].z;

	const float depth = DepthMask[dispatchID.xy];
	
	const float2 motionVector = MotionVectorMask[dispatchID.xy];

#if defined(DLSS) || defined(DLSS_RR)
	float nearFactor = smoothstep(4096.0 * 2.5, 0.0, SharedData::GetScreenDepth(depth));

	// Find longest motion vector in 5x5 neighborhood
	float2 longestMotionVector = motionVector;
	float maxMotionLengthSq = dot(motionVector, motionVector);

	[unroll]
	for (int y = -2; y <= 2; y++) {
		[unroll]
		for (int x = -2; x <= 2; x++) {
			int2 samplePos = int2(dispatchID.xy) + int2(x, y);

			// Skip samples outside true sampling dimensions
			if (any(samplePos < 0) || any(samplePos >= int2(trueSamplingDim)))
				continue;

			float neighborDepth = DepthMask[samplePos];

			// Take neighbor if it's longer AND closer
			if (neighborDepth < depth){
				float2 neighborMotionVector = MotionVectorMask[samplePos];

				// Square motion vector for length
				float motionLengthSq = dot(neighborMotionVector, neighborMotionVector);

				if (motionLengthSq > maxMotionLengthSq){
					maxMotionLengthSq = motionLengthSq;
					longestMotionVector = neighborMotionVector;
				}
			}
		}
	}

	MotionVectorOutput[dispatchID.xy] = lerp(longestMotionVector, motionVector, nearFactor);
#endif

	TransparencyCompositionMask[dispatchID.xy] = transparencyCompositionMask;
	
	const float2 curUV = (dispatchID.xy + .5f) / trueSamplingDim;
	const float2 prevUV = (curUV + motionVector.xy) * ResolutionScale;	
	
	const float currDepth = ScreenToViewDepth(depth);
	const float prevDepth = ScreenToViewDepth(PrevDepth.SampleLevel(PointSampler, prevUV, 0));

	float deptherror = abs(prevDepth - currDepth) / currDepth;
	
	float depthTolerance = DepthDisocclusion + 1e1f * length(motionVector.xy);
	
	float disocclusionMask = deptherror < depthTolerance ? 0.0f : 1.0f;

	float reactiveMask = max(taaMask.x * 0.25f + taaMask.y, disocclusionMask);
	ReactiveMask[dispatchID.xy] = reactiveMask;	
}