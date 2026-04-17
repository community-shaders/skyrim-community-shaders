// AMD Capsaicin GI-1.1 — Screen-space probe helpers
// Ported from Capsaicin screen_probes.hlsl (stripped of cached tile LRU)

#ifndef SCREEN_PROBES_HLSLI
#define SCREEN_PROBES_HLSLI

#include "SSRT/GI1Common.hlsli"
#include "SSRT/PackUtils.hlsli"
#include "SSRT/SH9.hlsli"
#include "SSRT/HemiOctahedral.hlsli"

// Groupshared declarations (sized for 64-thread groups, probeSize=8 -> 64 cells)
groupshared float lds_ScreenProbes_Radiance[64];
groupshared uint lds_ScreenProbes_Reprojection[4];
groupshared float4 lds_ScreenProbes_ProbeSHBuffer[9 * 64];
groupshared uint lds_ScreenProbes_RadianceValues[4 * 64];
groupshared uint lds_ScreenProbes_RadianceSampleCounts[64];
groupshared uint lds_ScreenProbes_RadianceReuseSampleCount;
groupshared float4 lds_ScreenProbes_RadianceBackup[64];

//
// Pack/unpack helpers
//

uint ScreenProbes_PackSeed(in uint2 seed)
{
	return (seed.x << 16) | seed.y;
}

uint2 ScreenProbes_UnpackSeed(in uint packed_seed)
{
	return uint2(packed_seed >> 16, packed_seed & 0xFFFFu);
}

uint2 ScreenProbes_PackSHColor(in float4 sh_color)
{
	return packHalf4(sh_color);
}

float4 ScreenProbes_UnpackSHColor(in uint2 packed_sh_color)
{
	return unpackHalf4(packed_sh_color);
}

uint2 ScreenProbes_PackRadiance(in float4 radiance)
{
	return packHalf4(radiance);
}

float4 ScreenProbes_UnpackRadiance(in uint2 packed_radiance)
{
	return unpackHalf4(packed_radiance);
}

uint2 ScreenProbes_PackSample(in float3 direction)
{
	return packHalf3(direction);
}

float3 ScreenProbes_UnpackSample(in uint2 packed_sample)
{
	return unpackHalf3(packed_sample);
}

uint4 ScreenProbes_QuantizeRadiance(in float4 radiance)
{
	return uint4(round(kGI1_FloatQuantize * radiance));
}

float3 ScreenProbes_RecoverRadiance(in uint3 quantized_radiance)
{
	return quantized_radiance / kGI1_FloatQuantize;
}

float4 ScreenProbes_RecoverRadiance(in uint4 quantized_radiance)
{
	return quantized_radiance / kGI1_FloatQuantize;
}

//
// Cell/probe index mapping
//

uint2 ScreenProbes_GetCellAndProbeIndex(in uint query_index)
{
	return uint2(
		query_index % (g_ProbeSize * g_ProbeSize),
		query_index / (g_ProbeSize * g_ProbeSize));
}

//
// Probe mask mip-based closest probe search
//

uint ScreenProbes_FindClosestProbe(in Texture2D<uint> probeMask, in uint2 pos)
{
	uint2 dims = uint2(
		(g_BufferDimensions.x + g_ProbeSize - 1) / g_ProbeSize,
		(g_BufferDimensions.y + g_ProbeSize - 1) / g_ProbeSize);

	pos = min(pos / g_ProbeSize, dims - 1);

	for (uint i = 0; i < g_ProbeMaskMipCount; ++i)
	{
		uint probe = probeMask.Load(int3(pos, i)).x;

		if (probe != kGI1_InvalidId)
			return probe;

		dims = max(dims >> 1, 1);
		pos = min(pos >> 1, dims - 1);
	}

	return kGI1_InvalidId;
}

// Directional offset variant for finding neighbor probes
uint ScreenProbes_FindClosestProbe(in Texture2D<uint> probeMask, in uint2 pos, in int2 offset)
{
	uint2 dims = uint2(
		(g_BufferDimensions.x + g_ProbeSize - 1) / g_ProbeSize,
		(g_BufferDimensions.y + g_ProbeSize - 1) / g_ProbeSize);

	pos = min(pos / g_ProbeSize, dims - 1);

	for (uint i = 0; i < g_ProbeMaskMipCount; ++i)
	{
		int2 loc = int2(pos) + offset;

		if (any(loc < 0) || any(loc >= (int2)dims))
			break;

		uint probe = probeMask.Load(int3(loc, i)).x;

		if (probe != kGI1_InvalidId)
			return probe;

		dims = max(dims >> 1, 1);
		pos = min(pos >> 1, dims - 1);
	}

	return kGI1_InvalidId;
}

//
// SH irradiance evaluation from probe buffer
//

float3 ScreenProbes_CalculateSHIrradiance(
	in RWStructuredBuffer<uint2> shBuffer,
	in float3 normal,
	in uint2 probe)
{
	float clamped_cosine_sh[9];
	SH_GetCoefficients_ClampedCosine(normal, clamped_cosine_sh);

	uint probe_count = (g_BufferDimensions.x + g_ProbeSize - 1) / g_ProbeSize;
	uint probe_index = probe.x + probe.y * probe_count;

	float3 irradiance = 0.0f;

	[unroll]
	for (uint i = 0; i < 9; ++i)
	{
		irradiance += clamped_cosine_sh[i] * ScreenProbes_UnpackSHColor(shBuffer[9 * probe_index + i]).xyz;
	}

	return max(irradiance, 0.0f);
}

float3 ScreenProbes_CalculateSHIrradiance_BentCone(
	in RWStructuredBuffer<uint2> shBuffer,
	in float3 normal,
	in float ao,
	in uint2 probe)
{
	float clamped_cosine_sh[9];
	SH_GetCoefficients_ClampedCosine_Cone(normal, acos(sqrt(saturate(1.0f - ao))), clamped_cosine_sh);

	uint probe_count = (g_BufferDimensions.x + g_ProbeSize - 1) / g_ProbeSize;
	uint probe_index = probe.x + probe.y * probe_count;

	float3 irradiance = 0.0f;

	[unroll]
	for (uint i = 0; i < 9; ++i)
	{
		irradiance += clamped_cosine_sh[i] * ScreenProbes_UnpackSHColor(shBuffer[9 * probe_index + i]).xyz;
	}

	return max(irradiance, 0.0f);
}

//
// CDF scan for importance sampling ray directions (Blelloch in groupshared)
//

uint ScreenProbes_FindCellIndex(in uint local_id, in float s)
{
	uint index = 0;
	uint count = (g_ProbeSize * g_ProbeSize);
	uint start = (local_id / count) * count;

	while (0 < count)
	{
		uint count2 = (count >> 1);
		uint mid = (index + count2);

		if (lds_ScreenProbes_Radiance[mid + start] > s)
			count = count2;
		else
		{
			index = (mid + 1);
			count -= (count2 + 1);
		}
	}

	return max(index, 1) - 1;
}

void ScreenProbes_ScanRadiance(in uint local_id, in float radiance)
{
	uint stride;

	lds_ScreenProbes_Radiance[local_id] = radiance;
	GroupMemoryBarrierWithGroupSync();

	uint block_size = (g_ProbeSize * g_ProbeSize);
	uint first_lane = (local_id / block_size) * block_size;
	uint local_lane = (local_id - first_lane);

	radiance = lds_ScreenProbes_Radiance[first_lane + block_size - 1];
	GroupMemoryBarrierWithGroupSync();

	for (stride = 1; stride <= (block_size >> 1); stride <<= 1)
	{
		if (local_lane < block_size / (2 * stride))
		{
			lds_ScreenProbes_Radiance[first_lane + 2 * (local_lane + 1) * stride - 1] +=
				lds_ScreenProbes_Radiance[first_lane + (2 * local_lane + 1) * stride - 1];
		}
		GroupMemoryBarrierWithGroupSync();
	}

	if (local_lane == 0)
	{
		lds_ScreenProbes_Radiance[first_lane + block_size - 1] = 0.0f;
	}
	GroupMemoryBarrierWithGroupSync();

	for (stride = (block_size >> 1); stride > 0; stride >>= 1)
	{
		if (local_lane < block_size / (2 * stride))
		{
			float tmp = lds_ScreenProbes_Radiance[first_lane + (2 * local_lane + 1) * stride - 1];
			lds_ScreenProbes_Radiance[first_lane + (2 * local_lane + 1) * stride - 1] = lds_ScreenProbes_Radiance[first_lane + 2 * (local_lane + 1) * stride - 1];
			lds_ScreenProbes_Radiance[first_lane + 2 * (local_lane + 1) * stride - 1] += tmp;
		}
		GroupMemoryBarrierWithGroupSync();
	}

	radiance += lds_ScreenProbes_Radiance[first_lane + block_size - 1];
	GroupMemoryBarrierWithGroupSync();

	lds_ScreenProbes_Radiance[local_id] /= max(radiance, 1e-5f);
	GroupMemoryBarrierWithGroupSync();
}

#endif  // SCREEN_PROBES_HLSLI
