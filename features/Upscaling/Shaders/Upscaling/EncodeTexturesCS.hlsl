#include "Common/SharedData.hlsli"

cbuffer UpscalingData : register(b0)
{
	float2 TrueSamplingDim;
	float2 pad0;
};

Texture2D<float2> TAAMask : register(t0);
Texture2D<float4> NormalsWaterMask : register(t1);
Texture2D<float2> MotionVectorMask : register(t2);
Texture2D<float> DepthMask : register(t3);

#ifdef PATH_TRACING
Texture2D<float4> PTMotionVectors : register(t4);
Texture2D<float4> PTColor : register(t5);
Texture2D<float> PTDepth : register(t6);
#endif

RWTexture2D<float> ReactiveMask : register(u0);
RWTexture2D<float> TransparencyCompositionMask : register(u1);
RWTexture2D<float2> MotionVectorOutput : register(u2);
#if defined(DEPTH_OUTPUT)
RWTexture2D<float> DepthOutput : register(u3);
#endif

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	if (any(dispatchID.xy >= uint2(TrueSamplingDim)))
		return;

	float2 taaMask = TAAMask[dispatchID.xy];
	float transparencyCompositionMask = NormalsWaterMask[dispatchID.xy].z;

#if defined(DLSS) || defined(DLSS_RR)
#	ifdef PATH_TRACING
	float ptAlpha = PTColor[dispatchID.xy].a;
	float depth = ptAlpha > 0.5 ? PTDepth[dispatchID.xy] : DepthMask[dispatchID.xy];
	float2 motionVector = ptAlpha > 0.5 ? PTMotionVectors[dispatchID.xy].xy : MotionVectorMask[dispatchID.xy];
#	else
	const float depth = DepthMask[dispatchID.xy];
	const float2 motionVector = MotionVectorMask[dispatchID.xy];
#	endif
	float nearFactor = smoothstep(4096.0 * 2.5, 0.0, SharedData::GetScreenDepth(depth));
	float2 longestMotionVector = motionVector;
	float maxMotionLengthSq = dot(motionVector, motionVector);

	[unroll] for (int y = -2; y <= 2; y++)
	{
		[unroll] for (int x = -2; x <= 2; x++)
		{
			int2 samplePos = int2(dispatchID.xy) + int2(x, y);

			if (any(samplePos < 0) || any(samplePos >= int2(TrueSamplingDim)))
				continue;

			float neighborDepth = DepthMask[samplePos];

			if (neighborDepth < depth) {
				float2 neighborMotionVector = MotionVectorMask[samplePos];
				float motionLengthSq = dot(neighborMotionVector, neighborMotionVector);

				if (motionLengthSq > maxMotionLengthSq) {
					maxMotionLengthSq = motionLengthSq;
					longestMotionVector = neighborMotionVector;
				}
			}
		}
	}

	MotionVectorOutput[dispatchID.xy] = lerp(longestMotionVector, motionVector, nearFactor);
#elif defined(PATH_TRACING)
	float ptAlpha = PTColor[dispatchID.xy].a;
	float2 motionVector = ptAlpha > 0.5 ? PTMotionVectors[dispatchID.xy].xy : MotionVectorMask[dispatchID.xy];
	MotionVectorOutput[dispatchID.xy] = motionVector;
#endif

#if defined(DEPTH_OUTPUT)
	DepthOutput[dispatchID.xy] = DepthMask[dispatchID.xy];
#endif

	TransparencyCompositionMask[dispatchID.xy] = transparencyCompositionMask;

	float reactiveMask = taaMask.x * 0.01f + taaMask.y;
	ReactiveMask[dispatchID.xy] = reactiveMask;
}
