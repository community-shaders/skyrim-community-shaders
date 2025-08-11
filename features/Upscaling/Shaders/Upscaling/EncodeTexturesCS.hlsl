#include "Common/SharedData.hlsli"

Texture2D<float2> TAAMask : register(t0);

Texture2D<float> DepthPreWater : register(t1);
Texture2D<float> DepthPostWater : register(t2);

RWTexture2D<float> ReactiveMask : register(u0);

#if defined(TRANSPARENCY_MASK)
RWTexture2D<float> TransparencyCompositionMask : register(u1);
#endif

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	float2 taaMask = TAAMask[dispatchID.xy];

#if defined(TRANSPARENCY_MASK)
	float reactiveMask = taaMask.x + taaMask.y;
#else
	float reactiveMask = taaMask.x * 0.1 + taaMask.y;
#endif

	ReactiveMask[dispatchID.xy] = reactiveMask;

#if defined(TRANSPARENCY_MASK)
	float depthPreWater = SharedData::GetScreenDepth(DepthPreWater[dispatchID.xy]);
	float depthPostWater = SharedData::GetScreenDepth(DepthPostWater[dispatchID.xy]);

	float depthDifference = abs(depthPreWater - depthPostWater);

	float transparencyCompositionMask = depthDifference;

	TransparencyCompositionMask[dispatchID.xy] = transparencyCompositionMask;
#endif
}
