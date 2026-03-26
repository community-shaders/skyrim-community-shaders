#include "Common/SharedData.hlsli"

cbuffer UpscalingData : register(b0)
{
	float2 ResolutionScale;
	float DepthDisocclusion;
	float pad0;
};

Texture2D<float> DepthIn : register(t0);
RWTexture2D<float> DepthOut : register(u0);

#ifdef PATH_TRACING
Texture2D<float> PTDepth : register(t1);
Texture2D<float4> PTColor : register(t2);
#endif

[numthreads(8, 8, 1)] void main(uint2 id : SV_DispatchThreadID) {
	const uint2 trueSamplingDim = SharedData::BufferDim.xy * ResolutionScale;

	if (any(id >= trueSamplingDim))
		return;

	float depth = DepthIn[id.xy];

#ifdef PATH_TRACING
	if (PTColor[id.xy].a > 0.5)
		depth = PTDepth[id.xy];
#endif

	DepthOut[id.xy] = depth;
}