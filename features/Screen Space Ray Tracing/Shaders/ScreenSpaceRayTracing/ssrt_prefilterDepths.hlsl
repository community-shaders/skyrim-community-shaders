#include "ScreenSpaceRayTracing/ssrt_common.hlsli"

Texture2D<float> srcNDCDepth : register(t0);
RWTexture2D<float> outDepth0 : register(u0);
RWTexture2D<float> halfResDepth : register(u1);

SamplerState samplerPointClamp : register(s0);

cbuffer SSRTCB : register(b1)
{
    uint MaxSteps;
    uint MaxMips;
    uint UseDynamicCubemapsAsFallback;
    float Thickness;
    float NormalBias;
    float BRDFBias;
    float OcclusionStrength;
    float CubemapNormalization;

    float2 TexDim;
    float2 RcpTexDim;
    float2 FrameDim;
    float2 RcpFrameDim;
};

float DepthMIPFilter(float depth0, float depth1, float depth2, float depth3)
{
	return min(min(depth0, depth1), min(depth2, depth3));
}

[numthreads(8, 8, 1)] void main(uint2 DTid : SV_DispatchThreadID)
{
	const float2 frameScale = FrameDim * RcpTexDim;

	// MIP 0
	const uint2 baseCoord = DTid;
	const uint2 pixCoord = baseCoord * 2;
	const float2 uv = (pixCoord + .5) * RcpFrameDim;

	float4 depths4 = srcNDCDepth.GatherRed(samplerPointClamp, uv * frameScale);
	float depth0 = depths4.w;
	float depth1 = depths4.z;
	float depth2 = depths4.x;
	float depth3 = depths4.y;

	// MIP 1 (half res) -> Hi-Z base and bilateral guide
	float dm1 = DepthMIPFilter(depth0, depth1, depth2, depth3);
	outDepth0[baseCoord] = dm1;
	halfResDepth[baseCoord] = dm1;
}
