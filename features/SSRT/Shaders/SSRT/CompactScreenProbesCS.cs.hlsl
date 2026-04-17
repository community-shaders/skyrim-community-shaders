// Compact spawned probes using prefix sum results
// Reads uncompacted seeds from PrevProbeSpawn (written by SpawnScreenProbes),
// writes compacted seeds to ProbeSpawn, and identifies override tiles

#include "SSRT/ScreenProbes.hlsli"

Texture2D<uint> g_ProbeMask : register(t0);
StructuredBuffer<uint> g_PrevProbeSpawn : register(t1);  // uncompacted seeds (from Spawn pass)

RWStructuredBuffer<uint> g_ProbeSpawn : register(u0);        // output: compacted seeds
RWStructuredBuffer<uint> g_ProbeSpawnScan : register(u1);    // input: 0/1 flags
RWStructuredBuffer<uint> g_ProbeSpawnIndex : register(u2);   // input: exclusive prefix sums
RWStructuredBuffer<uint> g_OverrideTile : register(u3);      // output: override tile list
RWStructuredBuffer<uint> g_OverrideTileCount : register(u4); // output: override count

[numthreads(64, 1, 1)] void main(uint did
								  : SV_DispatchThreadID)
{
	if (did >= g_MaxSpawnCount)
		return;

	if (g_ProbeSpawnScan[did] == 0)
		return;  // probe was culled (sky pixel)

	uint probe_index = g_ProbeSpawnIndex[did];
	uint probe_seed = g_PrevProbeSpawn[did];  // read from uncompacted buffer
	uint2 seed = ScreenProbes_UnpackSeed(probe_seed);
	uint probe_mask = g_ProbeMask.Load(int3(seed / g_ProbeSize, 0));

	// If this tile already has a valid probe from reprojection, it's an override candidate
	if (probe_mask != kGI1_InvalidId)
	{
		uint override_tile_index;
		InterlockedAdd(g_OverrideTileCount[0], 1, override_tile_index);
		g_OverrideTile[override_tile_index] = probe_index;
	}

	// Write compacted seed to prefix-summed index
	g_ProbeSpawn[probe_index] = probe_seed;
}
