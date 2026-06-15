#include "Common/Color.hlsli"
#include "Common/SharedData.hlsli"

cbuffer UpscalingData : register(b0)
{
	float2 TrueSamplingDim;
	float2 pad0;
};

RWTexture2D<float4> Main : register(u0);

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	if (any(dispatchID.xy >= uint2(TrueSamplingDim)))
		return;

	float4 main = Main[dispatchID.xy];

#if defined(TO_LINEAR)
	main.xyz = Color::SrgbToLinear(main.xyz);
#elif defined(TO_GAMMA)
	main.xyz = Color::LinearToSrgb(main.xyz);
#endif

	Main[dispatchID.xy] = main;
}