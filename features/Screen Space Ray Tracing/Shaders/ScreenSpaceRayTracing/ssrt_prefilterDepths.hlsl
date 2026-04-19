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

groupshared float g_scratchDepths[8][8];

[numthreads(8, 8, 1)] void main(uint2 DTid : SV_DispatchThreadID, uint2 GTid : SV_GroupThreadID)
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

	// MIP 1
	float dm1 = DepthMIPFilter(depth0, depth1, depth2, depth3);
	g_scratchDepths[GTid.x][GTid.y] = dm1;
	halfResDepth[baseCoord] = dm1;

	GroupMemoryBarrierWithGroupSync();

	// MIP 2
	[branch] if (all((GTid.xy % 2) == 0))
	{
		float inTL = g_scratchDepths[GTid.x + 0][GTid.y + 0];
		float inTR = g_scratchDepths[GTid.x + 1][GTid.y + 0];
		float inBL = g_scratchDepths[GTid.x + 0][GTid.y + 1];
		float inBR = g_scratchDepths[GTid.x + 1][GTid.y + 1];

		float dm2 = DepthMIPFilter(inTL, inTR, inBL, inBR);
		outDepth0[baseCoord / 2] = dm2;
	}
}
