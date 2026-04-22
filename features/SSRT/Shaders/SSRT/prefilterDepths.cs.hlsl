#include "SSRT/common.hlsli"

Texture2D<float> srcNDCDepth : register(t0);

RWTexture2D<float> outDepth0 : register(u0);
RWTexture2D<float> outDepth1 : register(u1);
RWTexture2D<float> outDepth2 : register(u2);
RWTexture2D<float> outDepth3 : register(u3);
RWTexture2D<float> outDepth4 : register(u4);

float ClampDepth(float deviceDepth)
{
	if (deviceDepth == 1.0)
		return 0.0;
	return max(ScreenToLinearDepth(deviceDepth), 0.0);
}

float DepthMIPFilter(float depth0, float depth1, float depth2, float depth3)
{
	return (depth0 + depth1 + depth2 + depth3) * 0.25;
}

groupshared float g_scratchDepths[8][8];
[numthreads(8, 8, 1)] void main(uint2 dispatchThreadID : SV_DispatchThreadID, uint2 groupThreadID : SV_GroupThreadID) {
	const float2 frameScale = FrameDim * RcpTexDim;

	// MIP 0
	const uint2 baseCoord = dispatchThreadID;
	const uint2 pixCoord = baseCoord * 2;
	const float2 uv = (pixCoord + .5) * RcpFrameDim;

	float4 depths4 = srcNDCDepth.GatherRed(samplerPointClamp, uv * frameScale);
	float depth0 = ClampDepth(depths4.w);
	float depth1 = ClampDepth(depths4.z);
	float depth2 = ClampDepth(depths4.x);
	float depth3 = ClampDepth(depths4.y);
	outDepth0[pixCoord + uint2(0, 0)] = depth0;
	outDepth0[pixCoord + uint2(1, 0)] = depth1;
	outDepth0[pixCoord + uint2(0, 1)] = depth2;
	outDepth0[pixCoord + uint2(1, 1)] = depth3;

	// MIP 1
	float dm1 = DepthMIPFilter(depth0, depth1, depth2, depth3);
	outDepth1[baseCoord] = dm1;
	g_scratchDepths[groupThreadID.x][groupThreadID.y] = dm1;

	GroupMemoryBarrierWithGroupSync();

	// MIP 2
	[branch] if (all((groupThreadID.xy % 2) == 0))
	{
		float inTL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 0];
		float inTR = g_scratchDepths[groupThreadID.x + 1][groupThreadID.y + 0];
		float inBL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 1];
		float inBR = g_scratchDepths[groupThreadID.x + 1][groupThreadID.y + 1];

		float dm2 = DepthMIPFilter(inTL, inTR, inBL, inBR);
		outDepth2[baseCoord / 2] = dm2;
		g_scratchDepths[groupThreadID.x][groupThreadID.y] = dm2;
	}

	GroupMemoryBarrierWithGroupSync();

	// MIP 3
	[branch] if (all((groupThreadID.xy % 4) == 0))
	{
		float inTL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 0];
		float inTR = g_scratchDepths[groupThreadID.x + 2][groupThreadID.y + 0];
		float inBL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 2];
		float inBR = g_scratchDepths[groupThreadID.x + 2][groupThreadID.y + 2];

		float dm3 = DepthMIPFilter(inTL, inTR, inBL, inBR);
		outDepth3[baseCoord / 4] = dm3;
		g_scratchDepths[groupThreadID.x][groupThreadID.y] = dm3;
	}

	GroupMemoryBarrierWithGroupSync();

	// MIP 4
	[branch] if (all((groupThreadID.xy % 8) == 0))
	{
		float inTL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 0];
		float inTR = g_scratchDepths[groupThreadID.x + 4][groupThreadID.y + 0];
		float inBL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 4];
		float inBR = g_scratchDepths[groupThreadID.x + 4][groupThreadID.y + 4];

		float dm4 = DepthMIPFilter(inTL, inTR, inBL, inBR);
		outDepth4[baseCoord / 8] = dm4;
	}
}
