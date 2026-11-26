#include "PostProcessing/PhysicalLensFlare/reconstruction.hlsli"
Texture2DArray<float> Coarse : register(t0);
Texture2D<float2> BlueViolet : register(t1);
RWTexture2DArray<float> Fine : register(u0);
RWStructuredBuffer<uint> GhostTiles : register(u1);
groupshared uint GhostOccupied;
SamplerState LinearSampler : register(s0);

// Unpack the blue/violet target and mark occupied tiles at half resolution.
// Only the offline legacy reference reconstructs the retired coarse layer.
void ResolveBands(uint3 tid, uint3 group, uint lane, bool packed, bool includeCoarse)
{
	uint width, height, layers, coarseWidth, coarseHeight;
	Fine.GetDimensions(width, height, layers);
	if (lane == 0)
		GhostOccupied = 0;
	GroupMemoryBarrierWithGroupSync();
	if (tid.x < width && tid.y < height) {
		float3 broad = 0;
		if (includeCoarse) {
			Coarse.GetDimensions(coarseWidth, coarseHeight, layers);
			float2 uv = (tid.xy + .5) / float2(width, height);
			broad = SamplePhysicalMip(Coarse, LinearSampler, uv, float2(coarseWidth, coarseHeight), 0);
		}
		float3 color = 0;
		float2 blue = packed ? BlueViolet.Load(int3(tid.xy, 0)) : 0;
		[unroll] for (uint channel = 0; channel < 3; ++channel)
		{
			color[channel] = (packed && channel == 2 ? blue.x : Fine[uint3(tid.xy, channel)]) + broad[channel];
			if (packed && channel == 0)
				color[channel] += blue.y;
			if (includeCoarse || (packed && channel != 1))
				Fine[uint3(tid.xy, channel)] = color[channel];
		}
		if (any(color != 0))
			InterlockedOr(GhostOccupied, 1);
	}
	GroupMemoryBarrierWithGroupSync();
	if (lane == 0) {
		uint index = group.y * ((width + 7) / 8) + group.x;
		GhostTiles[index] = (GhostTiles[index] & 0x1ffff) | (GhostOccupied << 17);
	}
}

[numthreads(8, 8, 1)] void ResolveCoarse(uint3 tid : SV_DispatchThreadID, uint3 group : SV_GroupID, uint lane : SV_GroupIndex) { ResolveBands(tid, group, lane, false, true); }
	[numthreads(8, 8, 1)] void ResolvePacked(uint3 tid : SV_DispatchThreadID, uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
	ResolveBands(tid, group, lane, true, false);
}

// Offline references retain the retired coarse layer and MRT scalar path.
[numthreads(8, 8, 1)] void ResolvePackedReference(uint3 tid : SV_DispatchThreadID, uint3 group : SV_GroupID, uint lane : SV_GroupIndex) { ResolveBands(tid, group, lane, true, true); }
	[numthreads(8, 8, 1)] void ResolveScalar(uint3 tid : SV_DispatchThreadID, uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
	ResolveBands(tid, group, lane, false, false);
}
