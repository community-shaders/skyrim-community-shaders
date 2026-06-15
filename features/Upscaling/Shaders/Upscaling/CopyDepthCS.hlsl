#include "Common/SharedData.hlsli"

cbuffer UpscalingData : register(b0)
{
	float2 TrueSamplingDim;
	float2 pad0;
};

Texture2D<float> DepthIn : register(t0);
RWTexture2D<float> DepthOut : register(u0);

#ifdef PATH_TRACING
Texture2D<float> PTDepth : register(t1);
Texture2D<float4> PTColor : register(t2);
#endif

[numthreads(8, 8, 1)] void main(uint2 id : SV_DispatchThreadID) {
	if (any(id >= uint2(TrueSamplingDim)))
		return;

	float depth = DepthIn[id];

#ifdef PATH_TRACING
	if (PTColor[id].a > 0.5)
		depth = PTDepth[id];
#endif

	DepthOut[id.xy] = depth;
}
