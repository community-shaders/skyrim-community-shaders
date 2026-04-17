// Temporal blending of new traced radiance into probe buffer
// Accumulates traced samples into cells, applies shadow-preserving hysteresis

#include "SSRT/ScreenProbes.hlsli"

Texture2D<float4> g_PrevProbeBuffer : register(t0);
Texture2D<float4> g_NormalsTexture : register(t1);

RWTexture2D<float4> g_ProbeBuffer : register(u0);               // current probe buffer (output)
RWTexture2D<uint> g_ProbeMask : register(u1);                    // probe mask (write seed on output)
RWStructuredBuffer<uint2> g_ProbeSpawnRadiance : register(u2);   // traced radiance
RWStructuredBuffer<uint2> g_ProbeSpawnSample : register(u3);     // sample directions
RWStructuredBuffer<uint> g_ProbeSpawn : register(u4);            // probe seeds
RWStructuredBuffer<uint> g_ProbeSpawnScan : register(u5);        // scan (probe count)
RWStructuredBuffer<uint> g_ProbeSpawnIndex : register(u6);       // index (probe count)

[numthreads(64, 1, 1)] void main(uint did
								  : SV_DispatchThreadID, uint local_id
								  : SV_GroupThreadID)
{
	uint probe_count = g_ProbeSpawnScan[g_MaxSpawnCount - 1]
	                 + g_ProbeSpawnIndex[g_MaxSpawnCount - 1];

	uint2 cell_and_probe_index = ScreenProbes_GetCellAndProbeIndex(did);
	uint  cell_index           = cell_and_probe_index.x;
	uint  probe_index          = cell_and_probe_index.y;

	uint2 cell  = uint2(cell_index % g_ProbeSize, cell_index / g_ProbeSize);
	uint2 seed  = (probe_index < probe_count ? ScreenProbes_UnpackSeed(g_ProbeSpawn[probe_index]) : uint2(0xFFFF, 0xFFFF));
	uint2 probe = (seed / g_ProbeSize);
	uint2 pos   = (probe * g_ProbeSize) + cell;

	// Initialize LDS
	{
		lds_ScreenProbes_RadianceValues[(local_id << 2) + 0] = 0;
		lds_ScreenProbes_RadianceValues[(local_id << 2) + 1] = 0;
		lds_ScreenProbes_RadianceValues[(local_id << 2) + 2] = 0;
		lds_ScreenProbes_RadianceValues[(local_id << 2) + 3] = 0;
		lds_ScreenProbes_RadianceSampleCounts[local_id] = 0;
	}
	GroupMemoryBarrierWithGroupSync();

	// Accumulate traced radiance into the sampled cell
	if (probe_index < probe_count)
	{
		float3 normal    = GBuffer::DecodeNormal(g_NormalsTexture.Load(int3(seed, 0)).xy);
		float3 direction = ScreenProbes_UnpackSample(g_ProbeSpawnSample[did]);
		float4 radiance  = ScreenProbes_UnpackRadiance(g_ProbeSpawnRadiance[did]);

		uint4 quantized_radiance = ScreenProbes_QuantizeRadiance(radiance);
		uint2 sampled_cell       = uint2(mapToHemiOctahedronInverse(mul(direction, CreateTBN(normal))) * g_ProbeSize);
		uint  sampled_cell_index = sampled_cell.x + sampled_cell.y * g_ProbeSize;

		InterlockedAdd(lds_ScreenProbes_RadianceValues[(sampled_cell_index << 2) + 0], quantized_radiance.x);
		InterlockedAdd(lds_ScreenProbes_RadianceValues[(sampled_cell_index << 2) + 1], quantized_radiance.y);
		InterlockedAdd(lds_ScreenProbes_RadianceValues[(sampled_cell_index << 2) + 2], quantized_radiance.z);
		InterlockedAdd(lds_ScreenProbes_RadianceValues[(sampled_cell_index << 2) + 3], quantized_radiance.w);
		InterlockedAdd(lds_ScreenProbes_RadianceSampleCounts[sampled_cell_index], 1);
	}
	GroupMemoryBarrierWithGroupSync();

	// Calculate backup radiance for untraced cells
	lds_ScreenProbes_RadianceBackup[local_id] = float4(
		ScreenProbes_RecoverRadiance(uint3(
			lds_ScreenProbes_RadianceValues[(local_id << 2) + 0],
			lds_ScreenProbes_RadianceValues[(local_id << 2) + 1],
			lds_ScreenProbes_RadianceValues[(local_id << 2) + 2])),
		lds_ScreenProbes_RadianceSampleCounts[local_id] > 0.0f ? 1.0f : 0.0f);
	GroupMemoryBarrierWithGroupSync();

	for (uint stride = 1; stride < 64; stride <<= 1)
	{
		if (local_id < 64 / (2 * stride))
			lds_ScreenProbes_RadianceBackup[2 * (local_id + 1) * stride - 1] += lds_ScreenProbes_RadianceBackup[(2 * local_id + 1) * stride - 1];
		GroupMemoryBarrierWithGroupSync();
	}

	if (local_id == 0)
	{
		float4 total_radiance   = lds_ScreenProbes_RadianceBackup[64 - 1];
		float3 radiance_avg     = total_radiance.xyz / max(total_radiance.w, 1.0f);
		float  empty_cell_count = (g_ProbeSize * g_ProbeSize - total_radiance.w);

		lds_ScreenProbes_RadianceBackup[0] = float4(radiance_avg / max(empty_cell_count, 1.0f), MAX_HIT_DISTANCE);
	}
	GroupMemoryBarrierWithGroupSync();

	if (probe_index >= probe_count)
		return;

	// Recover new radiance for this cell
	float4 radiance = ScreenProbes_RecoverRadiance(uint4(
		lds_ScreenProbes_RadianceValues[(local_id << 2) + 0],
		lds_ScreenProbes_RadianceValues[(local_id << 2) + 1],
		lds_ScreenProbes_RadianceValues[(local_id << 2) + 2],
		lds_ScreenProbes_RadianceValues[(local_id << 2) + 3]));

	uint sample_count = lds_ScreenProbes_RadianceSampleCounts[local_id];

	if (sample_count > 0)
	{
		radiance /= sample_count;
	}
	else
	{
		radiance = lds_ScreenProbes_RadianceBackup[0];
	}

	// Shadow-preserving biased temporal hysteresis
	float4 previous_radiance = g_PrevProbeBuffer[pos];

	if (previous_radiance.w > 0.0f)
	{
		float lumaA = luminance(radiance.xyz);
		float lumaB = luminance(previous_radiance.xyz);

		float temporal_blend = squared(clamp(
			max(lumaA - lumaB - min(lumaA, lumaB), 0.0f) / max(max(lumaA, lumaB), 1e-4f),
			0.0f, 0.95f));

		radiance = lerp(radiance, previous_radiance, temporal_blend);
	}

	// Write probe mask and blended radiance
	if (cell_index == 0)
	{
		g_ProbeMask[probe] = ScreenProbes_PackSeed(seed);
	}

	g_ProbeBuffer[pos] = radiance;
}
