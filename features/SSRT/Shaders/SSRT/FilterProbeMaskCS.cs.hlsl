// Build mip pyramid of probe occupancy
// Reads from previous mip level (u1), writes to current mip level (u0)
// Propagates valid probe references to coarser mips for FindClosestProbe

#include "SSRT/GI1Common.hlsli"

RWTexture2D<uint> g_OutProbeMask : register(u0);  // output mip
RWTexture2D<uint> g_InProbeMask : register(u1);   // input mip (previous level)

[numthreads(8, 8, 1)] void main(uint2 did
								 : SV_DispatchThreadID)
{
	uint inW, inH;
	g_InProbeMask.GetDimensions(inW, inH);

	if (any((did << 1) >= uint2(inW, inH)))
		return;

	uint probe_mask = kGI1_InvalidId;

	// Find first valid probe in the 2x2 region of the higher-resolution mip
	[unroll]
	for (uint y = 0; y < 2; ++y)
	{
		[unroll]
		for (uint x = 0; x < 2; ++x)
		{
			uint2 pos = (did << 1) + uint2(x, y);

			probe_mask = (all(pos < uint2(inW, inH)) ? g_InProbeMask[pos] : kGI1_InvalidId);

			if (probe_mask != kGI1_InvalidId)
				break;
		}

		if (probe_mask != kGI1_InvalidId)
			break;
	}

	g_OutProbeMask[did] = probe_mask;
}
