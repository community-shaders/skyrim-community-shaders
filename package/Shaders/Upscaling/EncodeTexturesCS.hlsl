#include "Common/SharedData.hlsli"

Texture2D<float2> TAAMask : register(t0);

Texture2D<float> DepthPreWater : register(t1);
Texture2D<float> DepthPostWater : register(t2);

RWTexture2D<float> ReactiveMask : register(u0);
RWTexture2D<float> TransparencyCompositionMask : register(u1);

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	float2 taaMask = TAAMask[dispatchID.xy];

	float reactiveMask = taaMask.x * 0.25;
	reactiveMask += sqrt(taaMask.y);

	ReactiveMask[dispatchID.xy] = reactiveMask;

	float depthPreWater = SharedData::GetScreenDepth(DepthPreWater[dispatchID.xy]);
	float depthPostWater = SharedData::GetScreenDepth(DepthPostWater[dispatchID.xy]);

	float depthDifference = saturate(abs(depthPreWater - depthPostWater) * 0.01);

	float transparencyCompositionMask = depthDifference;

	TransparencyCompositionMask[dispatchID.xy] = transparencyCompositionMask;
}
