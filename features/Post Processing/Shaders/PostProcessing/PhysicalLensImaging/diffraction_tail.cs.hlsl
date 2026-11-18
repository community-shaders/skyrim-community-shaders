#include "PostProcessing/PhysicalLensImaging/diffraction_parameters.hlsli"
#include "PostProcessing/PhysicalLensImaging/diffraction_sampling.hlsli"
StructuredBuffer<float4> TailSources : register(t0);
StructuredBuffer<uint> TailTiles : register(t1);
Texture2DArray<float4> TailPSF : register(t2);
StructuredBuffer<uint> TailWorkTiles : register(t3);
ByteAddressBuffer TailDispatch : register(t4);
RWTexture2DArray<float> TailRadiance : register(u0);
SamplerState LinearClamp : register(s0);
// A broad incoherent source suppresses fine diffraction structure. Evaluate
// its integrated field on a shared tile lattice; point and thin sources retain
// per-pixel evaluation. The three otherwise redundant subtile groups exit.
groupshared uint TileIndex, SourceMask, SampleStep;
groupshared float Samples[81];
float TailAt(float2 pixel, uint band, uint mask)
{
	float value = 0;
	[loop] while (mask != 0)
	{
		uint source = firstbitlow(mask);
		mask &= mask - 1;
		float4 light = TailSources[source * 7 + band * 2];
		float2 variance = TailSources[source * 7 + band * 2 + 1].xy;
		value += light.z * SampleDiffractionTail(TailPSF, LinearClamp, pixel - light.xy, variance, WaveSampling[band].xy, band);
	}
	return value;
}
// Broad fields need only a 3x3 lattice per tile. Pack four tiles so the
// 36 evaluations occupy both warps instead of leaving most lanes idle.
// Sampling positions and bilinear reconstruction match the unbatched path.
groupshared uint PackedTiles[4], PackedMasks[4];
groupshared float PackedSamples[36];
void PackedWaveTail(uint3 group, uint lane)
{
	uint first = (group.x + group.z * 65535) * 4;
	uint count = TailDispatch.Load(12);
	uint columns = (WaveImageSize.x + 15) / 16;
	if (lane < 4) {
		uint tile = first + lane < count ? TailWorkTiles[first + lane] : 0;
		PackedTiles[lane] = tile;
		PackedMasks[lane] = first + lane < count ? TailTiles[tile] & 0xffff : 0;
	}
	GroupMemoryBarrierWithGroupSync();
	if (lane < 36) {
		uint tile = lane / 9, sample = lane % 9;
		uint2 base = uint2(PackedTiles[tile] % columns, PackedTiles[tile] / columns) * 16;
		PackedSamples[lane] = TailAt(float2(base) + float2(sample % 3, sample / 3) * 8 + .5, group.y, PackedMasks[tile]);
	}
	GroupMemoryBarrierWithGroupSync();
	for (uint p = lane; p < 1024; p += 64) {
		uint tile = p / 256;
		uint2 local = uint2(p % 16, (p % 256) / 16), cell = local / 8;
		float2 f = float2(local % 8) / 8;
		uint a = tile * 9 + cell.y * 3 + cell.x;
		float value = lerp(lerp(PackedSamples[a], PackedSamples[a + 1], f.x), lerp(PackedSamples[a + 3], PackedSamples[a + 4], f.x), f.y);
		uint2 pixel = uint2(PackedTiles[tile] % columns, PackedTiles[tile] / columns) * 16 + local;
		if (PackedMasks[tile] != 0 && all(pixel < WaveImageSize))
			TailRadiance[uint3(pixel, group.y)] = value;
	}
}
[numthreads(8, 8, 1)] void BuildWaveTail(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID, uint lane : SV_GroupIndex) {
	if (TailDispatch.Load(4) == 3) {
		PackedWaveTail(group, lane);
		return;
	}
	uint band = group.y / 4, subTile = group.y % 4;
	uint columns = (WaveImageSize.x + 15) / 16;
	if (lane == 0) {
		uint work = group.x + group.z * 65535;
		TileIndex = work < TailDispatch.Load(12) ? TailWorkTiles[work] : 0;
		SourceMask = work < TailDispatch.Load(12) ? TailTiles[TileIndex] & 0xffff : 0;
		float minimumVariance = 1e20;
		uint mask = SourceMask;
		while (mask != 0) {
			uint source = firstbitlow(mask);
			mask &= mask - 1;
			float2 variance = TailSources[source * 7 + band * 2 + 1].xy;
			minimumVariance = min(minimumVariance, min(variance.x, variance.y));
		}
		// At most one sample interval per quarter source sigma. Every light
		// in the tile must qualify, so a nearby point source keeps sharp rays.
		SampleStep = minimumVariance >= 1024 ? 8 : minimumVariance >= 256 ? 4 :
		                                       minimumVariance >= 64      ? 2 :
		                                                                    1;
	}
	GroupMemoryBarrierWithGroupSync();
	if (SourceMask == 0)
		return;
	uint2 base = uint2(TileIndex % columns, TileIndex / columns) * 16;
	if (SampleStep == 1) {
		uint2 pixel = base + uint2(subTile & 1, subTile >> 1) * 8 + thread.xy;
		if (any(pixel >= WaveImageSize))
			return;
		TailRadiance[uint3(pixel, band)] = TailAt(float2(pixel) + .5, band, SourceMask);
	} else {
		if (subTile != 0)
			return;
		// Shared border samples agree across adjacent tiles. Keep the outer
		// sample even at an odd-sized image edge; only writes are clipped.
		uint side = 16 / SampleStep + 1;
		for (uint i = lane; i < side * side; i += 64) Samples[i] = TailAt(float2(base) + float2(i % side, i / side) * SampleStep + .5, band, SourceMask);
		GroupMemoryBarrierWithGroupSync();
		[unroll] for (uint quadrant = 0; quadrant < 4; ++quadrant)
		{
			uint2 local = thread.xy + uint2(quadrant & 1, quadrant >> 1) * 8;
			uint2 cell = local / SampleStep;
			float2 f = float2(local % SampleStep) / SampleStep;
			uint a = cell.y * side + cell.x;
			float value = lerp(lerp(Samples[a], Samples[a + 1], f.x), lerp(Samples[a + side], Samples[a + side + 1], f.x), f.y);
			uint2 pixel = base + local;
			if (all(pixel < WaveImageSize))
				TailRadiance[uint3(pixel, band)] = value;
		}
	}
}
