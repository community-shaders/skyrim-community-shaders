// Blelloch prefix sum — Pass 1: local exclusive scan per 128-element block
// Reads original flags from ScanBuffer, writes exclusive prefix sums to IndexBuffer
// ScanBuffer is NOT modified (retains 0/1 flags for later use)

#include "SSRT/GI1Common.hlsli"

RWStructuredBuffer<uint> g_ScanBuffer : register(u0);    // input: 0/1 flags (not modified)
RWStructuredBuffer<uint> g_BlockTotals : register(u1);   // output: sum per block
RWStructuredBuffer<uint> g_IndexBuffer : register(u2);   // output: local exclusive prefix sum

groupshared uint lds_Data[128];

[numthreads(64, 1, 1)] void main(uint local_id
								  : SV_GroupThreadID, uint group_id
								  : SV_GroupID)
{
	uint blockOffset = group_id * 128;
	uint idx0 = local_id * 2;
	uint idx1 = local_id * 2 + 1;

	// Load from ScanBuffer (0/1 flags)
	lds_Data[idx0] = g_ScanBuffer[blockOffset + idx0];
	lds_Data[idx1] = g_ScanBuffer[blockOffset + idx1];
	GroupMemoryBarrierWithGroupSync();

	// Up-sweep (reduce)
	[unroll]
	for (uint stride = 1; stride < 128; stride <<= 1)
	{
		uint index = (local_id + 1) * stride * 2 - 1;
		if (index < 128)
			lds_Data[index] += lds_Data[index - stride];
		GroupMemoryBarrierWithGroupSync();
	}

	// Save block total and clear for down-sweep
	if (local_id == 0)
	{
		g_BlockTotals[group_id] = lds_Data[127];
		lds_Data[127] = 0;
	}
	GroupMemoryBarrierWithGroupSync();

	// Down-sweep (exclusive scan)
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

	// Write local exclusive prefix sums to IndexBuffer
	g_IndexBuffer[blockOffset + idx0] = lds_Data[idx0];
	g_IndexBuffer[blockOffset + idx1] = lds_Data[idx1];
}
