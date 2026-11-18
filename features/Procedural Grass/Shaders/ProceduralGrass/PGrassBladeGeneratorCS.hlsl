#include "Common/FastMath.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/Math.hlsli"
#include "Common/Random.hlsli"

#define PSHADER
#include "Common/SharedData.hlsli"
#undef PSHADER

#ifndef CSHADER
#	define CSHADER
#endif

// Before the grass common header, which builds the camera-snapped probe frame on it.
#if defined(SKYLIGHTING) && defined(HIGH_LOD)
#	define SKYLIGHTING_PROBE_REGISTER t50
#	include "Skylighting/Skylighting.hlsli"
#endif

#include "ProceduralGrass/PGrassCommon.hlsli"

#if defined(PGRASS_CACHED_COLLISION)
#	define GRASS_COLLISION_CBUFFER_REGISTER b11
#	if defined(MID_LOD)
#		define GRASS_COLLISION_CURRENT_ONLY
#	endif
#	include "GrassCollision/GrassCollision.hlsli"
#endif

#if defined(HIGH_LOD) && defined(TERRAIN_SHADOWS)
#	include "TerrainShadows/TerrainShadows.hlsli"
#endif

#if defined(HIGH_LOD) && defined(CLOUD_SHADOWS)
#	include "CloudShadows/CloudShadows.hlsli"
#endif

#define FRAMEBUFFER

// A second list at the buffer's tail holds simpler near-tier blades or Far's double-blade handoff fill.
#if defined(HIGH_GEOMETRY_LOD) || defined(MID_OUTER_GEOMETRY) || defined(LOW_OUTER_GEOMETRY)
#	define PGRASS_OUTER_LIST
#endif

#if defined(__INTELLISENSE__)
#	define THREADGROUP_SIZE 8
#	define DENSITY 192
#	define PATCH_BLADE_COUNT 4
#	define SLOPE_EXTRA_BLADES 1
#endif

// Supply defaults for editor parsing and validation builds.
#if !defined(PATCH_BLADE_COUNT)
#	define PATCH_BLADE_COUNT 1
#endif
#if !defined(SLOPE_EXTRA_BLADES)
#	define SLOPE_EXTRA_BLADES 0
#endif

// Match the constant-buffer array to the renderer's quadrant capacity (PGrassCommon.h tier caps).
#if !defined(QUADRANT_DATA_SIZE)
#	if defined(HIGH_LOD)
#		define QUADRANT_DATA_SIZE 25
#	elif defined(MID_LOD)
#		define QUADRANT_DATA_SIZE 81
#	else
#		define QUADRANT_DATA_SIZE 169
#	endif
#endif

static const int TG_DIM_X = THREADGROUP_SIZE;
static const int TG_DIM_Y = 1;
static const uint BLADES_PER_ROW = DENSITY;

static const uint PATCHES_PER_QUADRANT = BLADES_PER_ROW * BLADES_PER_ROW / 4;
static const float BLADE_TO_WORLD = 2048.0f / BLADES_PER_ROW;

static const float UINT_TO_FLOAT = 1.0f / 4294967296.0f;

uint2 BaseGridPosition(uint2 patchPos, uint bladeIndex)
{
	uint patchHash = Random::iqint3(patchPos);
	uint bladeIndexRandomiser = (patchHash >> 16) & 3u;
	uint randomBladeIndex = bladeIndex ^ bladeIndexRandomiser;
	return patchPos * 2u + uint2(randomBladeIndex >> 1u, randomBladeIndex & 1u);
}

float2 BaseQuadrantPosition(uint2 pos, uint3 hash)
{
	float2 jitter = float2(hash.xy) * UINT_TO_FLOAT;
	return (float2(pos) + jitter) * BLADE_TO_WORLD;
}

static const uint WORK_HAS_LAND = 1u << 16u;

struct QuadrantData
{
	float2 quadWorldPos;
	uint quadrantHash;
	uint flags;
};

cbuffer QuadrantData : register(b7)
{
	float4 lodFadeIn;  // x: fade-in start, y: inverse range, z: unused, w: fade-out endpoint
	float4 lodFadeOut;
	QuadrantData data[QUADRANT_DATA_SIZE];
}

RWStructuredBuffer<Blade> BladeOutput : register(u0);
RWByteAddressBuffer IndirectArgs : register(u1);

// Far's handoff fill stages its candidates in rounds, so its group-shared staging does not limit occupancy.
#define FAR_STAGED_CANDIDATES 4
static const uint MAX_BLADES_PER_THREAD = 1u + (SLOPE_EXTRA_BLADES + PATCH_BLADE_COUNT - 1u) / PATCH_BLADE_COUNT;

// Adjacent lanes occupy adjacent slots for each emission, reducing shared-memory bank conflicts.
groupshared Blade GroupBlades[THREADGROUP_SIZE * MAX_BLADES_PER_THREAD];
#if defined(PGRASS_OUTER_LIST)
groupshared uint GroupBladeOuter[THREADGROUP_SIZE * MAX_BLADES_PER_THREAD];
groupshared uint GroupOuterCount;
#endif
groupshared uint GroupInnerCount;
groupshared uint2 GroupOutputBase;

Texture2D<float> TerrainHeightTexture : register(t0);
SamplerState LinearSampler : register(s0);

static const uint QUADRANT_GRASS_PITCH = 17;
static const float QUADRANT_GRASS_SPACING = 2048.0f / 16.0f;

StructuredBuffer<float> QuadrantHeights : register(t3);

// Return bilinear LAND height and slope from the same four corners.
bool SampleLandHeightSlope(out float height, out float2 slope, float2 quadLocalPos, uint quadrant, bool hasLand)
{
	height = 0.0f;
	slope = float2(0.0f, 0.0f);

	if (!hasLand)
		return false;

	float2 gridPosition = clamp(quadLocalPos / QUADRANT_GRASS_SPACING, 0.0f, QUADRANT_GRASS_PITCH - 1.001f);
	int2 baseSample = int2(gridPosition);
	float2 sampleFraction = gridPosition - baseSample;

	uint quadrantBase = quadrant * (QUADRANT_GRASS_PITCH * QUADRANT_GRASS_PITCH);
	uint lowerLeftIndex = quadrantBase + baseSample.y * QUADRANT_GRASS_PITCH + baseSample.x;
	float heightLowerLeft = QuadrantHeights[lowerLeftIndex];
	float heightLowerRight = QuadrantHeights[lowerLeftIndex + 1];
	float heightUpperLeft = QuadrantHeights[lowerLeftIndex + QUADRANT_GRASS_PITCH];
	float heightUpperRight = QuadrantHeights[lowerLeftIndex + QUADRANT_GRASS_PITCH + 1];

	height = lerp(lerp(heightLowerLeft, heightLowerRight, sampleFraction.x), lerp(heightUpperLeft, heightUpperRight, sampleFraction.x), sampleFraction.y);
	slope = float2(
				lerp(heightLowerRight - heightLowerLeft, heightUpperRight - heightUpperLeft, sampleFraction.y),
				lerp(heightUpperLeft - heightLowerLeft, heightUpperRight - heightLowerRight, sampleFraction.x)) *
	        (1.0f / QUADRANT_GRASS_SPACING);

	return true;
}

// The low 12 bits select QuadrantData. The remaining bits store the blade slot and flags.
StructuredBuffer<uint> VisibleBladeTasks : register(t5);
// Terrain and object surfaces dispatch separate ranges of the same work list.
cbuffer GrassWorkRange : register(b0)
{
	uint workItemOffset;
	float workShare;  // Share of each compact work item's patches that this dispatch generates.
}

uint LoadBladeTask(uint taskIndex)
{
	taskIndex += workItemOffset;
	return VisibleBladeTasks[taskIndex];
}

StructuredBuffer<uint> QuadrantGrassCells : register(t6);  // Packed 2x2 LAND IDs for each quadrant cell.
StructuredBuffer<float2> TileHeightBounds : register(t11);
StructuredBuffer<uint> OccupancyRows : register(t12);

#if defined(HIGH_LOD)
Texture2D<uint> GrassDensityTexture : register(t7);
#endif

Texture2D<float> GrassHiZ : register(t8);  // Shared current-frame scene-depth pyramid.
#if defined(LOW_LOD)
Texture2D<float> TerrainSurfaceLift : register(t9);
Texture2D<float> TerrainSurfaceHeight : register(t10);

/** @brief Returns how far generated roots can be raised around a box reaching `reach` from `world2D`. */
float GetTerrainLiftReach(float2 world2D, float reach)
{
	float2 offset = abs(world2D - grassLodOrigin);
	return TerrainLiftMax * GetTerrainLiftBlend(max(offset.x, offset.y) + reach);
}
#endif

static const uint WORK_QUADRANT_MASK = 0xFFFu;
static const uint WORK_LANE_SHIFT = 12u;
static const uint WORK_INSIDE_FRUSTUM = 1u << 17u;
static const uint WORK_OCCUPIED_TILE = 1u << 21u;
static const uint WORK_TILE_SHIFT = 22u;
static const uint WORK_TILE_MASK = 0xFFu;
static const uint WORK_FULL_GRASS = 1u << 30u;

static const uint PATCHES_PER_ROW = BLADES_PER_ROW / 2u;
static const uint PATCH_ROWS = (PATCHES_PER_QUADRANT + PATCHES_PER_ROW - 1u) / PATCHES_PER_ROW;
static const uint OCCUPANCY_TILES_PER_AXIS = QUADRANT_GRASS_PITCH - 1u;
static const uint MAX_TILE_PATCH_WIDTH = (PATCHES_PER_ROW + OCCUPANCY_TILES_PER_AXIS - 1u) / OCCUPANCY_TILES_PER_AXIS;

// Start slope-fill seeds after High's four base slots to keep their positions consistent across tiers.
static const uint SLOPE_EXTRA_SEED_BASE = 4u;

uint3 ExtraCandidateHash(uint2 patchPos, uint extraIndex, uint quadrantHash)
{
	return Random::pcg3d(uint3(patchPos, SLOPE_EXTRA_SEED_BASE + extraIndex + quadrantHash));
}

float2 ExtraCandidateQuadPos(uint2 patchPos, uint3 hash)
{
	return (float2(patchPos * 2u) + float2(hash.xy) * UINT_TO_FLOAT * 2.0f) * BLADE_TO_WORLD;
}

// Stable per-position dither shared by every tier's LOD fades.
float LodDither(float2 worldPos2D)
{
	return float(Random::pcg3d(uint3(asuint(worldPos2D), 0x9E3779B9u)).z) * UINT_TO_FLOAT;
}

// Maps a tile-local task index to its quadrant patch. Returns false outside the tile or past the last patch.
bool ResolveTilePatch(uint bladeTask, inout uint patch)
{
	if ((bladeTask & WORK_OCCUPIED_TILE) != 0u) {
		uint tile = (bladeTask >> WORK_TILE_SHIFT) & WORK_TILE_MASK;
		uint2 tilePos = uint2(tile % OCCUPANCY_TILES_PER_AXIS, tile / OCCUPANCY_TILES_PER_AXIS);
		uint2 patchStart = tilePos * uint2(PATCHES_PER_ROW, PATCH_ROWS) / OCCUPANCY_TILES_PER_AXIS;
		uint2 patchEnd = (tilePos + 1u) * uint2(PATCHES_PER_ROW, PATCH_ROWS) / OCCUPANCY_TILES_PER_AXIS;
		uint2 localPatch = uint2(patch % MAX_TILE_PATCH_WIDTH, patch / MAX_TILE_PATCH_WIDTH);
		if (any(localPatch >= patchEnd - patchStart))
			return false;
		patch = (patchStart.y + localPatch.y) * PATCHES_PER_ROW + patchStart.x + localPatch.x;
	}
	return patch < PATCHES_PER_QUADRANT;
}

#include "ProceduralGrass/PGrassCulling.hlsli"

#include "ProceduralGrass/PGrassPlacement.hlsli"

// Blade construction and generation build on placement.
#include "ProceduralGrass/PGrassBladeConstruction.hlsli"
#include "ProceduralGrass/PGrassGeneration.hlsli"

// Writes the group's staged blades with one output reservation per list. Every thread of the group must call it.
void FlushGroupBlades(uint groupIndex, uint2 emittedBladeCounts)
{
	if (groupIndex == 0u) {
		GroupInnerCount = 0u;
#if defined(PGRASS_OUTER_LIST)
		GroupOuterCount = 0u;
#endif
		GroupOutputBase = uint2(0u, 0u);
	}

	GroupMemoryBarrierWithGroupSync();

	uint2 threadOutputOffset = uint2(0u, 0u);
	if (emittedBladeCounts.x != 0u)
		InterlockedAdd(GroupInnerCount, emittedBladeCounts.x, threadOutputOffset.x);
#if defined(PGRASS_OUTER_LIST)
	if (emittedBladeCounts.y != 0u)
		InterlockedAdd(GroupOuterCount, emittedBladeCounts.y, threadOutputOffset.y);
#endif

	GroupMemoryBarrierWithGroupSync();

	if (groupIndex == 0u) {
		if (GroupInnerCount != 0u)
			IndirectArgs.InterlockedAdd(4u, GroupInnerCount, GroupOutputBase.x);
#if defined(PGRASS_OUTER_LIST)
		if (GroupOuterCount != 0u) {
			uint oldOuterEnd;
			IndirectArgs.InterlockedAdd(24u, GroupOuterCount, oldOuterEnd);
			IndirectArgs.InterlockedAdd(36u, 0u - GroupOuterCount, oldOuterEnd);
			GroupOutputBase.y = oldOuterEnd - GroupOuterCount;
		}
#endif
	}

	GroupMemoryBarrierWithGroupSync();

#if defined(PGRASS_OUTER_LIST)
	uint2 categoryOffset = 0u;
#endif
	uint emittedBladeCount = emittedBladeCounts.x + emittedBladeCounts.y;
	[loop] for (uint i = 0u; i < emittedBladeCount; ++i)
	{
#if defined(PGRASS_OUTER_LIST)
		uint outer = GroupBladeOuter[i * THREADGROUP_SIZE + groupIndex];
		uint outputIndex;
		if (outer != 0u)
			outputIndex = GroupOutputBase.y + threadOutputOffset.y + categoryOffset.y++;
		else
			outputIndex = GroupOutputBase.x + threadOutputOffset.x + categoryOffset.x++;
#else
		uint outputIndex = GroupOutputBase.x + threadOutputOffset.x + i;
#endif
		BladeOutput[outputIndex] = GroupBlades[i * THREADGROUP_SIZE + groupIndex];
	}
}

[numthreads(TG_DIM_X, TG_DIM_Y, 1)] void main(uint3 dispatch : SV_DispatchThreadID, uint3 groupID : SV_GroupID, uint groupIndex : SV_GroupIndex) {
	uint tileTask = LoadBladeTask(dispatch.z);

	if (groupIndex == 0u) {
		GroupTileOccluded = 0u;
		if (grassHiZParams.w >= 1.0f && debugFlags.x <= 0.5f) {
			if ((tileTask & (WORK_OCCUPIED_TILE | WORK_HAS_LAND)) == (WORK_OCCUPIED_TILE | WORK_HAS_LAND))
				GroupTileOccluded = IsOccupiedTileOccluded(tileTask) ? 1u : 0u;
		}
	}

	GroupMemoryBarrierWithGroupSync();
	if (GroupTileOccluded != 0u)
		return;

	uint2 emittedBladeCounts;
	GenerateThreadBlades(dispatch, groupIndex, emittedBladeCounts);
	FlushGroupBlades(groupIndex, emittedBladeCounts);
}
