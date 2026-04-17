// Project per-cell radiance to SH9 coefficients
// Each probe (64 cells for probeSize=8) produces 9 SH coefficients via groupshared reduction

#include "SSRT/ScreenProbes.hlsli"

Texture2D<float4> g_NormalsTexture : register(t0);

RWTexture2D<float4> g_ProbeBuffer : register(u0);         // probe buffer (read radiance)
RWStructuredBuffer<uint2> g_ProbeSH : register(u1);        // SH output
RWStructuredBuffer<uint> g_ProbeSpawn : register(u2);      // probe seeds
RWStructuredBuffer<uint> g_ProbeSpawnScan : register(u3);  // scan (probe count)
RWStructuredBuffer<uint> g_ProbeSpawnIndex : register(u4); // index (probe count)

[numthreads(64, 1, 1)] void main(uint did
								  : SV_DispatchThreadID, uint local_id
								  : SV_GroupThreadID)
{
	uint probe_count = g_ProbeSpawnScan[g_MaxSpawnCount - 1]
	                 + g_ProbeSpawnIndex[g_MaxSpawnCount - 1];

	uint2 cell_and_probe_index = ScreenProbes_GetCellAndProbeIndex(did);
	uint  cell_index           = cell_and_probe_index.x;
	uint  probe_index          = cell_and_probe_index.y;

	uint2 cell      = uint2(cell_index % g_ProbeSize, cell_index / g_ProbeSize);
	uint2 seed      = (probe_index < probe_count ? ScreenProbes_UnpackSeed(g_ProbeSpawn[probe_index]) : uint2(0xFFFF, 0xFFFF));
	uint2 probe     = (seed / g_ProbeSize);
	uint2 probe_pos = (probe * g_ProbeSize) + cell;

	// Project radiance onto SH basis
	if (probe_index < probe_count)
	{
		float3 normal    = GBuffer::DecodeNormal(g_NormalsTexture.Load(int3(seed, 0)).xy);
		float3 radiance  = g_ProbeBuffer[probe_pos].xyz / g_ProbeSize;
		float3 direction = mapToHemiOctahedron((cell + 0.5f) / g_ProbeSize);

		float3 b1, b2;
		GetOrthoVectors(normal, b1, b2);
		direction = direction.x * b1 + direction.y * b2 + direction.z * normal;

		float direction_sh[9];
		SH_GetCoefficients(direction, direction_sh);

		[unroll]
		for (uint j = 0; j < 9; ++j)
		{
			lds_ScreenProbes_ProbeSHBuffer[9 * local_id + j] = float4(direction_sh[j] * radiance, 1.0f);
		}
	}
	GroupMemoryBarrierWithGroupSync();

	if (probe_index >= probe_count)
		return;

	// Reduce across all cells to produce 9 SH coefficients per probe
	uint sh_index = cell.x + cell.y * g_ProbeSize;

	if (sh_index < 9)
	{
		float4 irradiance_sh = float4(0.0f, 0.0f, 0.0f, 0.0f);

		uint2 offset = (uint2(local_id & 7, local_id >> 3) / g_ProbeSize) * g_ProbeSize;

		for (uint y = 0; y < g_ProbeSize; ++y)
		{
			for (uint x = 0; x < g_ProbeSize; ++x)
			{
				uint2 p     = uint2(x, y) + offset;
				uint  index = p.x + (p.y << 3);

				irradiance_sh += lds_ScreenProbes_ProbeSHBuffer[9 * index + sh_index];
			}
		}

		uint index = probe.x + probe.y * ((uint(g_BufferDimensions.x) + g_ProbeSize - 1) / g_ProbeSize);

		g_ProbeSH[9 * index + sh_index] = ScreenProbes_PackSHColor(irradiance_sh);
	}
}
