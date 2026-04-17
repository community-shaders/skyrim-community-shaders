// Temporal reprojection of screen probes using motion vectors
// Each 8x8 thread group maps to one probe tile
// Finds best matching probe from previous frame and reprojects radiance per cell

#include "SSRT/ScreenProbes.hlsli"
#include "SSRT/Halton.hlsli"

Texture2D<float> g_DepthTexture : register(t0);
Texture2D<float4> g_NormalsTexture : register(t1);
Texture2D<float2> g_VelocityTexture : register(t2);
Texture2D<float> g_PrevDepthTexture : register(t3);
Texture2D<float4> g_PrevNormalsTexture : register(t4);
Texture2D<uint> g_PrevProbeMask : register(t5);
Texture2D<float4> g_PrevProbeBuffer : register(t6);

RWTexture2D<uint> g_ProbeMask : register(u0);
RWTexture2D<float4> g_ProbeBuffer : register(u1);
RWStructuredBuffer<uint> g_EmptyTile : register(u2);
RWStructuredBuffer<uint> g_EmptyTileCount : register(u3);
RWStructuredBuffer<uint2> g_ProbeSH : register(u4);
RWStructuredBuffer<uint2> g_PrevProbeSH : register(u5);

[numthreads(8, 8, 1)] void main(uint2 did
								 : SV_DispatchThreadID, uint2 group_id
								 : SV_GroupID, uint2 local_id
								 : SV_GroupThreadID, uint local_index
								 : SV_GroupIndex)
{
	float  depth        = g_DepthTexture.Load(int3(did, 0)).x;
	float3 normal       = (all(did < uint2(g_BufferDimensions)) ? g_NormalsTexture.Load(int3(did, 0)).xyz : float3(0.0f, 0.0f, 0.0f));
	bool   is_sky_pixel = (dot(normal, normal) == 0.0f);

	normal = GBuffer::DecodeNormal(g_NormalsTexture.Load(int3(did, 0)).xy);
	float3 normalWS = normalize(FrameBuffer::ViewToWorld(normal, false));

	float2 uv        = (did + 0.5f) / g_BufferDimensions;
	float3 world_pos = reconstructWorldPosition(uv, depth);
	float  cell_size = distance(g_Eye, world_pos) * g_CellSize;

	uint2 cell          = (local_id % g_ProbeSize);
	uint  cell_index    = cell.x + cell.y * g_ProbeSize;
	uint2 probe_block   = (local_id / g_ProbeSize);
	uint  probe_segment = probe_block.x + (probe_block.y << 1);

	if (cell_index == 0)
	{
		lds_ScreenProbes_Reprojection[probe_segment] = ((f32tof16(65504.0f) << 16) | 0xFFFFu);
	}

	lds_ScreenProbes_RadianceValues[(local_index << 2) + 0] = 0;
	lds_ScreenProbes_RadianceValues[(local_index << 2) + 1] = 0;
	lds_ScreenProbes_RadianceValues[(local_index << 2) + 2] = 0;
	lds_ScreenProbes_RadianceValues[(local_index << 2) + 3] = 0;
	lds_ScreenProbes_RadianceSampleCounts[local_index] = 0;

	GroupMemoryBarrierWithGroupSync();

	// Find closest matching probe from previous frame via motion vectors
	if (!is_sky_pixel)
	{
		float2 velocity     = g_VelocityTexture.Load(int3(did, 0)).xy;
		float2 previous_uv  = uv - velocity;
		int2   previous_pos = int2(previous_uv * g_BufferDimensions);

		if (all(previous_pos >= 0) && all(previous_pos < int2(g_BufferDimensions)))
		{
			uint probe_mask = g_PrevProbeMask.Load(int3(previous_pos / g_ProbeSize, 0)).x;

			if (probe_mask != kGI1_InvalidId)
			{
				uint2  probe_seed   = ScreenProbes_UnpackSeed(probe_mask);
				float2 probe_uv     = (probe_seed + 0.5f) / g_BufferDimensions;
				float  probe_depth  = g_PrevDepthTexture.Load(int3(probe_seed, 0)).x;
				float3 probe_normal = GBuffer::DecodeNormal(g_PrevNormalsTexture.Load(int3(probe_seed, 0)).xy);
				float3 probe_pos    = reconstructPrevWorldPosition(probe_uv, probe_depth);

				if (abs(dot(probe_pos - world_pos, normalWS)) < cell_size && dot(normal, probe_normal) > 0.95f)
				{
					uint probe_score = ((f32tof16(distance(probe_pos, world_pos) / cell_size) << 16) | local_index);

					InterlockedMin(lds_ScreenProbes_Reprojection[probe_segment], probe_score);
				}
			}
		}
	}
	GroupMemoryBarrierWithGroupSync();

	// Map each lane to the winning probe and reproject radiance temporally
	uint  local_lane = (lds_ScreenProbes_Reprojection[probe_segment] & 0xFFFFu);
	uint2 seed       = (group_id << 3) + uint2(local_lane & 7, local_lane >> 3);

	uv = (seed + 0.5f) / g_BufferDimensions;

	float2 velocity     = g_VelocityTexture.Load(int3(seed, 0)).xy;
	float2 previous_uv  = uv - velocity;
	uint2  previous_pos = uint2(previous_uv * g_BufferDimensions);

	uint probe_mask = g_PrevProbeMask.Load(int3(previous_pos / g_ProbeSize, 0)).x;

	if (local_lane != 0xFFFFu)
	{
		depth     = g_DepthTexture.Load(int3(seed, 0)).x;
		normal    = GBuffer::DecodeNormal(g_NormalsTexture.Load(int3(seed, 0)).xy);
		normalWS  = normalize(FrameBuffer::ViewToWorld(normal, false));
		world_pos = reconstructWorldPosition(uv, depth);

		uint2  prev_seed    = ScreenProbes_UnpackSeed(probe_mask);
		float2 probe_uv     = (prev_seed + 0.5f) / g_BufferDimensions;
		float  probe_depth  = g_PrevDepthTexture.Load(int3(prev_seed, 0)).x;
		float3 probe_pos    = reconstructPrevWorldPosition(probe_uv, probe_depth);
		float3 probe_normal = GBuffer::DecodeNormal(g_PrevNormalsTexture.Load(int3(prev_seed, 0)).xy);

		float4 probe_radiance  = g_PrevProbeBuffer[((prev_seed / g_ProbeSize) * g_ProbeSize) + cell];
		float3 probe_direction = mapToHemiOctahedron((cell + 0.5f) / g_ProbeSize);

		float3 b1, b2;
		GetOrthoVectors(probe_normal, b1, b2);
		probe_direction = probe_direction.x * b1 + probe_direction.y * b2 + probe_direction.z * probe_normal;

		float3 probe_direction_ws = normalize(FrameBuffer::ViewToWorld(probe_direction, false));
		float3 hit_point       = probe_pos + probe_direction_ws * probe_radiance.w;
		float3 reprojected_dir = hit_point - world_pos;
		float  reprojected_len = length(reprojected_dir);

		reprojected_dir /= reprojected_len;

		if (dot(normalWS, reprojected_dir) > 0.0f)
		{
			float3 reprojected_dir_vs = normalize(FrameBuffer::WorldToView(reprojected_dir, false));
			float2 remap_uv         = mapToHemiOctahedronInverse(mul(reprojected_dir_vs, CreateTBN(normal)));
			uint2  remap_cell       = uint2(remap_uv * g_ProbeSize);
			uint   remap_cell_index = remap_cell.x + remap_cell.y * g_ProbeSize;
			uint4  remap_radiance   = ScreenProbes_QuantizeRadiance(float4(probe_radiance.xyz, reprojected_len));

			InterlockedAdd(lds_ScreenProbes_RadianceValues[(remap_cell_index << 2) + 0], remap_radiance.x);
			InterlockedAdd(lds_ScreenProbes_RadianceValues[(remap_cell_index << 2) + 1], remap_radiance.y);
			InterlockedAdd(lds_ScreenProbes_RadianceValues[(remap_cell_index << 2) + 2], remap_radiance.z);
			InterlockedAdd(lds_ScreenProbes_RadianceValues[(remap_cell_index << 2) + 3], remap_radiance.w);
			InterlockedAdd(lds_ScreenProbes_RadianceSampleCounts[remap_cell_index], 1);
		}
	}
	GroupMemoryBarrierWithGroupSync();

	// Calculate backup radiance for unvisited cells
	lds_ScreenProbes_RadianceBackup[local_index] = float4(
		ScreenProbes_RecoverRadiance(uint3(
			lds_ScreenProbes_RadianceValues[(local_index << 2) + 0],
			lds_ScreenProbes_RadianceValues[(local_index << 2) + 1],
			lds_ScreenProbes_RadianceValues[(local_index << 2) + 2])),
		lds_ScreenProbes_RadianceSampleCounts[local_index] > 0.0f ? 1.0f : 0.0f);
	GroupMemoryBarrierWithGroupSync();

	for (uint stride = 1; stride < 64; stride <<= 1)
	{
		if (local_index < 64 / (2 * stride))
			lds_ScreenProbes_RadianceBackup[2 * (local_index + 1) * stride - 1] += lds_ScreenProbes_RadianceBackup[(2 * local_index + 1) * stride - 1];
		GroupMemoryBarrierWithGroupSync();
	}

	if (local_index == 0)
	{
		float4 total_radiance   = lds_ScreenProbes_RadianceBackup[64 - 1];
		float3 radiance         = total_radiance.xyz / max(total_radiance.w, 1.0f);
		float  empty_cell_count = (g_ProbeSize * g_ProbeSize - total_radiance.w);

		lds_ScreenProbes_RadianceBackup[0] = float4(radiance / max(empty_cell_count, 1.0f), MAX_HIT_DISTANCE);
	}
	GroupMemoryBarrierWithGroupSync();

	// Disocclusion: no matching probe found
	if (local_lane == 0xFFFFu)
	{
		if (cell_index == 0)
		{
			uint2 probe = (did / g_ProbeSize);

			if (!is_sky_pixel)
			{
				uint2 jitter = min(CalculateHaltonSequence(g_FrameIndex) * g_ProbeSpawnTileSize, g_ProbeSpawnTileSize - 1.0f);

				// Check whether this tile won't be filled during spawn pass
				if (any((jitter / g_ProbeSize) != (probe % (g_ProbeSpawnTileSize / g_ProbeSize))))
				{
					uint empty_tile_index;
					InterlockedAdd(g_EmptyTileCount[0], 1, empty_tile_index);

					uint probe_count = (uint(g_BufferDimensions.x) + g_ProbeSize - 1) / g_ProbeSize;
					uint probe_index = (probe.x + probe.y * probe_count);

					g_EmptyTile[empty_tile_index] = probe_index;
				}
			}

			g_ProbeMask[probe] = kGI1_InvalidId;
		}

		return;
	}

	// Reproject SH coefficients
	if (cell_index < 9)
	{
		uint2 probe          = (did / g_ProbeSize);
		uint2 previous_probe = (ScreenProbes_UnpackSeed(probe_mask) / g_ProbeSize);

		uint probe_count          = (uint(g_BufferDimensions.x) + g_ProbeSize - 1) / g_ProbeSize;
		uint probe_index          = (probe.x + probe.y * probe_count);
		uint previous_probe_index = (previous_probe.x + previous_probe.y * probe_count);

		if (cell_index == 0)
		{
			g_ProbeMask[probe] = ScreenProbes_PackSeed(seed);
		}

		g_ProbeSH[9 * probe_index + cell_index] = g_PrevProbeSH[9 * previous_probe_index + cell_index];
	}

	// Write reprojected radiance
	float4 radiance = ScreenProbes_RecoverRadiance(uint4(
		lds_ScreenProbes_RadianceValues[(local_index << 2) + 0],
		lds_ScreenProbes_RadianceValues[(local_index << 2) + 1],
		lds_ScreenProbes_RadianceValues[(local_index << 2) + 2],
		lds_ScreenProbes_RadianceValues[(local_index << 2) + 3]));

	uint sample_count = lds_ScreenProbes_RadianceSampleCounts[local_index];

	if (sample_count > 0)
	{
		radiance /= sample_count;
	}
	else
	{
		radiance = lds_ScreenProbes_RadianceBackup[0];
	}

	g_ProbeBuffer[did] = radiance;
}
