// Prefilter game NDC depth into 4 mip levels using group-shared memory.
// Adapted from XeGTAO prefilterDepths (Intel, MIT License).
// Each thread handles a 2x2 full-res block; one pass produces mip 0-3.

Texture2D<float> srcNDCDepth : register(t0);

RWTexture2D<float> outDepth0 : register(u0);  // full-res
RWTexture2D<float> outDepth1 : register(u1);  // half-res
RWTexture2D<float> outDepth2 : register(u2);  // quarter-res
RWTexture2D<float> outDepth3 : register(u3);  // eighth-res

cbuffer SSSCB : register(b1)
{
	float2 FrameDim;
	float2 RcpTexDim;

	float2 TexDim;
	float2 DynamicRes;

	float SurfaceThickness;
	float ShadowContrast;
	float RayLength;
	uint CurrentMip;

	float3 LightWorldDir;
	uint pad;
};

SamplerState samplerPointClamp : register(s1);

float DepthMIPFilter(float d0, float d1, float d2, float d3)
{
	return min(d0, min(d1, min(d2, d3)));
}

groupshared float g_scratchDepths[8][8];

[numthreads(8, 8, 1)] void main(uint2 dispatchThreadID
								: SV_DispatchThreadID, uint2 groupThreadID
								: SV_GroupThreadID) {
	const uint2 baseCoord = dispatchThreadID;
	const uint2 pixCoord = baseCoord * 2;

	// Gather the 2x2 full-res block starting at pixCoord.
	// GatherRed at UV (pixCoord + 0.5) * RcpTexDim samples texels at
	// (pixCoord.x, pixCoord.y), (+1,0), (0,+1), (+1,+1).
	float4 depths4 = srcNDCDepth.GatherRed(samplerPointClamp, (float2(pixCoord) + 0.5) * RcpTexDim);
	float depth0 = depths4.w;  // TL
	float depth1 = depths4.z;  // TR
	float depth2 = depths4.x;  // BL
	float depth3 = depths4.y;  // BR

	// MIP 0 — full-res copy.
	outDepth0[pixCoord + uint2(0, 0)] = depth0;
	outDepth0[pixCoord + uint2(1, 0)] = depth1;
	outDepth0[pixCoord + uint2(0, 1)] = depth2;
	outDepth0[pixCoord + uint2(1, 1)] = depth3;

	// MIP 1 — half-res average.
	float dm1 = DepthMIPFilter(depth0, depth1, depth2, depth3);
	outDepth1[baseCoord] = dm1;
	g_scratchDepths[groupThreadID.x][groupThreadID.y] = dm1;

	GroupMemoryBarrierWithGroupSync();

	// MIP 2 — quarter-res.
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

	// MIP 3 — eighth-res.
	[branch] if (all((groupThreadID.xy % 4) == 0))
	{
		float inTL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 0];
		float inTR = g_scratchDepths[groupThreadID.x + 2][groupThreadID.y + 0];
		float inBL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 2];
		float inBR = g_scratchDepths[groupThreadID.x + 2][groupThreadID.y + 2];
		outDepth3[baseCoord / 4] = DepthMIPFilter(inTL, inTR, inBL, inBR);
	}
}
