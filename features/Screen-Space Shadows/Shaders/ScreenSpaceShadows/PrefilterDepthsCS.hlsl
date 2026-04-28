// Prefilter game NDC depth into 4 mip levels using group-shared memory.
// Adapted from XeGTAO prefilterDepths (Intel, MIT License).
// Each thread handles a 2x2 full-res block; one pass produces mip 0-3.
// Output is float2(linearDepth, linearDepth²) so downstream VSM queries can
// use per-sample moments directly.  Sky / invalid pixels write float2(0,0).

#include "Common/SharedData.hlsli"

Texture2D<float> srcNDCDepth : register(t0);

RWTexture2D<float2> outDepth0 : register(u0);  // full-res
RWTexture2D<float2> outDepth1 : register(u1);  // half-res
RWTexture2D<float2> outDepth2 : register(u2);  // quarter-res
RWTexture2D<float2> outDepth3 : register(u3);  // eighth-res

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
};

SamplerState samplerPointClamp : register(s1);

// Convert NDC depth to float2(linearZ, linearZ²).  Returns (0,0) for sky/invalid.
float2 ToMoment(float ndcZ)
{
	if (ndcZ <= 0.0 || ndcZ >= 1.0)
		return float2(0, 0);
	float z = SharedData::GetScreenDepth(ndcZ);
	return float2(z, z * z);
}

// Average moment pairs, skipping sentinel (0,0) entries.
float2 DepthMIPFilter(float2 d0, float2 d1, float2 d2, float2 d3)
{
	float2 sum = 0;
	float count = 0;
	if (d0.x > 0.0) { sum += d0; count += 1.0; }
	if (d1.x > 0.0) { sum += d1; count += 1.0; }
	if (d2.x > 0.0) { sum += d2; count += 1.0; }
	if (d3.x > 0.0) { sum += d3; count += 1.0; }
	return count > 0.0 ? sum / count : float2(0, 0);
}

groupshared float2 g_scratchDepths[8][8];

[numthreads(8, 8, 1)] void main(uint2 dispatchThreadID
								: SV_DispatchThreadID, uint2 groupThreadID
								: SV_GroupThreadID) {
	const uint2 baseCoord = dispatchThreadID;
	const uint2 pixCoord = baseCoord * 2;

	// Gather the 2x2 full-res block starting at pixCoord.
	float4 depths4 = srcNDCDepth.GatherRed(samplerPointClamp, (float2(pixCoord) + 0.5) * RcpTexDim);
	float2 m0 = ToMoment(depths4.w);  // TL
	float2 m1 = ToMoment(depths4.z);  // TR
	float2 m2 = ToMoment(depths4.x);  // BL
	float2 m3 = ToMoment(depths4.y);  // BR

	// MIP 0 — full-res moments.
	outDepth0[pixCoord + uint2(0, 0)] = m0;
	outDepth0[pixCoord + uint2(1, 0)] = m1;
	outDepth0[pixCoord + uint2(0, 1)] = m2;
	outDepth0[pixCoord + uint2(1, 1)] = m3;

	// MIP 1 — half-res averaged moments.
	float2 dm1 = DepthMIPFilter(m0, m1, m2, m3);
	outDepth1[baseCoord] = dm1;
	g_scratchDepths[groupThreadID.x][groupThreadID.y] = dm1;

	GroupMemoryBarrierWithGroupSync();

	// MIP 2 — quarter-res.
	[branch] if (all((groupThreadID.xy % 2) == 0))
	{
		float2 inTL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 0];
		float2 inTR = g_scratchDepths[groupThreadID.x + 1][groupThreadID.y + 0];
		float2 inBL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 1];
		float2 inBR = g_scratchDepths[groupThreadID.x + 1][groupThreadID.y + 1];
		float2 dm2 = DepthMIPFilter(inTL, inTR, inBL, inBR);
		outDepth2[baseCoord / 2] = dm2;
		g_scratchDepths[groupThreadID.x][groupThreadID.y] = dm2;
	}

	GroupMemoryBarrierWithGroupSync();

	// MIP 3 — eighth-res.
	[branch] if (all((groupThreadID.xy % 4) == 0))
	{
		float2 inTL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 0];
		float2 inTR = g_scratchDepths[groupThreadID.x + 2][groupThreadID.y + 0];
		float2 inBL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 2];
		float2 inBR = g_scratchDepths[groupThreadID.x + 2][groupThreadID.y + 2];
		outDepth3[baseCoord / 4] = DepthMIPFilter(inTL, inTR, inBL, inBR);
	}
}
