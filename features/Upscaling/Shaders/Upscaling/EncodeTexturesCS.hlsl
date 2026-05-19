#include "Common/SharedData.hlsli"

cbuffer UpscalingData : register(b0)
{
	float2 TrueSamplingDim;  // per-eye render dim in VR, full render dim otherwise
	uint EyeOffsetX;         // X offset into stereo source buffers; 0 for non-VR / left eye
	uint pad0;
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

	uint2 srcCoord = dispatchID.xy + uint2(EyeOffsetX, 0);

	float2 taaMask = TAAMask[srcCoord];
	float transparencyCompositionMask = NormalsWaterMask[srcCoord].z;

#if defined(DLSS) || defined(DLSS_RR)
#	ifdef PATH_TRACING
	float ptAlpha = PTColor[srcCoord].a;
	float depth = ptAlpha > 0.5 ? PTDepth[srcCoord] : DepthMask[srcCoord];
	float2 motionVector = ptAlpha > 0.5 ? PTMotionVectors[srcCoord].xy : MotionVectorMask[srcCoord];
#	else
	const float depth = DepthMask[srcCoord];
	const float2 motionVector = MotionVectorMask[srcCoord];
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

			int2 srcPos = samplePos + int2(EyeOffsetX, 0);
			float neighborDepth = DepthMask[srcPos];

			if (neighborDepth < depth) {
				float2 neighborMotionVector = MotionVectorMask[srcPos];
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
	float ptAlpha = PTColor[srcCoord].a;
	float2 motionVector = ptAlpha > 0.5 ? PTMotionVectors[srcCoord].xy : MotionVectorMask[srcCoord];
	MotionVectorOutput[dispatchID.xy] = motionVector;
#endif

#if defined(DEPTH_OUTPUT)
	DepthOutput[dispatchID.xy] = DepthMask[srcCoord];
#endif

	TransparencyCompositionMask[dispatchID.xy] = transparencyCompositionMask;

	float reactiveMask = taaMask.x * 0.01f + taaMask.y;
	ReactiveMask[dispatchID.xy] = reactiveMask;
}
