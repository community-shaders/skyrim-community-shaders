#include "Common/SharedData.hlsli"

cbuffer UpscalingData : register(b0)
{
	float2 TrueSamplingDim;  // BufferDim.xy * ResolutionScale
	float2 pad0;
};

Texture2D<float2> TAAMask : register(t0);
Texture2D<float4> NormalsWaterMask : register(t1);
Texture2D<float2> MotionVectorMask : register(t2);
Texture2D<float> DepthMask : register(t3);

RWTexture2D<float> ReactiveMask : register(u0);

#if defined(DLSS) || defined(FSR)
RWTexture2D<float> TransparencyCompositionMask : register(u1);
#endif

RWTexture2D<float2> MotionVectorOutput : register(u2);

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	// Early exit if dispatch thread is outside true sampling dimensions
	if (any(dispatchID.xy >= uint2(TrueSamplingDim)))
		return;

	float2 taaMask = TAAMask[dispatchID.xy];
	float transparencyCompositionMask = NormalsWaterMask[dispatchID.xy].z;

	float depth = DepthMask[dispatchID.xy];

#if defined(DLSS) || defined(XESS)
	// Find longest motion vector in 5x5 neighborhood
	float2 motionVector = MotionVectorMask[dispatchID.xy];
	float2 longestMotionVector = motionVector;
	float maxMotionLengthSq = dot(motionVector, motionVector);
#endif

	[unroll]
	for (int y = -2; y <= 2; y++) {
		[unroll]
		for (int x = -2; x <= 2; x++) {
			int2 samplePos = int2(dispatchID.xy) + int2(x, y);

			// Skip samples outside true sampling dimensions
			if (any(samplePos < 0) || any(samplePos >= int2(TrueSamplingDim)))
				continue;

			float neighborDepth = DepthMask[samplePos];

			// Take neighbor if it's longer AND closer
			if (neighborDepth < depth){
				taaMask.x = min(taaMask.x, TAAMask[samplePos].x);

#if defined(DLSS) || defined(XESS)

				float2 neighborMotionVector = MotionVectorMask[samplePos];

				// Square motion vector for length
				float motionLengthSq = dot(neighborMotionVector, neighborMotionVector);

				if (motionLengthSq > maxMotionLengthSq){
					maxMotionLengthSq = motionLengthSq;
					longestMotionVector = neighborMotionVector;
				}
#endif
			}
		}
	}

#if defined(DLSS) || defined(XESS)
	MotionVectorOutput[dispatchID.xy] = longestMotionVector;
#endif

#if defined(DLSS) || defined(FSR)
	float reactiveMask = taaMask.x * 0.1 + taaMask.y;
	ReactiveMask[dispatchID.xy] = reactiveMask;
	TransparencyCompositionMask[dispatchID.xy] = transparencyCompositionMask;
#else
	float reactiveMask = taaMask.x * 0.01 + taaMask.y;
	ReactiveMask[dispatchID.xy] = reactiveMask + transparencyCompositionMask * 0.1;
#endif
}