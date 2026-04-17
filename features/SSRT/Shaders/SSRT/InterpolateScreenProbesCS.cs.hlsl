// Interpolate SH irradiance from 4 nearest probes to full resolution
// Uses depth/normal-weighted bilinear interpolation with bent cone AO

#include "SSRT/ScreenProbes.hlsli"
#include "SSRT/Random.hlsli"

Texture2D<float> g_DepthTexture : register(t0);
Texture2D<float4> g_NormalsTexture : register(t1);
Texture2D<uint> g_ProbeMask : register(t2);
Texture2D<float4> g_ProbeBufferSRV : register(t3);

RWTexture2D<float4> g_GIDenoiserColor : register(u0);
RWStructuredBuffer<uint2> g_ProbeSH : register(u1);

[numthreads(8, 8, 1)] void main(uint2 did
								 : SV_DispatchThreadID)
{
	float  depth        = g_DepthTexture.Load(int3(did, 0)).x;
	float3 raw_normal   = g_NormalsTexture.Load(int3(did, 0)).xyz;
	bool   is_sky_pixel = (dot(raw_normal, raw_normal) == 0.0f);

	if (is_sky_pixel || any(did >= uint2(g_BufferDimensions)))
	{
		g_GIDenoiserColor[did] = float4(0.0f, 0.0f, 0.0f, 1.0f);
		return;
	}

	float3 normal   = GBuffer::DecodeNormal(g_NormalsTexture.Load(int3(did, 0)).xy);
	float3 normalWS = normalize(FrameBuffer::ViewToWorld(normal, false));

	uint2  pos       = did;
	float2 uv        = (did + 0.5f) / g_BufferDimensions;
	float3 world_pos = reconstructWorldPosition(uv, depth);
	float  cell_size = distance(g_Eye, world_pos) * g_CellSize;

	// Jitter sample position for anti-aliasing
	RandomState rng = MakeRandom(did.x + did.y * uint(g_BufferDimensions.x), g_FrameIndex);
	float2 s        = rng.rand2();
	int2   jitter   = (2.0f * s - 1.0f) * g_ProbeSpawnTileSize;
	uint2  new_pos  = clamp(int2(did) + jitter, 0, int2(g_BufferDimensions) - 1);

	float2 new_uv        = (new_pos + 0.5f) / g_BufferDimensions;
	float  new_depth     = g_DepthTexture.Load(int3(new_pos, 0)).x;
	float3 new_world_pos = reconstructWorldPosition(new_uv, new_depth);

	if (abs(dot(new_world_pos - world_pos, normalWS)) < 0.5f * cell_size)
	{
		pos = new_pos;
	}

	// Locate nearby probes for interpolation
	uint4 probes;

	probes.x = ScreenProbes_FindClosestProbe(g_ProbeMask, pos);

	if (probes.x == kGI1_InvalidId)
	{
		g_GIDenoiserColor[did] = float4(0.0f, 0.0f, 0.0f, 1.0f);
		return;
	}

	uint2 center_seed = ScreenProbes_UnpackSeed(probes.x);
	int2  offset      = int2(pos.x < center_seed.x ? -1 : 1, pos.y < center_seed.y ? -1 : 1);

	probes.y = ScreenProbes_FindClosestProbe(g_ProbeMask, pos, int2(offset.x, 0));
	probes.z = ScreenProbes_FindClosestProbe(g_ProbeMask, pos, int2(0, offset.y));
	probes.w = ScreenProbes_FindClosestProbe(g_ProbeMask, pos, offset);

	// Deduplicate
	if (probes.y == probes.x)                                                 probes.y = kGI1_InvalidId;
	if (probes.z == probes.y || probes.z == probes.x)                         probes.z = kGI1_InvalidId;
	if (probes.w == probes.z || probes.w == probes.y || probes.w == probes.x) probes.w = kGI1_InvalidId;

	// Calculate depth/normal blending weights
	float4 w = float4(0.0f, 0.0f, 0.0f, 0.0f);

	[unroll]
	for (uint i = 0; i < 4; ++i)
	{
		if (probes[i] != kGI1_InvalidId)
		{
			uint2  probe_seed  = ScreenProbes_UnpackSeed(probes[i]);
			float2 probe_uv    = (probe_seed + 0.5f) / g_BufferDimensions;
			float  probe_depth = g_DepthTexture.Load(int3(probe_seed, 0)).x;
			float3 probe_pos   = reconstructWorldPosition(probe_uv, probe_depth);

			if (abs(dot(probe_pos - world_pos, normalWS)) > cell_size)
			{
				w[i] = 0.0f;
			}
			else
			{
				w[i]  = saturate(1.0f - abs(toLinearDepth(probe_depth, g_NearFar) - toLinearDepth(depth, g_NearFar)) / toLinearDepth(depth, g_NearFar));
				w[i] *= max(dot(normal, GBuffer::DecodeNormal(g_NormalsTexture.Load(int3(probe_seed, 0)).xy)), 0.0f);
				w[i]  = pow(w[i], 8.0f);
			}
		}
	}

	bool use_backup = false;

	if (dot(w, w) == 0.0f)
	{
		w = float4(1.0f,
			probes.y != kGI1_InvalidId ? 1.0f : 0.0f,
			probes.z != kGI1_InvalidId ? 1.0f : 0.0f,
			probes.w != kGI1_InvalidId ? 1.0f : 0.0f);

		use_backup = true;
	}

	w /= (w.x + w.y + w.z + w.w);

	// Evaluate SH irradiance from each probe
	float  ao         = 1.0f;
	float3 irradiance = float3(0.0f, 0.0f, 0.0f);

	[unroll]
	for (uint j = 0; j < 4; ++j)
	{
		if (probes[j] != kGI1_InvalidId)
		{
			uint2 probe_tile = ScreenProbes_UnpackSeed(probes[j]) / g_ProbeSize;

			irradiance += w[j] * ScreenProbes_CalculateSHIrradiance_BentCone(g_ProbeSH, normal, ao, probe_tile);
		}
	}

	float denoiser_hint = (use_backup ? 0.0f : 1.0f);

	g_GIDenoiserColor[did] = float4(irradiance, denoiser_hint);
}
