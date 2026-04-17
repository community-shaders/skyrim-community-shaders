// Spatial blur of screen probe radiance (separable 2-pass: X then Y)
// Finds nearby probes, reprojects directions, blends with depth/normal weights

#include "SSRT/ScreenProbes.hlsli"
#include "SSRT/GIDenoiser.hlsli"

Texture2D<float4> g_InputProbeBuffer : register(t0);  // input probe (SRV)
Texture2D<float> g_DepthTexture : register(t1);
Texture2D<float4> g_NormalsTexture : register(t2);
Texture2D<uint> g_ProbeMask : register(t3);

RWTexture2D<float4> g_OutputProbeBuffer : register(u0);  // output probe (UAV)
RWStructuredBuffer<uint> g_ProbeSpawn : register(u1);     // probe seeds
RWStructuredBuffer<uint> g_ProbeSpawnScan : register(u2); // scan (probe count)
RWStructuredBuffer<uint> g_ProbeSpawnIndex : register(u3); // index (probe count)

[numthreads(64, 1, 1)] void main(uint did
								  : SV_DispatchThreadID)
{
	uint probe_count = g_ProbeSpawnScan[g_MaxSpawnCount - 1]
	                 + g_ProbeSpawnIndex[g_MaxSpawnCount - 1];

	uint2 cell_and_probe_index = ScreenProbes_GetCellAndProbeIndex(did);
	uint  cell_index           = cell_and_probe_index.x;
	uint  probe_index          = cell_and_probe_index.y;

	if (probe_index >= probe_count)
		return;

	uint2 cell  = uint2(cell_index % g_ProbeSize, cell_index / g_ProbeSize);
	uint2 seed  = ScreenProbes_UnpackSeed(g_ProbeSpawn[probe_index]);
	uint2 probe = (seed / g_ProbeSize);
	uint2 pos   = (probe * g_ProbeSize) + cell;

	float2 uv        = (seed + 0.5f) / g_BufferDimensions;
	float  depth     = g_DepthTexture.Load(int3(seed, 0)).x;
	float3 normal    = GBuffer::DecodeNormal(g_NormalsTexture.Load(int3(seed, 0)).xy);
	float3 normalWS  = normalize(FrameBuffer::ViewToWorld(normal, false));
	float3 world_pos = reconstructWorldPosition(uv, depth);
	float  cell_size = distance(g_Eye, world_pos) * g_CellSize;

	float4 radiance     = g_InputProbeBuffer[pos];
	float3 direction    = mapToHemiOctahedron((cell + 0.5f) / g_ProbeSize);
	float  hit_distance = radiance.w;
	float  total_weight = 1.0f;

	float3 b1, b2;
	GetOrthoVectors(normal, b1, b2);
	direction = direction.x * b1 + direction.y * b2 + direction.z * normal;

	const int kRadius = 3;
	const int kSize   = (kRadius << 1);

	for (int i = 0; i < kSize; ++i)
	{
		int  step       = (((i & 1) << 1) - 1) * ((i >> 1) + 1);
		uint neighbor_mask = ScreenProbes_FindClosestProbe(g_ProbeMask, seed, step * g_BlurDirection);

		if (neighbor_mask == kGI1_InvalidId)
			continue;

		uint2  probe_seed   = ScreenProbes_UnpackSeed(neighbor_mask);
		float2 probe_uv     = (probe_seed + 0.5f) / g_BufferDimensions;
		float  probe_depth  = g_DepthTexture.Load(int3(probe_seed, 0)).x;
		float3 probe_normal = GBuffer::DecodeNormal(g_NormalsTexture.Load(int3(probe_seed, 0)).xy);
		float3 probe_world  = reconstructWorldPosition(probe_uv, probe_depth);

		if (abs(dot(probe_world - world_pos, normalWS)) > cell_size || dot(direction, probe_normal) < 0.0f)
			continue;

		uint2 probe_cell = uint2(mapToHemiOctahedronInverse(mul(direction, CreateTBN(probe_normal))) * g_ProbeSize);
		uint2 probe_tile = (probe_seed / g_ProbeSize);
		uint2 probe_pos  = (probe_tile * g_ProbeSize) + probe_cell;

		GetOrthoVectors(probe_normal, b1, b2);
		float3 probe_direction = mapToHemiOctahedron((probe_cell + 0.5f) / g_ProbeSize);
		probe_direction = normalize(probe_direction.x * b1 + probe_direction.y * b2 + probe_direction.z * probe_normal);

		float3 probe_direction_ws = normalize(FrameBuffer::ViewToWorld(probe_direction, false));
		float  probe_hit_distance = min(g_InputProbeBuffer[probe_pos].w, hit_distance);
		float3 hit_point          = probe_world + probe_direction_ws * probe_hit_distance;
		float3 reprojected_dir    = normalize(hit_point - world_pos);

		float3 reprojected_dir_vs = normalize(FrameBuffer::WorldToView(reprojected_dir, false));
		if (dot(direction, reprojected_dir_vs) < kGI1_AngleThreshold)
			continue;

		float weight = pow(saturate(1.0f - abs(toLinearDepth(probe_depth, g_NearFar) - toLinearDepth(depth, g_NearFar)) / toLinearDepth(depth, g_NearFar)), 8.0f);

		radiance     += weight * float4(GIDenoiser_RemoveNaNs(g_InputProbeBuffer[probe_pos].xyz), probe_hit_distance);
		total_weight += weight;

		hit_distance = radiance.w / total_weight;
	}

	g_OutputProbeBuffer[pos] = (radiance / total_weight);
}
