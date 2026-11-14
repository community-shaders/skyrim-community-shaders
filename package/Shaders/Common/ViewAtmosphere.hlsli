#ifndef VIEW_ATMOSPHERE_HLSLI
#define VIEW_ATMOSPHERE_HLSLI

#include "Common/Game.hlsli"
#include "Common/SharedData.hlsli"

namespace ViewAtmosphere
{
	struct Grid
	{
		uint4 size;
		uint4 offset;
		float4 depth;
	};
	struct Parameters
	{
		Grid grids[3];
		uint4 atlas;
		uint4 flags;
		float4 history;
		row_major float4x4 clipToWorld;
	};
	Texture3D<float4> Luminance : register(t125);
	Texture3D<float4> Transmittance : register(t126);
	StructuredBuffer<Parameters> Data : register(t127);

	bool Enabled()
	{
		uint count, stride;
		Data.GetDimensions(count, stride);
		return count > 0u && SharedData::PostWaterComposite != 0u && Data[0].flags.x != 0u && Data[0].flags.w == 0u;
	}

	bool ReflectionEnabled()
	{
		uint count, stride;
		Data.GetDimensions(count, stride);
		return count > 0u && Data[0].flags.x != 0u && Data[0].flags.w == 1u;
	}

	float ReflectionDistance(float slice)
	{
		// 10 m logarithmic offset resolves near-ground fog while retaining the 40 km AP range.
		return Data[0].grids[0].depth.z * (exp2(slice * Data[0].grids[0].depth.w / 31.0) - 1.0);
	}

	float Distance(Grid grid, float slice)
	{
		return grid.depth.x + grid.depth.z * (exp2(slice * grid.depth.w / grid.size.z) - 1.0);
	}

	float Slice(Grid grid, float distance)
	{
		return grid.size.z * log2(1.0 + max(distance - grid.depth.x, 0.0) / grid.depth.z) / grid.depth.w;
	}

	float SourceSlice(Grid grid, float distance)
	{
		const float ratio = exp2(grid.depth.w / grid.size.z);
		return grid.size.z * log2((1.0 + max(distance - grid.depth.x, 0.0) / grid.depth.z) * 2.0 / (1.0 + ratio)) / grid.depth.w;
	}

	float3 AtlasUv(Grid grid, float2 uv, float slice)
	{
		const float2 xy = clamp(uv * grid.size.xy, 0.5, float2(grid.size.xy) - 0.5) + grid.offset.xy;
		return float3(xy, clamp(slice + 0.5, 0.5, float(grid.size.z) + 0.5)) / Data[0].atlas.xyz;
	}

	float3 Position(Grid grid, uint3 coord, float3 offset)
	{
		const float2 uv = (coord.xy + offset.xy) / grid.size.xy;
		float4 ray = mul(Data[0].clipToWorld, float4(uv * float2(2, -2) + float2(-1, 1), 0.5, 1));
		return normalize(ray.xyz / ray.w) * lerp(Distance(grid, coord.z), Distance(grid, coord.z + 1u), offset.z);
	}

	struct Medium
	{
		float3 luminance;
		float3 transmittance;
	};

	Medium Identity()
	{
		Medium result;
		result.luminance = 0.0;
		result.transmittance = 1.0;
		return result;
	}

	float3 SampleReflectionTexture(Texture3D<float4> source, float3 uvw, SamplerState samp)
	{
		// Material samplers need not wrap. Explicitly join the azimuth seam.
		uvw.x = frac(uvw.x);
		if (uvw.x < 0.5 / 32.0 || uvw.x > 31.5 / 32.0) {
			const float weight = frac(uvw.x * 32.0 + 0.5);
			return lerp(source.SampleLevel(samp, float3(31.5 / 32.0, uvw.yz), 0).rgb,
				source.SampleLevel(samp, float3(0.5 / 32.0, uvw.yz), 0).rgb, weight);
		}
		return source.SampleLevel(samp, uvw, 0).rgb;
	}

	Medium SampleReflection(float2 uv, float distance, SamplerState samp)
	{
		const float end = clamp(distance, 0.0, Data[0].grids[0].depth.y);
		const float slice = min(floor(31.0 * log2(1.0 + end / Data[0].grids[0].depth.z) / Data[0].grids[0].depth.w), 30.0);
		const float fraction = saturate((end - ReflectionDistance(slice)) / max(ReflectionDistance(slice + 1.0) - ReflectionDistance(slice), 1e-6));
		const float3 nearUv = float3(uv, (slice + 0.5) / 32.0);
		const float3 farUv = float3(uv, (slice + 1.5) / 32.0);
		const float3 nearL = SampleReflectionTexture(Luminance, nearUv, samp);
		const float3 farL = SampleReflectionTexture(Luminance, farUv, samp);
		const float3 nearT = SampleReflectionTexture(Transmittance, nearUv, samp);
		const float3 farT = SampleReflectionTexture(Transmittance, farUv, samp);
		const float3 segmentT = saturate(farT / max(nearT, 1e-20));
		const float3 partialT = pow(max(segmentT, 1e-20), fraction);
		const float3 weight = float3(
			segmentT.x > 0.999 ? fraction : (1.0 - partialT.x) / max(1.0 - segmentT.x, 1e-20),
			segmentT.y > 0.999 ? fraction : (1.0 - partialT.y) / max(1.0 - segmentT.y, 1e-20),
			segmentT.z > 0.999 ? fraction : (1.0 - partialT.z) / max(1.0 - segmentT.z, 1e-20));
		Medium result;
		result.luminance = lerp(nearL, farL, weight);
		result.transmittance = nearT * partialT;
		return result;
	}

	Medium Sample(float2 uv, float distance, SamplerState samp)
	{
		Medium result = Identity();
		[unroll] for (uint level = 0; level < 3; ++level)
		{
			const Grid grid = Data[0].grids[level];
			if (distance <= grid.depth.x)
				break;
			if (distance >= grid.depth.y) {
				const float3 uvw = AtlasUv(grid, uv, grid.size.z);
				result.luminance += result.transmittance * Luminance.SampleLevel(samp, uvw, 0).rgb;
				result.transmittance *= Transmittance.SampleLevel(samp, uvw, 0).rgb;
				continue;
			}
			const float end = min(distance, grid.depth.y);
			const float slice = min(floor(Slice(grid, end)), float(grid.size.z - 1u));
			const float nearDistance = Distance(grid, slice);
			const float farDistance = Distance(grid, slice + 1.0);
			const float fraction = saturate((end - nearDistance) / max(farDistance - nearDistance, 1e-6));
			const float3 nearUv = AtlasUv(grid, uv, slice);
			const float3 farUv = AtlasUv(grid, uv, slice + 1.0);
			const float3 nearL = Luminance.SampleLevel(samp, nearUv, 0).rgb;
			const float3 farL = Luminance.SampleLevel(samp, farUv, 0).rgb;
			const float3 nearT = Transmittance.SampleLevel(samp, nearUv, 0).rgb;
			const float3 farT = Transmittance.SampleLevel(samp, farUv, 0).rgb;
			const float3 segmentT = saturate(farT / max(nearT, 1e-20));
			const float3 partialT = pow(max(segmentT, 1e-20), fraction);
			const float3 weight = float3(
				segmentT.x > 0.999 ? fraction : (1.0 - partialT.x) / max(1.0 - segmentT.x, 1e-20),
				segmentT.y > 0.999 ? fraction : (1.0 - partialT.y) / max(1.0 - segmentT.y, 1e-20),
				segmentT.z > 0.999 ? fraction : (1.0 - partialT.z) / max(1.0 - segmentT.z, 1e-20));
			const float3 light = lerp(nearL, farL, weight);
			const float3 tr = nearT * partialT;
			result.luminance += result.transmittance * light;
			result.transmittance *= tr;
		}
		return result;
	}
}
#endif
