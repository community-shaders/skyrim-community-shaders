#include "Common/Game.hlsli"
#include "Common/SharedData.hlsli"

cbuffer FocusParameters : register(b0)
{
	float2 FocusUV;
	float ManualKM;
	float Speed;
	uint AutoFocus;
	uint ResetFocus;
	float2 Padding;
};
Texture2D<float> Depth : register(t0);
Texture2D<float> Previous : register(t1);
SamplerState LinearSampler : register(s0);
RWTexture2D<float> Focus : register(u0);
[numthreads(1, 1, 1)] void UpdateFocus(uint3 id : SV_DispatchThreadID) {
	float depth = ManualKM;
	if (AutoFocus)
		depth = max(SharedData::GetScreenDepth(Depth.SampleLevel(LinearSampler, FocusUV, 0)) * GAME_UNIT_TO_M * .001, 1e-6);
	Focus[uint2(0, 0)] = ResetFocus ? depth : lerp(Previous[uint2(0, 0)], depth, Speed);
}
