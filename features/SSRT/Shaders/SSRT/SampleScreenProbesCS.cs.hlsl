// Importance-sample ray directions for screen probes
// Builds CDF from neighbor probe radiance and samples directions via ray guiding
// Writes sample directions and reprojected radiance for BlendScreenProbes

#include "SSRT/ScreenProbes.hlsli"
#include "SSRT/Random.hlsli"

Texture2D<float> g_DepthTexture : register(t0);
Texture2D<float4> g_NormalsTexture : register(t1);
Texture2D<float4> g_ProbeBufferSRV : register(t2);  // probe buffer (neighbor radiance)
Texture2D<uint> g_ProbeMask : register(t3);

RWStructuredBuffer<uint2> g_ProbeSpawnSample : register(u0);     // output: sample directions
RWStructuredBuffer<uint2> g_ProbeSpawnRadiance : register(u1);   // output: cleared for populate
RWStructuredBuffer<uint> g_ProbeSpawn : register(u2);            // probe spawn seeds
RWStructuredBuffer<uint> g_ProbeSpawnScan : register(u3);        // scan flags (probe count)
RWStructuredBuffer<uint> g_ProbeSpawnIndex : register(u4);       // index (probe count)
RWTexture2D<float4> g_PrevProbeBuffer : register(u5);            // write reprojected radiance

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

	float2 uv        = (seed + 0.5f) / g_BufferDimensions;
	float  depth     = g_DepthTexture.Load(int3(seed, 0)).x;
	float3 normal    = GBuffer::DecodeNormal(g_NormalsTexture.Load(int3(seed, 0)).xy);
	float3 normalWS  = normalize(FrameBuffer::ViewToWorld(normal, false));
	float3 world_pos = reconstructWorldPosition(uv, depth);

	float cell_size  = distance(g_Eye, world_pos) * g_CellSize;
	int2  probe_res  = int2((uint2(g_BufferDimensions) + g_ProbeSize - 1) / g_ProbeSize);

	if (local_id == 0)
	{
		lds_ScreenProbes_RadianceReuseSampleCount = 0;
	}

	lds_ScreenProbes_RadianceValues[(local_id << 2) + 0] = 0;
	lds_ScreenProbes_RadianceValues[(local_id << 2) + 1] = 0;
	lds_ScreenProbes_RadianceValues[(local_id << 2) + 2] = 0;
	lds_ScreenProbes_RadianceValues[(local_id << 2) + 3] = 0;
	lds_ScreenProbes_RadianceSampleCounts[local_id] = 0;
	GroupMemoryBarrierWithGroupSync();

	// Accumulate radiance from neighbor probes (3x3 neighborhood)
	if (probe_index < probe_count)
	{
		const int kRadius = 1;

		for (int y = -kRadius; y <= kRadius; ++y)
		{
			for (int x = -kRadius; x <= kRadius; ++x)
			{
				int2 tap = int2(probe) + int2(x, y);

				if (any(tap < 0) || any(tap >= probe_res))
					continue;

				uint probe_mask = g_ProbeMask.Load(int3(tap, 0)).x;

				if (probe_mask == kGI1_InvalidId)
					continue;

				if (x == 0 && y == 0)
					InterlockedAdd(lds_ScreenProbes_RadianceReuseSampleCount, 1);

				uint2  probe_seed   = ScreenProbes_UnpackSeed(probe_mask);
				float2 probe_uv     = (probe_seed + 0.5f) / g_BufferDimensions;
				float  probe_depth  = g_DepthTexture.Load(int3(probe_seed, 0)).x;
				float3 probe_pos    = reconstructWorldPosition(probe_uv, probe_depth);

				if (abs(dot(probe_pos - world_pos, normalWS)) > cell_size)
					continue;

				float3 probe_normal    = GBuffer::DecodeNormal(g_NormalsTexture.Load(int3(probe_seed, 0)).xy);
				float4 probe_radiance  = g_ProbeBufferSRV[((probe_seed / g_ProbeSize) * g_ProbeSize) + cell];
				float3 probe_direction = mapToHemiOctahedron((cell + 0.5f) / g_ProbeSize);

				float3 b1, b2;
				GetOrthoVectors(probe_normal, b1, b2);
				probe_direction = probe_direction.x * b1 + probe_direction.y * b2 + probe_direction.z * probe_normal;

				float3 probe_direction_ws = normalize(FrameBuffer::ViewToWorld(probe_direction, false));
				float3 hit_point       = probe_pos + probe_direction_ws * probe_radiance.w;
				float3 reprojected_dir = hit_point - world_pos;
				float  reprojected_len = length(reprojected_dir);

				reprojected_dir /= reprojected_len;

				if (dot(normalWS, reprojected_dir) < 0.0f)
					continue;

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
	}
	GroupMemoryBarrierWithGroupSync();

	// Recover accumulated radiance
	float  total_weight      = float(lds_ScreenProbes_RadianceSampleCounts[local_id]);
	float4 previous_radiance = ScreenProbes_RecoverRadiance(uint4(
		lds_ScreenProbes_RadianceValues[(local_id << 2) + 0],
		lds_ScreenProbes_RadianceValues[(local_id << 2) + 1],
		lds_ScreenProbes_RadianceValues[(local_id << 2) + 2],
		lds_ScreenProbes_RadianceValues[(local_id << 2) + 3]));

	if (total_weight > 0.0f)
	{
		previous_radiance /= total_weight;
	}
	else
	{
		previous_radiance.w = -1.0f;
	}

	// Build CDF from neighbor radiance for importance sampling
	float3 direction = mapToHemiOctahedron((cell + 0.5f) / g_ProbeSize);
	float  radiance  = luminance(previous_radiance.xyz) * dot(direction, float3(0.0f, 0.0f, 1.0f));

	ScreenProbes_ScanRadiance(local_id, radiance);

	if (probe_index >= probe_count)
		return;

	// Sample direction via CDF-guided ray guiding (diffuse only, no specular BRDF)
	{
		RandomState randomNG = MakeRandom(did, g_FrameIndex);

		uint  sampled_cell_index = ScreenProbes_FindCellIndex(local_id, randomNG.rand());
		uint2 sampled_cell       = (total_weight > 0.0f
				? uint2(sampled_cell_index % g_ProbeSize, sampled_cell_index / g_ProbeSize)
				: cell);
		direction = mapToHemiOctahedron((sampled_cell + randomNG.rand2()) / g_ProbeSize);

		float3 b1, b2;
		GetOrthoVectors(normal, b1, b2);
		direction = direction.x * b1 + direction.y * b2 + direction.z * normal;
	}

	g_PrevProbeBuffer[pos]       = previous_radiance;
	g_ProbeSpawnSample[did]      = ScreenProbes_PackSample(direction);
}
