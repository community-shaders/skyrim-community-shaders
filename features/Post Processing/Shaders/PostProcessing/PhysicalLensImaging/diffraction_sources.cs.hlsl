#include "PostProcessing/PhysicalLensFlare/exposure.hlsli"
#include "PostProcessing/PhysicalLensFlare/optics.hlsli"
StructuredBuffer<float4> SourceWaveKernel : register(t3);
#include "PostProcessing/PhysicalLensImaging/diffraction_domain.hlsli"
#include "PostProcessing/PhysicalLensImaging/diffraction_parameters.hlsli"
#include "PostProcessing/PhysicalLensImaging/regions.hlsli"
Texture2DArray<float> WaveMaxDensity : register(t4);
Texture2D<float4> WaveTileScene : register(t0);
Texture2DArray<float4> WaveTileMapping : register(t1);
StructuredBuffer<float4> WaveTileRegions : register(t3);

StructuredBuffer<Source> WaveInputSources : register(t1);
Texture2DArray<float4> WaveMapping : register(t0);
RWStructuredBuffer<float4> WaveSourceOutput : register(u0);
StructuredBuffer<float4> WaveSourceInput : register(t2);
RWStructuredBuffer<uint> WaveTileOutput : register(u1);
RWByteAddressBuffer WaveDispatch : register(u2);
RWStructuredBuffer<uint> WaveWorkTiles : register(u3);

// Bilinear table evaluation and its exact local Jacobian. Extrapolate the
// outer cell for sources just beyond the sensor, whose wings may remain visible.
float3 MappingJacobian(float2 uv, uint band, out float2 dx, out float2 dy)
{
	uint w, h, layers;
	WaveMapping.GetDimensions(w, h, layers);
	float2 p = uv * float2(w - 1, h - 1);
	int2 cell = clamp(int2(floor(p)), 0, int2(w - 2, h - 2));
	float2 f = p - cell;
	float3 a = WaveMapping.Load(int4(cell, band, 0)).xyz;
	float3 b = WaveMapping.Load(int4(cell + int2(1, 0), band, 0)).xyz;
	float3 c = WaveMapping.Load(int4(cell + int2(0, 1), band, 0)).xyz;
	float3 d = WaveMapping.Load(int4(cell + 1, band, 0)).xyz;
	dx = lerp(b.xy - a.xy, d.xy - c.xy, f.y) * (w - 1);
	dy = lerp(c.xy - a.xy, d.xy - b.xy, f.x) * (h - 1);
	return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

[numthreads(16, 1, 1)] void BuildWaveSources(uint sourceIndex : SV_GroupIndex) {
	Source source = (Source)0;
	if (sourceIndex < MaxSources)
		source = WaveInputSources[sourceIndex];
	// A few moments cannot reconstruct a sky-sized radiance field. Its weak
	// diffraction is effectively local; preserve that image instead of replacing
	// its energy by a rectangular emitter. Fade the approximation continuously.
	float compact = 1 - smoothstep(.015, .06, sqrt(max(source.Position.z, source.Position.w)));
	float4 bounds = float4(WaveImageSize, 0, 0);
	[unroll] for (uint band = 0; band < 3; ++band)
	{
		float2 uv = source.Position.xy;
		float2 inverseX = float2(1, 0), inverseY = float2(0, 1);
		float determinant = 1, transmission = 1;
		bool valid = source.Color[band] > 0 && compact > 0 && (WaveStarburst.w == 0 || WaveStarburst.y > 0);
		[branch] if (valid && DiffractionEnabled > 1)
		{
			[loop] for (uint iteration = 0; iteration < 5; ++iteration)
			{
				float2 dx, dy;
				float3 mapping = MappingJacobian(uv, band, dx, dy);
				determinant = dx.x * dy.y - dx.y * dy.x;
				if (abs(determinant) < 1e-6 || mapping.z <= 0) {
					valid = false;
					break;
				}
				inverseX = float2(dy.y, -dy.x) / determinant;
				inverseY = float2(-dx.y, dx.x) / determinant;
				float2 error = mapping.xy - source.Position.xy;
				if (iteration == 4) {
					valid = length(error * WaveImageSize) < .25;
					transmission = max(mapping.z, 0);
				} else
					uv -= float2(dot(inverseX, error), dot(inverseY, error));
			}
		}
		float2 variance = 0;
		float energy = 0;
		if (valid) {
			variance = float2(dot(inverseX * inverseX, source.Position.zw) + 2 * inverseX.x * inverseX.y * source.Color.w,
				dot(inverseY * inverseY, source.Position.zw) + 2 * inverseY.x * inverseY.y * source.Color.w);
			variance = max(variance * float2(WaveImageSize) * WaveImageSize, 0);
			energy = source.Color[band] * float(WaveImageSize.x) * WaveImageSize.y * transmission / abs(determinant) * compact;
			if (WaveStarburst.w > 0) {
				// The cached RGB spectrum is normalized after integration and gamut
				// clipping. Throughput N^-2 plus the PSF's N^2 area gives N^-4
				// irradiance (Hullin eq. 4), without subtracting the direct image.
				float gain = WaveStarburst.y * pow(5.6 / WaveStarburst.z, 2);
				energy *= gain * SourceWaveKernel[band * 4 + 3].z;
			}
			// Conservative sampled r^-2 envelope; omit only contributions below
			// 1e-5 exposed linear radiance, rather than a fixed small PSF rectangle.
			float visibleRadius = sqrt(max(energy * VisibilityExposure() * SourceWaveKernel[band * 4 + 3].w / 1e-5, 0));
			float2 extent = max(sqrt(3 * variance), .5) + min(8250 * WaveSampling[band].xy, max(32 * WaveSampling[band].xy, visibleRadius));
			bounds.xy = min(bounds.xy, uv * WaveImageSize - extent);
			bounds.zw = max(bounds.zw, uv * WaveImageSize + extent);
		}
		WaveSourceOutput[sourceIndex * 7 + band * 2] = float4(uv * WaveImageSize, energy, valid ? compact : 0);
		WaveSourceOutput[sourceIndex * 7 + band * 2 + 1] = float4(variance, 0, 0);
	}
	WaveSourceOutput[sourceIndex * 7 + 6] = bounds;
}

// Small per-tile masks avoid a full-resolution intermediate and avoid scanning
// all sources in every composite pixel. Each set bit denotes one finite emitter.
bool CoreTileActive(float2 lo, float2 hi)
{
	if (WaveStarburst.w > 0 && WaveStarburst.y == 0)
		return false;
	float2 uvLo = (lo - 2) / WaveImageSize, uvHi = (hi + 2) / WaveImageSize;
	if (DiffractionEnabled > 1) {
		uint w, h, layers;
		WaveTileMapping.GetDimensions(w, h, layers);
		int2 first = clamp(int2(floor(uvLo * float2(w - 1, h - 1))), 0, int2(w - 2, h - 2));
		int2 last = clamp(int2(ceil(uvHi * float2(w - 1, h - 1))), 1, int2(w - 1, h - 1));
		uvLo = 1;
		uvHi = 0;
		[loop] for (int y = first.y; y <= last.y; ++y)[loop] for (int x = first.x; x <= last.x; ++x)
		{
			[unroll] for (uint band = 0; band < 3; ++band)
			{
				float2 uv = WaveTileMapping.Load(int4(x, y, band, 0)).xy;
				uvLo = min(uvLo, uv);
				uvHi = max(uvHi, uv);
			}
		}
		// Bilinear values lie in the convex hull of their cell's corners.
		// Expand by a bound on the two pixel derivatives used by the core.
		float2 margin = (uvHi - uvLo) * 2 * dot(float2(w - 1, h - 1) / WaveImageSize, 1);
		uvLo -= margin;
		uvHi += margin;
	}
	uint2 firstRegion = WaveRegionTile(uvLo, WaveImageSize);
	uint2 lastRegion = WaveRegionTile(uvHi, WaveImageSize);
	bool active = false;
	[loop] for (uint y = firstRegion.y; y <= lastRegion.y; ++y)[loop] for (uint x = firstRegion.x; x <= lastRegion.x; ++x)
	{
		float4 region = WaveTileRegions[y * 64 + x];
		[unroll] for (uint layer = 0; layer < 2; ++layer)
		{
			uint owner = uint(region[2 + layer]);
			if (owner > 0) {
				[unroll] for (uint band = 0; band < 3; ++band) active = active || WaveSourceInput[(owner - 1) * 7 + band * 2].w > 0;
			}
		}
	}
	return active;
}

// All active sources must be broad before packing four work tiles into a
// group. One narrow source keeps the existing per-tile path for the frame.
groupshared uint PackBroadWaveTiles;
[numthreads(8, 8, 1)] void BuildWaveTiles(uint3 id : SV_DispatchThreadID, uint lane : SV_GroupIndex) {
	if (lane == 0) {
		PackBroadWaveTiles = 1;
		[loop] for (uint source = 0; source < MaxSources; ++source)[unroll] for (uint band = 0; band < 3; ++band)
		{
			float4 light = WaveSourceInput[source * 7 + band * 2];
			if (light.z > 0 && any(WaveSourceInput[source * 7 + band * 2 + 1].xy < 1024))
				PackBroadWaveTiles = 0;
		}
	}
	GroupMemoryBarrierWithGroupSync();
	uint2 size = (WaveImageSize + 15) / 16;
	if (any(id.xy >= size))
		return;
	if (all(id.xy == 0))
		WaveDispatch.Store(4, PackBroadWaveTiles != 0 ? 3 : 12);
	float2 lo = id.xy * 16, hi = lo + 16;
	float background = 1e30;
	[unroll] for (uint corner = 0; corner < 5; ++corner)
	{
		uint2 p = corner == 4 ? uint2(lo + 8) : uint2(lo) + uint2(corner & 1, corner >> 1) * 15;
		float3 radiance = WaveTileScene.Load(int3(min(p, WaveImageSize - 1), 0)).rgb;
		background = min(background, dot(max(radiance, 0), float3(.2126, .7152, .0722)));
	}
	// The accumulated omitted signal stays below 0.2% of the local background;
	// dark backgrounds retain an absolute floor of 1e-5 per emitter.
	float tolerance = max(1e-5, .002 * background * VisibilityExposure() / max(MaxSources, 1));
	uint mask = CoreTileActive(lo, hi) ? (1u << 16) : 0;
	for (uint source = 0; source < MaxSources; ++source) {
		float4 bounds = WaveSourceInput[source * 7 + 6];
		if (all(bounds.xy < hi) && all(bounds.zw > lo)) {
			bool visible = false;
			[unroll] for (uint band = 0; band < 3; ++band)
			{
				float4 light = WaveSourceInput[source * 7 + band * 2];
				float2 variance = WaveSourceInput[source * 7 + band * 2 + 1].xy;
				float2 radius = max(2 * sqrt(variance), .5);
				float2 a = WaveDomainInverse((lo - light.xy - radius) / WaveSampling[band].xy) + 255;
				float2 b = WaveDomainInverse((hi - light.xy + radius) / WaveSampling[band].xy) + 257;
				float span = max(b.x - a.x, b.y - a.y) / 8;
				uint level = (uint)clamp(ceil(log2(max(span, 1))), 0, 6);
				int size = 64 >> level;
				int2 first = clamp(int2(floor(a / (8 << level))), 0, size - 1);
				int2 last = clamp(int2(floor(b / (8 << level))), 0, size - 1);
				float peak = max(max(WaveMaxDensity.Load(int4(first, band, level)), WaveMaxDensity.Load(int4(last, band, level))),
					max(WaveMaxDensity.Load(int4(first.x, last.y, band, level)), WaveMaxDensity.Load(int4(last.x, first.y, band, level))));
				// Allow for the small Jacobian variation inside a log-spaced cell.
				visible = visible || 1.25 * peak * light.z * VisibilityExposure() / max(WaveSampling[band].x * WaveSampling[band].y, 1e-20) >= tolerance;
			}
			if (visible)
				mask |= 1u << source;
		}
	}
	WaveTileOutput[id.y * size.x + id.x] = mask;
	if ((mask & 0xffff) != 0) {
		uint index, ignored;
		WaveDispatch.InterlockedAdd(12, 1, index);
		uint work = PackBroadWaveTiles != 0 ? index / 4 : index;
		WaveDispatch.InterlockedMax(0, min(work + 1, 65535), ignored);
		WaveDispatch.InterlockedMax(8, work / 65535 + 1, ignored);
		WaveWorkTiles[index] = id.y * size.x + id.x;
	}
}
