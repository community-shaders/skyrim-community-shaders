#include "Common/SharedData.hlsli"

Texture2D<float2> TAAMask : register(t0);
Texture2D<float4> NormalsWaterMask : register(t1);
Texture2D<float2> MotionVectorMask : register(t2);
Texture2D<float2> DepthMask : register(t3);

RWTexture2D<float> ReactiveMask : register(u0);

#if defined(DLSS) || defined(FSR)
RWTexture2D<float> TransparencyCompositionMask : register(u1);
#endif

RWTexture2D<float2> MotionVectorOutput : register(u2);

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	float2 taaMask = TAAMask[dispatchID.xy];

	float reactiveMask = taaMask.x * 0.1 + taaMask.y;
	float transparencyCompositionMask = NormalsWaterMask[dispatchID.xy].z;

#if defined(DLSS)
	ReactiveMask[dispatchID.xy] = reactiveMask;
	TransparencyCompositionMask[dispatchID.xy] = transparencyCompositionMask;
#elif defined(FSR)
	ReactiveMask[dispatchID.xy] = reactiveMask;
	TransparencyCompositionMask[dispatchID.xy] = transparencyCompositionMask;
#else
	ReactiveMask[dispatchID.xy] = reactiveMask + transparencyCompositionMask * 0.1;
#endif

#if defined(DLSS) || defined(XESS)
	float depth = DepthMask[dispatchID.xy];
	float2 motionVector = MotionVectorMask[dispatchID.xy];

	// Find longest motion vector in 3x3 neighborhood
	float2 longestMotionVector = motionVector;
	float maxMotionLengthSq = dot(motionVector, motionVector);

	[unroll]
	for (int y = -1; y <= 1; y++) {
		[unroll]
		for (int x = -1; x <= 1; x++) {
			int2 samplePos = int2(dispatchID.xy) + int2(x, y);

			// Load neighbor motion vector and depth
			float2 neighborMotionVector = MotionVectorMask[samplePos];
			float neighborDepth = DepthMask[samplePos];

			// Square motion vector for length
			float motionLengthSq = dot(neighborMotionVector, neighborMotionVector);

			// Take neighbor if it's longer AND closer
			bool takeNeighbor = (motionLengthSq > maxMotionLengthSq) && (neighborDepth < depth);
			maxMotionLengthSq = takeNeighbor ? motionLengthSq : maxMotionLengthSq;
			longestMotionVector = takeNeighbor ? neighborMotionVector : longestMotionVector;
		}
	}

	MotionVectorOutput[dispatchID.xy] = longestMotionVector;
#endif
}