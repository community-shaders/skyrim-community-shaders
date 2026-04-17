// Patch disoccluded regions by stealing probes from override tiles
// Dispatched indirectly based on empty tile count

#include "SSRT/ScreenProbes.hlsli"
#include "SSRT/Halton.hlsli"
#include "SSRT/Random.hlsli"

Texture2D<float4> g_NormalsTexture : register(t0);

RWStructuredBuffer<uint> g_ProbeSpawn : register(u0);        // probe spawn buffer (InterlockedExchange)
RWStructuredBuffer<uint> g_EmptyTile : register(u1);         // empty tile indices
RWStructuredBuffer<uint> g_OverrideTile : register(u2);      // override tile indices
RWStructuredBuffer<uint> g_OverrideTileCount : register(u3); // override tile count
RWStructuredBuffer<uint> g_EmptyTileCount : register(u4);    // empty tile count

[numthreads(64, 1, 1)] void main(uint did
								  : SV_DispatchThreadID)
{
	uint override_tile_count = g_OverrideTileCount[0];

	if (override_tile_count == 0 || did >= g_EmptyTileCount[0])
		return;

	uint probe_count = (uint(g_BufferDimensions.x) + g_ProbeSize - 1) / g_ProbeSize;
	uint probe_index = g_EmptyTile[did];

	uint2 probe = uint2(probe_index % probe_count, probe_index / probe_count);
	uint2 jitter = min(CalculateHaltonSequence(g_FrameIndex) * g_ProbeSize, g_ProbeSize - 1.0f);

	uint2  seed         = min(probe * g_ProbeSize + jitter, uint2(g_BufferDimensions) - 1);
	float3 normal       = g_NormalsTexture.Load(int3(seed, 0)).xyz;
	bool   is_sky_pixel = (dot(normal, normal) == 0.0f);

	if (is_sky_pixel)
		return;

	RandomState random = MakeRandom(did, g_FrameIndex);
	uint index = random.randInt(override_tile_count);

	uint dummy;
	InterlockedExchange(g_ProbeSpawn[g_OverrideTile[index]], ScreenProbes_PackSeed(seed), dummy);
}
