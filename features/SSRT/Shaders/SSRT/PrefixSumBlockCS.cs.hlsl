// Blelloch prefix sum — Pass 2: scan block totals
// Single group scans all block totals (supports up to 128 blocks = 16384 elements)

#include "SSRT/GI1Common.hlsli"

RWStructuredBuffer<uint> g_BlockTotals : register(u0);

groupshared uint lds_Data[128];

[numthreads(64, 1, 1)] void main(uint local_id
								  : SV_GroupThreadID)
{
	uint idx0 = local_id * 2;
	uint idx1 = local_id * 2 + 1;

	lds_Data[idx0] = g_BlockTotals[idx0];
	lds_Data[idx1] = g_BlockTotals[idx1];
	GroupMemoryBarrierWithGroupSync();

	// Up-sweep
	[unroll]
	for (uint stride = 1; stride < 128; stride <<= 1)
	{
		uint index = (local_id + 1) * stride * 2 - 1;
		if (index < 128)
			lds_Data[index] += lds_Data[index - stride];
		GroupMemoryBarrierWithGroupSync();
	}

	if (local_id == 0)
		lds_Data[127] = 0;
	GroupMemoryBarrierWithGroupSync();

	// Down-sweep
	[unroll]
	for (uint s = 64; s > 0; s >>= 1)
	{
		uint index = (local_id + 1) * s * 2 - 1;
		if (index < 128)
		{
			uint tmp = lds_Data[index - s];
			lds_Data[index - s] = lds_Data[index];
			lds_Data[index] += tmp;
		}
		GroupMemoryBarrierWithGroupSync();
	}

	g_BlockTotals[idx0] = lds_Data[idx0];
	g_BlockTotals[idx1] = lds_Data[idx1];
}
