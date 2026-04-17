// Spawn screen probes at Halton-jittered positions within spawn tiles
// Writes probe seeds and scan flags for prefix sum compaction

#include "SSRT/ScreenProbes.hlsli"
#include "SSRT/Halton.hlsli"

Texture2D<float4> g_NormalsTexture : register(t0);

RWStructuredBuffer<uint> g_ProbeSpawnScan : register(u0);
RWStructuredBuffer<uint> g_PrevProbeSpawn : register(u1);

[numthreads(64, 1, 1)] void main(uint did
								  : SV_DispatchThreadID)
{
	uint maxWidth = (uint(g_BufferDimensions.x) + g_ProbeSpawnTileSize - 1) / g_ProbeSpawnTileSize;
	uint maxHeight = (uint(g_BufferDimensions.y) + g_ProbeSpawnTileSize - 1) / g_ProbeSpawnTileSize;

	if (did >= maxWidth * maxHeight)
		return;

	uint2 probe = uint2(did % maxWidth, did / maxWidth);
	uint2 jitter = min(CalculateHaltonSequence(g_FrameIndex) * g_ProbeSpawnTileSize, g_ProbeSpawnTileSize - 1.0f);
	uint2 seed = min(probe * g_ProbeSpawnTileSize + jitter, uint2(g_BufferDimensions) - 1);

	float3 normal = g_NormalsTexture.Load(int3(seed, 0)).xyz;
	bool is_sky_pixel = (dot(normal, normal) == 0.0f);

	if (!is_sky_pixel)
	{
		g_PrevProbeSpawn[did] = ScreenProbes_PackSeed(seed);
	}

	g_ProbeSpawnScan[did] = (!is_sky_pixel ? 1 : 0);
}
