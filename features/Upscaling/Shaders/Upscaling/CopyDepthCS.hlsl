#include "Common/SharedData.hlsli"

cbuffer UpscalingData : register(b0)
{
	float2 ResolutionScale;
	float DepthDisocclusion;
	float pad0;
};

Texture2D<float> DepthIn : register(t0);
RWTexture2D<float> DepthOut : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 id : SV_DispatchThreadID)
{
	const uint2 trueSamplingDim = SharedData::BufferDim.xy * ResolutionScale;
    
    if (any(id >= trueSamplingDim))
        return;

    DepthOut[id.xy] = DepthIn[id.xy];
}