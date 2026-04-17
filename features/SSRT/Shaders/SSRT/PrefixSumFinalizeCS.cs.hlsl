// Blelloch prefix sum — Pass 3: add block offsets to local prefix sums in IndexBuffer

#include "SSRT/GI1Common.hlsli"

RWStructuredBuffer<uint> g_IndexBuffer : register(u0);   // in/out: local sums -> global exclusive prefix sums
RWStructuredBuffer<uint> g_BlockTotals : register(u1);   // input: scanned block totals

[numthreads(64, 1, 1)] void main(uint local_id
								  : SV_GroupThreadID, uint group_id
								  : SV_GroupID)
{
	uint blockOffset = group_id * 128;
	uint blockTotal = g_BlockTotals[group_id];

	uint idx0 = local_id * 2;
	uint idx1 = local_id * 2 + 1;

	g_IndexBuffer[blockOffset + idx0] += blockTotal;
	g_IndexBuffer[blockOffset + idx1] += blockTotal;
}
