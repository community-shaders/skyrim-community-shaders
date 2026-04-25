#include "ScreenSpaceRayTracing/ssrt_common.hlsli"

Texture2D<float> srcNDCDepth : register(t0);
RWTexture2D<float> outDepth0 : register(u0);

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
	float _pad0;

	float2 TexDim;
	float2 RcpTexDim;
	float2 FrameDim;
	float2 RcpFrameDim;
	float HitDistA;
	float HitDistB;
	float HitDistC;
	float HitDistD;
	uint FrameIndex;
};

[numthreads(8, 8, 1)] void main(uint2 DTid : SV_DispatchThreadID) {
	const float2 frameScale = FrameDim * RcpTexDim;
	const uint2 baseCoord = DTid;
	const float2 uv = (baseCoord + 0.5) * RcpFrameDim;

	outDepth0[baseCoord] = srcNDCDepth.SampleLevel(samplerPointClamp, uv * frameScale, 0);
}
