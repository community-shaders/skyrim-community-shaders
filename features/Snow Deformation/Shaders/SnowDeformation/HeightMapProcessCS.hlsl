cbuffer HeightProcessCB : register(b0)
{
	int2 ScrollDelta;
	uint ClearAll;
	uint padConeStep;

	float2 HeightWindowCenter;
	float HeightHalfExtent;
	float padSlopePerUnit;

	float2 TerrainWindowOrigin;
	float TerrainTexelSize;
	uint TerrainDim;

	float GhostDecay;
	float padObjectSnowDepth;
	float padRimStep;
	float padOverheadIgnore;

	float ShelterBandHeight;
	float padDiffuseLambda;
	float ShelterMaxHeight;
	float ShelterConeTan;

	uint HeightProcessKeeper;
	uint DeadGhostTiles;
	uint2 HeightProcessPad;
}

#define SHELTER_MELT 0.9
#define SHELTER_RING_TEXELS 10
#define SHELTER_CONE_MAX_TEXELS 160.0
#define SHELTER_CONE_LO 0.5
#define SHELTER_CONE_HI 0.85

cbuffer GhostCB : register(b2)
{
	row_major float4x4 GhostViewProj;
	float4 GhostCameraPosAdjust;
}

#include "SnowDeformation/SnowCommon.hlsli"
#include "SnowDeformation/SnowExclusions.hlsli"

Texture2D<float> InA : register(t0);
Texture2D<float> InB : register(t1);
Texture2D<float4> TerrainWindow : register(t2);
RWTexture2D<float> OutA : register(u0);
RWTexture2D<float> OutB : register(u1);
RWTexture2D<uint> WaterNearOut : register(u1);
RWTexture2D<float2> OutMask : register(u2);
RWTexture2D<uint> TileEmptyOut : register(u3);
Texture2D<uint> TileEmpty : register(t4);
StructuredBuffer<float4> ExclusionList : register(t5);
Texture2D<float2> RasterIn : register(t6);

float2 TexelWorldXY(uint2 p, uint2 dims)
{
	float texel = HeightHalfExtent * 2.0 / dims.x;
	return float2(
		HeightWindowCenter.x + (float(p.x) - dims.x * 0.5 + 0.5) * texel,
		HeightWindowCenter.y + (dims.y * 0.5 - float(p.y) - 0.5) * texel);
}

bool GhostInView(float2 worldXY, float worldZ)
{
	float3 rel = float3(worldXY, worldZ) - GhostCameraPosAdjust.xyz;
	float4 clip = mul(GhostViewProj, float4(rel, 1.0));
	return clip.w > 0.0 && all(abs(clip.xy) <= clip.w) && clip.z >= 0.0 && clip.z <= clip.w;
}

uint ScrollTexel(uint2 texel, uint2 dims);
groupshared uint scrollElevated;

[numthreads(8, 8, 1)] void ScrollCS(uint3 dtid
									: SV_DispatchThreadID, uint3 gid
									: SV_GroupID, uint gi
									: SV_GroupIndex) {
	uint2 dims;
	OutA.GetDimensions(dims.x, dims.y);
	if (gi == 0)
		scrollElevated = 0;
	GroupMemoryBarrierWithGroupSync();
	[branch] if (all(dtid.xy < dims))
		InterlockedOr(scrollElevated, ScrollTexel(dtid.xy, dims));
	GroupMemoryBarrierWithGroupSync();
	if (gi == 0)
		TileEmptyOut[gid.xy] = scrollElevated == 0 ? 1u : 0u;
}

uint ScrollTexel(uint2 texel, uint2 dims)
{
	uint3 dtid = uint3(texel, 0);
	float2 raster = RasterIn[dtid.xy];
	float rasterTop = raster.x;
	float rasterBottom = -raster.y;
	bool haveTop = rasterTop > -50000.0;
	bool haveBottom = rasterBottom < 50000.0;

	float top = -100000.0;
	float bottom = 100000.0;

	if (!ClearAll) {
		int2 src = int2(dtid.xy) + ScrollDelta;
		if (all(src >= 0) && all(src < int2(dims))) {
			top = InA[uint2(src)] - GhostDecay;
			bottom = InB[uint2(src)] + GhostDecay;
		}
	}

	[branch] if (haveTop)
	{
		if (top <= rasterTop || GhostInView(TexelWorldXY(dtid.xy, dims), top))
			top = rasterTop;
	}
	[branch] if (haveBottom)
	{
		if (bottom >= rasterBottom || GhostInView(TexelWorldXY(dtid.xy, dims), bottom))
			bottom = rasterBottom;
	}

	OutA[dtid.xy] = top;
	OutB[dtid.xy] = bottom;
	bool dead = DeadGhostTiles != 0 && bottom - top > ShelterBandHeight + 61.0;
	return (top > -50000.0 && bottom < 50000.0 && !dead) ? 1u : 0u;
}

float2 ShelterTap(int2 p, int2 dims, float terrain)
{
	p = clamp(p, int2(0, 0), dims - 1);
	float2 result = 0.0;
	float top = InA[p];
	[branch] if (top > -50000.0)
	{
		float bottom = InB[p];
		float clearance = bottom - terrain;
		float floating = smoothstep(20.0, 60.0, clearance) * smoothstep(40.0, 80.0, top - terrain);
		result = floating * (1.0 - smoothstep(float2(ShelterMaxHeight, ShelterBandHeight), float2(ShelterMaxHeight, ShelterBandHeight) + 100.0, clearance));
	}
	return result;
}

float ShelterEnclosure(int2 texel, int2 dims, float terrain)
{
	static const float2 kEnclosureTaps[32] = {
		float2(1.000, 0.000), float2(0.707, 0.707), float2(0.000, 1.000), float2(-0.707, 0.707),
		float2(-1.000, 0.000), float2(-0.707, -0.707), float2(0.000, -1.000), float2(0.707, -0.707),
		float2(0.736, 0.146), float2(0.417, 0.624), float2(-0.146, 0.736), float2(-0.624, 0.417),
		float2(-0.736, -0.146), float2(-0.417, -0.624), float2(0.146, -0.736), float2(0.624, -0.417),
		float2(0.462, 0.191), float2(0.191, 0.462), float2(-0.191, 0.462), float2(-0.462, 0.191),
		float2(-0.462, -0.191), float2(-0.191, -0.462), float2(0.191, -0.462), float2(0.462, -0.191),
		float2(0.208, 0.139), float2(0.049, 0.245), float2(-0.139, 0.208), float2(-0.245, 0.049),
		float2(-0.208, -0.139), float2(-0.049, -0.245), float2(0.139, -0.208), float2(0.245, -0.049)
	};
	float texelSize = HeightHalfExtent * 2.0 / dims.x;
	float radius = clamp(ShelterBandHeight * ShelterConeTan / texelSize, SHELTER_RING_TEXELS, SHELTER_CONE_MAX_TEXELS);

	float covered = ShelterTap(texel, dims, terrain).y * 2.0;
	[loop] for (uint tapI = 0; tapI < 32; tapI++)
		covered += ShelterTap(texel + int2(round(kEnclosureTaps[tapI] * radius)), dims, terrain).y;
	return smoothstep(SHELTER_CONE_LO, SHELTER_CONE_HI, covered / 34.0);
}

groupshared uint combineTileCount;
groupshared uint combineTileList[MAX_EXCLUSIONS];
groupshared uint combineElevated;

[numthreads(8, 8, 1)] void CombineCS(uint3 dtid
									 : SV_DispatchThreadID, uint3 gid
									 : SV_GroupID, uint gi
									 : SV_GroupIndex) {
	uint2 dims;
	OutMask.GetDimensions(dims.x, dims.y);

	if (gi == 0) {
		combineTileCount = 0;
		combineElevated = 0;
	}
	GroupMemoryBarrierWithGroupSync();
	if (gi < 25) {
		int2 tile = clamp(int2(gid.xy) + int2(gi % 5u, gi / 5u) - 2, 0, int2(dims / 8u) - 1);
		if (TileEmpty.Load(int3(tile, 0)) == 0)
			combineElevated = 1;
	}
	{
		float2 cornerA = TexelWorldXY(gid.xy * 8, dims);
		float2 cornerB = TexelWorldXY(gid.xy * 8 + 7, dims);
		float2 tileMin = min(cornerA, cornerB);
		float2 tileMax = max(cornerA, cornerB);
		for (uint testI = gi; testI < ExclusionCount; testI += 64) {
			float4 posRadius = ExclusionList[testI];
			float2 center = posRadius.xy;
			float reach = ExclusionReach(posRadius, ExclusionList[MAX_EXCLUSIONS + testI]);
			float2 closest = clamp(center, tileMin, tileMax);
			[branch] if (dot(closest - center, closest - center) <= reach * reach)
			{
				uint slot;
				InterlockedAdd(combineTileCount, 1, slot);
				if (slot < MAX_EXCLUSIONS)
					combineTileList[slot] = testI;
			}
		}
	}
	GroupMemoryBarrierWithGroupSync();

	if (any(dtid.xy >= dims))
		return;
	[branch] if (combineElevated == 0 && combineTileCount == 0)
	{
		OutMask[dtid.xy] = float2(0.0, 0.0);
		return;
	}

	float2 worldXY = TexelWorldXY(dtid.xy, dims);
	float terrain = TerrainWindowHeight(TerrainWindow, (worldXY - TerrainWindowOrigin) / TerrainTexelSize, TerrainDim);
	float suppress = 0.0;
	float melt = 0.0;

	{
		int2 texel = int2(dtid.xy);
		int2 dimsI = int2(dims);
		static const int2 kShelterRing[8] = {
			int2(SHELTER_RING_TEXELS, 0), int2(-SHELTER_RING_TEXELS, 0),
			int2(0, SHELTER_RING_TEXELS), int2(0, -SHELTER_RING_TEXELS),
			int2(7, 7), int2(7, -7), int2(-7, 7), int2(-7, -7)
		};
		static const int2 kShelterRingInner[8] = {
			int2(5, 0), int2(-5, 0), int2(0, 5), int2(0, -5),
			int2(4, 4), int2(4, -4), int2(-4, 4), int2(-4, -4)
		};
		float2 ringFrac = ShelterTap(texel, dimsI, terrain) * 2.0;
		[unroll] for (uint ringI = 0; ringI < 8; ringI++)
			ringFrac += ShelterTap(texel + kShelterRing[ringI], dimsI, terrain) +
			            ShelterTap(texel + kShelterRingInner[ringI], dimsI, terrain);
		ringFrac /= 18.0;
		float shelterFrac = ringFrac.x;
		[branch] if (ringFrac.y - ringFrac.x > 0.001)
			shelterFrac = lerp(ringFrac.x, ringFrac.y, ShelterEnclosure(texel, dimsI, terrain));
		melt = max(melt, SHELTER_MELT * saturate(shelterFrac));
	}

	{
		uint tileCount = min(combineTileCount, MAX_EXCLUSIONS);
		for (uint listI = 0; listI < tileCount; listI++) {
			ExclusionResult one = EvaluateExclusionAt(combineTileList[listI], worldXY, terrain);
			suppress = max(suppress, one.Suppress);
			melt = max(melt, one.Melt);
		}
	}

	OutMask[dtid.xy] = float2(suppress, melt);
}

float WaterWetTexel(int2 w)
{
	if (any(w < 0) || any(w >= int2(TerrainDim, TerrainDim)))
		return 0.0;
	float water = InA.Load(int3(w, 0));
	float h = TerrainWindow.Load(int3(w.x, (int)TerrainDim - 1 - w.y, 0)).x;
	return (water > -50000.0 && h > -50000.0 && h < water - 8.0) ? 1.0 : 0.0;
}

float WaterUnderTexel(int2 w)
{
	if (any(w < 0) || any(w >= int2(TerrainDim, TerrainDim)))
		return 0.0;
	float water = InA.Load(int3(w, 0));
	float h = TerrainWindow.Load(int3(w.x, (int)TerrainDim - 1 - w.y, 0)).x;
	return (water > -50000.0 && h > -50000.0 && h < water - 2.0) ? 1.0 : 0.0;
}

static const float kWaterNearMax = 3.0;

[numthreads(8, 8, 1)] void WaterCoverCS(uint3 dtid : SV_DispatchThreadID)
{
	if (any(dtid.xy >= TerrainDim))
		return;
	int2 w = (int2)dtid.xy;
	float water = InA.Load(int3(w, 0));
	[branch] if (WaterWetTexel(w) > 0.5)
	{
		float nb = WaterWetTexel(w + int2(1, 0)) + WaterWetTexel(w - int2(1, 0)) + WaterWetTexel(w + int2(0, 1)) + WaterWetTexel(w - int2(0, 1));
		[flatten] if (nb < 0.5)
			water = -100000.0;
	}
	OutA[dtid.xy] = water;
	uint nearSq = 10u;
	[loop] for (int j = -3; j <= 3; j++)
		[loop] for (int i = -3; i <= 3; i++)
			[flatten] if (WaterUnderTexel(w + int2(i, j)) > 0.5)
				nearSq = min(nearSq, (uint)(i * i + j * j));
	WaterNearOut[dtid.xy] = nearSq;
}

#ifdef RASTER_COMPARE
Texture2D<float4> CompareKept : register(t8);
Texture2D<float4> CompareFresh : register(t9);
RWByteAddressBuffer CompareCount : register(u4);

[numthreads(8, 8, 1)] void RasterCompareCS(uint3 dtid
										   : SV_DispatchThreadID) {
	uint2 dims;
	CompareKept.GetDimensions(dims.x, dims.y);
	if (any(dtid.xy >= dims))
		return;
	float4 kept = CompareKept[dtid.xy];
	float4 fresh = CompareFresh[dtid.xy];
	if (all(asuint(kept) == asuint(fresh)))
		return;
	uint prior;
	CompareCount.InterlockedAdd(0, 1u, prior);
	if (prior >= 8u)
		return;
	uint base = 16u + prior * 32u;
	CompareCount.Store4(base, uint4((dtid.y << 16) | dtid.x, asuint(kept.x), asuint(kept.y), asuint(fresh.x)));
	CompareCount.Store4(base + 16u, uint4(asuint(fresh.y), 0u, 0u, 0u));
}
#endif
