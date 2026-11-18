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

RWTexture2D<float> ReactiveMask : register(u0);
RWTexture2D<float> TransparencyCompositionMask : register(u1);
RWTexture2D<float2> MotionVectorOutput : register(u2);
#if defined(DEPTH_OUTPUT)
RWTexture2D<float> DepthOutput : register(u3);
#endif

#if defined(DLSS)
#	define DILATE_TILE 12
groupshared float TileDepth[DILATE_TILE * DILATE_TILE];
groupshared float2 TileMotion[DILATE_TILE * DILATE_TILE];
#endif

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID, uint groupIndex : SV_GroupIndex) {
#if defined(DLSS)
	const int2 tileOrigin = int2(groupID.xy) * 8 - 2;
	[unroll] for (uint t = groupIndex; t < DILATE_TILE * DILATE_TILE; t += 64)
	{
		int2 tilePos = tileOrigin + int2(t % DILATE_TILE, t / DILATE_TILE);
		TileDepth[t] = DepthMask[tilePos];
		TileMotion[t] = MotionVectorMask[tilePos];
	}
	GroupMemoryBarrierWithGroupSync();
#endif

	// Bounds check
	if (any(dispatchID.xy >= uint2(TrueSamplingDim)))
		return;

	float2 taaMask = TAAMask[dispatchID.xy];
	float transparencyCompositionMask = NormalsWaterMask[dispatchID.xy].z;

#if defined(DLSS)
	const uint centerIndex = (groupThreadID.y + 2) * DILATE_TILE + groupThreadID.x + 2;
	float depth = TileDepth[centerIndex];
	float nearFactor = smoothstep(4096.0 * 2.5, 0.0, SharedData::GetScreenDepth(depth));

	// Find longest motion vector in 5x5 neighborhood
	float2 motionVector = TileMotion[centerIndex];
	float2 longestMotionVector = motionVector;
	float maxMotionLengthSq = dot(motionVector, motionVector);

	[unroll] for (int y = -2; y <= 2; y++)
	{
		[unroll] for (int x = -2; x <= 2; x++)
		{
			int2 samplePos = int2(dispatchID.xy) + int2(x, y);

			// Bounds check
			if (any(samplePos < 0) || any(samplePos >= int2(TrueSamplingDim)))
				continue;

			const uint neighborIndex = centerIndex + y * DILATE_TILE + x;
			float neighborDepth = TileDepth[neighborIndex];

			// Take neighbor if it's longer AND closer
			if (neighborDepth < depth) {
				float2 neighborMotionVector = TileMotion[neighborIndex];

				// Square motion vector for length
				float motionLengthSq = dot(neighborMotionVector, neighborMotionVector);

				if (motionLengthSq > maxMotionLengthSq) {
					maxMotionLengthSq = motionLengthSq;
					longestMotionVector = neighborMotionVector;
				}
			}
		}
	}

	MotionVectorOutput[dispatchID.xy] = lerp(longestMotionVector, motionVector, nearFactor);
#endif

#if defined(DEPTH_OUTPUT)
	// Copy depth as R32_FLOAT so FSR DX11 backend receives a typed format.
	// The raw depth resource is R24G8_TYPELESS which maps to FFX_SURFACE_FORMAT_UNKNOWN.
	DepthOutput[dispatchID.xy] = DepthMask[dispatchID.xy];
#endif

	float reactiveMask = taaMask.x * 0.1 + taaMask.y;
	ReactiveMask[dispatchID.xy] = reactiveMask;

	TransparencyCompositionMask[dispatchID.xy] = transparencyCompositionMask;
}
