#include "SnowDeformation/SnowCommon.hlsli"
#include "SnowDeformation/SnowExclusions.hlsli"

Texture2D<float4> TerrainWindow : register(t0);
RWTexture2D<float2> OutField : register(u0);

cbuffer ExclusionFieldCB : register(b0)
{
	float2 FieldCenter;
	float padHalfExtent;
	float FieldTexelSize;

	float2 TerrainWindowOrigin;
	float TerrainTexelSize;
	uint TerrainDim;
}

#define TILE_DIM 16

groupshared uint TileCount;
groupshared uint TileList[MAX_EXCLUSIONS];

float2 FieldTexelWorldXY(uint2 texel, float2 dims)
{
	return FieldCenter + (float2(texel) - dims * 0.5 + 0.5) * FieldTexelSize;
}

[numthreads(TILE_DIM, TILE_DIM, 1)]
void main(uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID, uint gindex : SV_GroupIndex)
{
	float2 dims;
	OutField.GetDimensions(dims.x, dims.y);

	if (gindex == 0)
		TileCount = 0;
	GroupMemoryBarrierWithGroupSync();

	{
		float2 tileMin = FieldTexelWorldXY(gid.xy * TILE_DIM, dims) - FieldTexelSize * 0.5;
		float2 tileMax = tileMin + FieldTexelSize * TILE_DIM;
		for (uint testI = gindex; testI < ExclusionCount; testI += TILE_DIM * TILE_DIM) {
			if (ExclusionDirExtType[testI].w > 2.5)
				continue;
			float3 center = ExclusionPosRadius[testI].xyz;
			float reach = ExclusionPosRadius[testI].w * 1.4 + max(ExclusionDirExtType[testI].z, 0.0);
			float2 closest = clamp(center.xy, tileMin, tileMax);
			[branch] if (dot(closest - center.xy, closest - center.xy) <= reach * reach)
			{
				uint slot;
				InterlockedAdd(TileCount, 1, slot);
				if (slot < MAX_EXCLUSIONS)
					TileList[slot] = testI;
			}
		}
	}
	GroupMemoryBarrierWithGroupSync();

	if (any(float2(dtid.xy) >= dims))
		return;

	float suppress = 0.0;
	float melt = 0.0;
	uint tileCount = min(TileCount, MAX_EXCLUSIONS);
	[branch] if (tileCount > 0)
	{
		float2 worldXY = FieldTexelWorldXY(dtid.xy, dims);
		float terrain = TerrainWindowHeight(TerrainWindow, (worldXY - TerrainWindowOrigin) / TerrainTexelSize, TerrainDim);
		for (uint listI = 0; listI < tileCount; listI++) {
			ExclusionResult one = EvaluateExclusionAt(TileList[listI], worldXY, terrain);
			suppress = max(suppress, one.Suppress);
			melt = max(melt, one.Melt);
		}
	}

	OutField[dtid.xy] = float2(suppress, melt);
}
