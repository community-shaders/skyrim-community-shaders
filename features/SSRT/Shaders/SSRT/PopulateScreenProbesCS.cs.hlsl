// Populate screen probes via Hi-Z screen-space tracing (replaces DXR raytracing)
// Reads sample directions from SampleScreenProbes, traces against depth pyramid,
// writes hit radiance and distance

#include "SSRT/ScreenProbes.hlsli"
#include "SSRT/HiZTrace.hlsli"

Texture2D<float> g_DepthPyramid : register(t0);
Texture2D<float4> g_RadiancePyramid : register(t1);
Texture2D<float4> g_NormalsTexture : register(t2);

RWStructuredBuffer<uint2> g_ProbeSpawnRadiance : register(u0);  // output: traced radiance
RWStructuredBuffer<uint> g_ProbeSpawn : register(u1);           // probe seeds
RWStructuredBuffer<uint2> g_ProbeSpawnSample : register(u2);    // sample directions
RWStructuredBuffer<uint> g_ProbeSpawnScan : register(u3);       // scan (probe count)
RWStructuredBuffer<uint> g_ProbeSpawnIndex : register(u4);      // index (probe count)

[numthreads(32, 1, 1)] void main(uint did
								  : SV_DispatchThreadID)
{
	uint probe_count = g_ProbeSpawnScan[g_MaxSpawnCount - 1]
	                 + g_ProbeSpawnIndex[g_MaxSpawnCount - 1];

	uint2 cell_and_probe_index = ScreenProbes_GetCellAndProbeIndex(did);
	uint  probe_index          = cell_and_probe_index.y;

	if (probe_index >= probe_count)
		return;

	uint2  seed         = ScreenProbes_UnpackSeed(g_ProbeSpawn[probe_index]);
	float3 raw_normal   = g_NormalsTexture.Load(int3(seed, 0)).xyz;
	bool   is_sky_pixel = (dot(raw_normal, raw_normal) == 0.0f);

	if (is_sky_pixel)
		return;

	// Direction from SampleScreenProbes is in VIEW SPACE (TBN built from view-space normals)
	float3 directionVS = ScreenProbes_UnpackSample(g_ProbeSpawnSample[did]);
	float3 normalVS    = GBuffer::DecodeNormal(g_NormalsTexture.Load(int3(seed, 0)).xy);

	// Convert view-space direction and normal to world space (matches DeferredCompositeCS pattern)
	float3 directionWS = normalize(FrameBuffer::ViewToWorld(directionVS, false));
	float3 normalWS    = normalize(FrameBuffer::ViewToWorld(normalVS, false));

	float2 uv        = (seed + 0.5f) / g_BufferDimensions;
	float  depth     = g_DepthPyramid.Load(int3(seed, 0)).x;
	float3 world_pos = reconstructWorldPosition(uv, depth);

	// Normal bias: push origin off surface to avoid self-intersection
	// Scale by linear depth so bias is consistent across distances
	float linearZ = SharedData::GetScreenDepth(depth);
	world_pos += normalWS * 0.005f * linearZ;

	// Trace via Hi-Z screen-space depth pyramid
	HiZTraceResult result = HiZTrace(
		g_DepthPyramid,
		g_RadiancePyramid,
		world_pos,
		directionWS,
		g_HiZMaxDistance);

	float3 hit_radiance = result.hit ? result.hitRadiance : float3(0.0f, 0.0f, 0.0f);
	float  hit_distance = result.hit ? result.hitDistance : MAX_HIT_DISTANCE;

	g_ProbeSpawnRadiance[did] = ScreenProbes_PackRadiance(float4(hit_radiance, hit_distance));
}
