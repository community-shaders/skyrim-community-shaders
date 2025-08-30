/*
 * Copyright (c) 2023-2025, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

// Version
#define SHARC_VERSION_MAJOR                     1
#define SHARC_VERSION_MINOR                     5
#define SHARC_VERSION_BUILD                     1
#define SHARC_VERSION_REVISION                  0

// Constants
#define SHARC_SAMPLE_NUM_BIT_NUM                18
#define SHARC_SAMPLE_NUM_BIT_OFFSET             0
#define SHARC_SAMPLE_NUM_BIT_MASK               ((1u << SHARC_SAMPLE_NUM_BIT_NUM) - 1)
#define SHARC_ACCUMULATED_FRAME_NUM_BIT_NUM     6
#define SHARC_ACCUMULATED_FRAME_NUM_BIT_OFFSET  (SHARC_SAMPLE_NUM_BIT_NUM)
#define SHARC_ACCUMULATED_FRAME_NUM_BIT_MASK    ((1u << SHARC_ACCUMULATED_FRAME_NUM_BIT_NUM) - 1)
#define SHARC_STALE_FRAME_NUM_BIT_NUM           8
#define SHARC_STALE_FRAME_NUM_BIT_OFFSET        (SHARC_SAMPLE_NUM_BIT_NUM + SHARC_ACCUMULATED_FRAME_NUM_BIT_NUM)
#define SHARC_STALE_FRAME_NUM_BIT_MASK          ((1u << SHARC_STALE_FRAME_NUM_BIT_NUM) - 1)
#define SHARC_GRID_LOGARITHM_BASE               2.0f
#define SHARC_GRID_LEVEL_BIAS                   0       // positive bias adds extra levels with content magnification (can be negative as well)
#define SHARC_BLEND_ADJACENT_LEVELS             1       // combine the data from adjacent levels on camera movement
#define SHARC_NORMALIZED_SAMPLE_NUM             (1u << (SHARC_SAMPLE_NUM_BIT_NUM - 1))
#define SHARC_ACCUMULATED_FRAME_NUM_MIN         1       // minimum number of frames to use for data accumulation
#define SHARC_ACCUMULATED_FRAME_NUM_MAX         SHARC_ACCUMULATED_FRAME_NUM_BIT_MASK // maximum number of frames to use for data accumulation


// Tweakable parameters
#ifndef SHARC_SAMPLE_NUM_MULTIPLIER
#define SHARC_SAMPLE_NUM_MULTIPLIER             16      // increase sample count internally to make resolve step with low sample count more robust, power of 2 usage may help compiler with optimizations
#endif

#ifndef SHARC_SAMPLE_NUM_THRESHOLD
#define SHARC_SAMPLE_NUM_THRESHOLD              0       // elements with sample count above this threshold will be used for early-out/resampling
#endif

#ifndef SHARC_SEPARATE_EMISSIVE
#define SHARC_SEPARATE_EMISSIVE                 0       // if enabled, emissive values must be provided separately during updates. For cache queries, you can either supply them directly or include them in the query result
#endif

#ifndef SHARC_MATERIAL_DEMODULATION
#define SHARC_MATERIAL_DEMODULATION             0       // enable material demodulation to preserve material details
#endif

#ifndef SHARC_PROPOGATION_DEPTH
#define SHARC_PROPOGATION_DEPTH                 4       // controls the amount of vertices stored in memory for signal backpropagation
#endif

#ifndef SHARC_ENABLE_CACHE_RESAMPLING
#define SHARC_ENABLE_CACHE_RESAMPLING           (SHARC_UPDATE && (SHARC_PROPOGATION_DEPTH > 1)) // resamples the cache during update step
#endif

#ifndef SHARC_RESAMPLING_DEPTH_MIN
#define SHARC_RESAMPLING_DEPTH_MIN              1       // controls minimum path depth which can be used with cache resampling
#endif

#ifndef SHARC_RADIANCE_SCALE
#define SHARC_RADIANCE_SCALE                    1e3f    // scale used for radiance values accumulation. Each component uses 32-bit integer for data storage
#endif

#ifndef SHARC_STALE_FRAME_NUM_MIN
#define SHARC_STALE_FRAME_NUM_MIN               8       // minimum number of frames to keep the element in the cache
#endif

#ifndef RW_STRUCTURED_BUFFER
#define RW_STRUCTURED_BUFFER(name, type) RWStructuredBuffer<type> name
#endif

#ifndef BUFFER_AT_OFFSET
#define BUFFER_AT_OFFSET(name, offset) name[offset]
#endif

// Debug
#define SHARC_DEBUG_BITS_OCCUPANCY_THRESHOLD_LOW        0.125
#define SHARC_DEBUG_BITS_OCCUPANCY_THRESHOLD_MEDIUM     0.5

/*
 * RTXGI2 DIVERGENCE:
 *    Use SHARC_ENABLE_64_BIT_ATOMICS instead of SHARC_DISABLE_64_BIT_ATOMICS
 *    (Prefer 'enable' bools over 'disable' to avoid unnecessary mental gymnastics)
 *    Automatically set SHARC_ENABLE_64_BIT_ATOMICS if we're using DXC and it's not defined.
 */
#if !defined(SHARC_ENABLE_64_BIT_ATOMICS) && defined(__DXC_VERSION_MAJOR)
// Use DXC macros to figure out if 64-bit atomics are possible from the current shader model
#if __SHADER_TARGET_MAJOR < 6
#define SHARC_ENABLE_64_BIT_ATOMICS 0
#elif __SHADER_TARGET_MAJOR > 6
#define SHARC_ENABLE_64_BIT_ATOMICS 1
#else
// 6.x
#if __SHADER_TARGET_MINOR < 6
#define SHARC_ENABLE_64_BIT_ATOMICS 0
#else
#define SHARC_ENABLE_64_BIT_ATOMICS 1
#endif
#endif
#elif !defined(SHARC_ENABLE_64_BIT_ATOMICS)
// Not DXC, and SHARC_ENABLE_64_BIT_ATOMICS not defined
#error "Please define SHARC_ENABLE_64_BIT_ATOMICS as 0 or 1"
#endif

#if SHARC_ENABLE_64_BIT_ATOMICS
#define HASH_GRID_ENABLE_64_BIT_ATOMICS 1
#else
#define HASH_GRID_ENABLE_64_BIT_ATOMICS 0
#endif
#include "HashGridCommon.h"

struct SharcParameters
{
    HashGridParameters gridParameters;
    HashMapData hashMapData;
    bool enableAntiFireflyFilter;

    RW_STRUCTURED_BUFFER(voxelDataBuffer, uint4);
    RW_STRUCTURED_BUFFER(voxelDataBufferPrev, uint4);
};

struct SharcState
{
#if SHARC_UPDATE
    HashGridIndex cacheIndices[SHARC_PROPOGATION_DEPTH];
    float3 sampleWeights[SHARC_PROPOGATION_DEPTH];
    uint pathLength;
#endif // SHARC_UPDATE
};

struct SharcHitData
{
    float3 positionWorld;
    float3 normalWorld;             // geometry normal in world space. Shading or object-space normals should work, but are not generally recommended
#if SHARC_MATERIAL_DEMODULATION
    float3 materialDemodulation;    // demodulation factor used to preserve material details. Use > 0 when active; set to float3(1.0f, 1.0f, 1.0f) when unused
#endif // SHARC_MATERIAL_DEMODULATION
#if SHARC_SEPARATE_EMISSIVE
    float3 emissive;                // separate emissive improves behavior with dynamic lighting. Requires computing material emissive on each(even cached) hit
#endif // SHARC_SEPARATE_EMISSIVE
};

struct SharcVoxelData
{
    uint3 accumulatedRadiance;
    uint accumulatedSampleNum;
    uint accumulatedFrameNum;
    uint staleFrameNum;
};

struct SharcResolveParameters
{
    float3 cameraPositionPrev;
    uint accumulationFrameNum;
    uint staleFrameNumMax;
    bool enableAntiFireflyFilter;
};

uint SharcGetSampleNum(uint packedData)
{
    return (packedData >> SHARC_SAMPLE_NUM_BIT_OFFSET) & SHARC_SAMPLE_NUM_BIT_MASK;
}

uint SharcGetStaleFrameNum(uint packedData)
{
    return (packedData >> SHARC_STALE_FRAME_NUM_BIT_OFFSET) & SHARC_STALE_FRAME_NUM_BIT_MASK;
}

uint SharcGetAccumulatedFrameNum(uint packedData)
{
    return (packedData >> SHARC_ACCUMULATED_FRAME_NUM_BIT_OFFSET) & SHARC_ACCUMULATED_FRAME_NUM_BIT_MASK;
}

float3 SharcResolveAccumulatedRadiance(uint3 accumulatedRadiance, uint accumulatedSampleNum)
{
    return accumulatedRadiance / (accumulatedSampleNum * float(SHARC_RADIANCE_SCALE));
}

SharcVoxelData SharcUnpackVoxelData(uint4 voxelDataPacked)
{
    SharcVoxelData voxelData;
    voxelData.accumulatedRadiance = voxelDataPacked.xyz;
    voxelData.accumulatedSampleNum = SharcGetSampleNum(voxelDataPacked.w);
    voxelData.staleFrameNum = SharcGetStaleFrameNum(voxelDataPacked.w);
    voxelData.accumulatedFrameNum = SharcGetAccumulatedFrameNum(voxelDataPacked.w);
    return voxelData;
}

SharcVoxelData SharcGetVoxelData(RW_STRUCTURED_BUFFER(voxelDataBuffer, uint4), HashGridIndex cacheIndex)
{
    SharcVoxelData voxelData;
    voxelData.accumulatedRadiance = uint3(0, 0, 0);
    voxelData.accumulatedSampleNum = 0;
    voxelData.accumulatedFrameNum = 0;
    voxelData.staleFrameNum = 0;

    if (cacheIndex == HASH_GRID_INVALID_CACHE_INDEX)
        return voxelData;

    uint4 voxelDataPacked = BUFFER_AT_OFFSET(voxelDataBuffer, cacheIndex);

    return SharcUnpackVoxelData(voxelDataPacked);
}

void SharcAddVoxelData(in SharcParameters sharcParameters, HashGridIndex cacheIndex, float3 sampleValue, float3 sampleWeight, uint sampleData)
{
    if (cacheIndex == HASH_GRID_INVALID_CACHE_INDEX)
        return;

    if (sharcParameters.enableAntiFireflyFilter)
    {
        float scalarWeight = dot(sampleWeight, float3(0.213f, 0.715f, 0.072f));
        scalarWeight = max(scalarWeight, 1.0f);

        const float sampleWeightThreshold = 2.0f;
        if (scalarWeight > sampleWeightThreshold)
        {
            uint4 voxelDataPackedPrev = BUFFER_AT_OFFSET(sharcParameters.voxelDataBufferPrev, cacheIndex);
            uint sampleNumPrev = SharcGetSampleNum(voxelDataPackedPrev.w);
            const uint sampleConfidenceThreshold = 2;
            if (sampleNumPrev > SHARC_SAMPLE_NUM_MULTIPLIER * sampleConfidenceThreshold)
            {
                float luminancePrev = max(dot(SharcResolveAccumulatedRadiance(voxelDataPackedPrev.xyz, sampleNumPrev), float3(0.213f, 0.715f, 0.072f)), 1.0f);
                float luminanceCur = max(dot(sampleValue * sampleWeight, float3(0.213f, 0.715f, 0.072f)), 1.0f);
                float confidenceScale = lerp(5.0f, 10.0f, 1.0f / sampleNumPrev);
                sampleWeight *= saturate(confidenceScale * luminancePrev / luminanceCur);
            }
            else
            {
                scalarWeight = pow(scalarWeight, 0.5f);
                sampleWeight /= scalarWeight;
            }
        }
    }

    uint3 scaledRadiance = uint3(sampleValue * sampleWeight * SHARC_RADIANCE_SCALE);

    if (scaledRadiance.x != 0) InterlockedAdd(BUFFER_AT_OFFSET(sharcParameters.voxelDataBuffer, cacheIndex).x, scaledRadiance.x);
    if (scaledRadiance.y != 0) InterlockedAdd(BUFFER_AT_OFFSET(sharcParameters.voxelDataBuffer, cacheIndex).y, scaledRadiance.y);
    if (scaledRadiance.z != 0) InterlockedAdd(BUFFER_AT_OFFSET(sharcParameters.voxelDataBuffer, cacheIndex).z, scaledRadiance.z);
    if (sampleData != 0) InterlockedAdd(BUFFER_AT_OFFSET(sharcParameters.voxelDataBuffer, cacheIndex).w, sampleData);
}

void SharcInit(inout SharcState sharcState)
{
#if SHARC_UPDATE
    sharcState.pathLength = 0;
#endif // SHARC_UPDATE
}

void SharcUpdateMiss(in SharcParameters sharcParameters, in SharcState sharcState, float3 radiance)
{
#if SHARC_UPDATE
    for (int i = 0; i < sharcState.pathLength; ++i)
        SharcAddVoxelData(sharcParameters, sharcState.cacheIndices[i], radiance, sharcState.sampleWeights[i], 0);
#endif // SHARC_UPDATE
}

bool SharcUpdateHit(in SharcParameters sharcParameters, inout SharcState sharcState, SharcHitData sharcHitData, float3 directLighting, float random)
{
    bool continueTracing = true;
#if SHARC_UPDATE
    HashGridIndex cacheIndex = HashMapInsertEntry(sharcParameters.hashMapData, sharcHitData.positionWorld, sharcHitData.normalWorld, sharcParameters.gridParameters);

    float3 sharcRadiance = directLighting;

#if SHARC_ENABLE_CACHE_RESAMPLING
    uint resamplingDepth = uint(round(lerp(SHARC_RESAMPLING_DEPTH_MIN, SHARC_PROPOGATION_DEPTH - 1, random)));
    if (resamplingDepth <= sharcState.pathLength)
    {
        SharcVoxelData voxelData = SharcGetVoxelData(sharcParameters.voxelDataBufferPrev, cacheIndex);
        if (voxelData.accumulatedSampleNum > SHARC_SAMPLE_NUM_THRESHOLD)
        {
            sharcRadiance = SharcResolveAccumulatedRadiance(voxelData.accumulatedRadiance, voxelData.accumulatedSampleNum);
#if SHARC_MATERIAL_DEMODULATION
            sharcRadiance *= sharcHitData.materialDemodulation;
#endif // SHARC_MATERIAL_DEMODULATION
            continueTracing = false;
        }
    }
#endif // SHARC_ENABLE_CACHE_RESAMPLING

    if (continueTracing)
    {
#if SHARC_MATERIAL_DEMODULATION
        SharcAddVoxelData(sharcParameters, cacheIndex, directLighting / sharcHitData.materialDemodulation, float3(1.0f, 1.0f, 1.0f), 1);
#else // !SHARC_MATERIAL_DEMODULATION
        SharcAddVoxelData(sharcParameters, cacheIndex, directLighting, float3(1.0f, 1.0f, 1.0f), 1);
#endif // !SHARC_MATERIAL_DEMODULATION
    }

#if SHARC_SEPARATE_EMISSIVE
    sharcRadiance += sharcHitData.emissive;
#endif // SHARC_SEPARATE_EMISSIVE

    uint i;
    for (i = 0; i < sharcState.pathLength; ++i)
        SharcAddVoxelData(sharcParameters, sharcState.cacheIndices[i], sharcRadiance, sharcState.sampleWeights[i], 0);

    for (i = sharcState.pathLength; i > 0; --i)
    {
        sharcState.cacheIndices[i] = sharcState.cacheIndices[i - 1];
        sharcState.sampleWeights[i] = sharcState.sampleWeights[i - 1];
    }

    sharcState.cacheIndices[0] = cacheIndex;
    sharcState.sampleWeights[0] = 1.0f;
#if SHARC_MATERIAL_DEMODULATION
    sharcState.sampleWeights[0] = 1.0f / sharcHitData.materialDemodulation;
#endif // SHARC_MATERIAL_DEMODULATION
    sharcState.pathLength = min(++sharcState.pathLength, SHARC_PROPOGATION_DEPTH - 1);
#endif // SHARC_UPDATE
    return continueTracing;
}

void SharcSetThroughput(inout SharcState sharcState, float3 throughput)
{
#if SHARC_UPDATE
    for (uint i = 0; i < sharcState.pathLength; ++i)
        sharcState.sampleWeights[i] *= throughput;
#endif // SHARC_UPDATE
}

bool SharcGetCachedRadiance(in SharcParameters sharcParameters, in SharcHitData sharcHitData, out float3 radiance, bool debug)
{
    if (debug) radiance = float3(0, 0, 0);
    const uint sampleThreshold = debug ? 0 : SHARC_SAMPLE_NUM_THRESHOLD;

    HashGridIndex cacheIndex = HashMapFindEntry(sharcParameters.hashMapData, sharcHitData.positionWorld, sharcHitData.normalWorld, sharcParameters.gridParameters);
    if (cacheIndex == HASH_GRID_INVALID_CACHE_INDEX)
        return false;

    SharcVoxelData voxelData = SharcGetVoxelData(sharcParameters.voxelDataBuffer, cacheIndex);
    if (voxelData.accumulatedSampleNum > sampleThreshold)
    {
        radiance = SharcResolveAccumulatedRadiance(voxelData.accumulatedRadiance, voxelData.accumulatedSampleNum);
#if SHARC_MATERIAL_DEMODULATION
        radiance *= sharcHitData.materialDemodulation;
#endif // SHARC_MATERIAL_DEMODULATION
#if SHARC_SEPARATE_EMISSIVE
        radiance += sharcHitData.emissive;
#endif // SHARC_SEPARATE_EMISSIVE

        return true;
    }

    return false;
}

int SharcGetGridDistance2(int3 position)
{
    return position.x * position.x + position.y * position.y + position.z * position.z;
}

HashGridKey SharcGetAdjacentLevelHashKey(HashGridKey hashKey, HashGridParameters gridParameters, float3 cameraPositionPrev)
{
	const int signBit      = 1 << (HASH_GRID_POSITION_BIT_NUM - 1);
    const int signMask     = ~((1 << HASH_GRID_POSITION_BIT_NUM) - 1);

    int3 gridPosition;
    gridPosition.x = int((hashKey >> HASH_GRID_POSITION_BIT_NUM * 0) & HASH_GRID_POSITION_BIT_MASK);
    gridPosition.y = int((hashKey >> HASH_GRID_POSITION_BIT_NUM * 1) & HASH_GRID_POSITION_BIT_MASK);
    gridPosition.z = int((hashKey >> HASH_GRID_POSITION_BIT_NUM * 2) & HASH_GRID_POSITION_BIT_MASK);

    // Fix negative coordinates
    gridPosition.x = ((gridPosition.x & signBit) != 0) ? gridPosition.x | signMask : gridPosition.x;
    gridPosition.y = ((gridPosition.y & signBit) != 0) ? gridPosition.y | signMask : gridPosition.y;
    gridPosition.z = ((gridPosition.z & signBit) != 0) ? gridPosition.z | signMask : gridPosition.z;

    int level = int((hashKey >> (HASH_GRID_POSITION_BIT_NUM * 3)) & HASH_GRID_LEVEL_BIT_MASK);

    float voxelSize = HashGridGetVoxelSize(level, gridParameters);
    int3 cameraGridPosition = int3(floor((gridParameters.cameraPosition + HASH_GRID_POSITION_OFFSET) / voxelSize));
    int3 cameraVector = cameraGridPosition - gridPosition;
    int cameraDistance = SharcGetGridDistance2(cameraVector);

    int3 cameraGridPositionPrev = int3(floor((cameraPositionPrev + HASH_GRID_POSITION_OFFSET) / voxelSize));
    int3 cameraVectorPrev = cameraGridPositionPrev - gridPosition;
    int cameraDistancePrev = SharcGetGridDistance2(cameraVectorPrev);

    if (cameraDistance < cameraDistancePrev)
    {
        gridPosition = int3(floor(gridPosition / gridParameters.logarithmBase));
        level = min(level + 1, int(HASH_GRID_LEVEL_BIT_MASK));
    }
    else // this may be inaccurate
    {
        gridPosition = int3(floor(gridPosition * gridParameters.logarithmBase));
        level = max(level - 1, 1);
    }

    HashGridKey modifiedHashGridKey = ((uint64_t(gridPosition.x) & HASH_GRID_POSITION_BIT_MASK) << (HASH_GRID_POSITION_BIT_NUM * 0))
        | ((uint64_t(gridPosition.y) & HASH_GRID_POSITION_BIT_MASK) << (HASH_GRID_POSITION_BIT_NUM * 1))
        | ((uint64_t(gridPosition.z) & HASH_GRID_POSITION_BIT_MASK) << (HASH_GRID_POSITION_BIT_NUM * 2))
        | ((uint64_t(level) & HASH_GRID_LEVEL_BIT_MASK) << (HASH_GRID_POSITION_BIT_NUM * 3));

#if HASH_GRID_USE_NORMALS
    modifiedHashGridKey |= hashKey & (uint64_t(HASH_GRID_NORMAL_BIT_MASK) << (HASH_GRID_POSITION_BIT_NUM * 3 + HASH_GRID_LEVEL_BIT_NUM));
#endif // HASH_GRID_USE_NORMALS

    return modifiedHashGridKey;
}

void SharcResolveEntry(uint entryIndex, SharcParameters sharcParameters, SharcResolveParameters resolveParameters)
{
    if (entryIndex >= sharcParameters.hashMapData.capacity)
        return;

    HashGridKey hashKey = BUFFER_AT_OFFSET(sharcParameters.hashMapData.hashEntriesBuffer, entryIndex);
    if (hashKey == HASH_GRID_INVALID_HASH_KEY)
        return;

    uint4 voxelDataPackedPrev = BUFFER_AT_OFFSET(sharcParameters.voxelDataBufferPrev, entryIndex);
    uint4 voxelDataPacked = BUFFER_AT_OFFSET(sharcParameters.voxelDataBuffer, entryIndex);

    uint sampleNum = SharcGetSampleNum(voxelDataPacked.w);
    uint sampleNumPrev = SharcGetSampleNum(voxelDataPackedPrev.w);
    uint accumulatedFrameNum = SharcGetAccumulatedFrameNum(voxelDataPackedPrev.w) + 1;
    uint staleFrameNum = SharcGetStaleFrameNum(voxelDataPackedPrev.w);

    voxelDataPacked.xyz *= SHARC_SAMPLE_NUM_MULTIPLIER;
    sampleNum *= SHARC_SAMPLE_NUM_MULTIPLIER;

    uint3 accumulatedRadiance = voxelDataPacked.xyz + voxelDataPackedPrev.xyz;
    uint accumulatedSampleNum = sampleNum + sampleNumPrev;

#if SHARC_BLEND_ADJACENT_LEVELS
    // Reproject sample from adjacent level
    float3 cameraOffset = sharcParameters.gridParameters.cameraPosition.xyz - resolveParameters.cameraPositionPrev.xyz;
    if ((dot(cameraOffset, cameraOffset) != 0) && (accumulatedFrameNum < resolveParameters.accumulationFrameNum))
    {
        HashGridKey adjacentLevelHashKey = SharcGetAdjacentLevelHashKey(hashKey, sharcParameters.gridParameters, resolveParameters.cameraPositionPrev);

        HashGridIndex cacheIndex = HASH_GRID_INVALID_CACHE_INDEX;
        uint hashCollisionsNum;
        if (HashMapFind(sharcParameters.hashMapData, adjacentLevelHashKey, cacheIndex, hashCollisionsNum))
        {
            uint4 adjacentPackedDataPrev = BUFFER_AT_OFFSET(sharcParameters.voxelDataBufferPrev, cacheIndex);
            uint adjacentSampleNum = SharcGetSampleNum(adjacentPackedDataPrev.w);
            if (adjacentSampleNum > SHARC_SAMPLE_NUM_THRESHOLD)
            {
                float blendWeight = adjacentSampleNum / float(adjacentSampleNum + accumulatedSampleNum);
                accumulatedRadiance = uint3(lerp(float3(accumulatedRadiance.xyz), float3(adjacentPackedDataPrev.xyz), blendWeight));
                accumulatedSampleNum = uint(lerp(float(accumulatedSampleNum), float(adjacentSampleNum), blendWeight));
            }
        }
    }
#endif // SHARC_BLEND_ADJACENT_LEVELS

    // Clamp internal sample count to help with potential overflow
    if (accumulatedSampleNum > SHARC_NORMALIZED_SAMPLE_NUM)
    {
        accumulatedSampleNum >>= 1;
        accumulatedRadiance >>= 1;
    }

    uint accumulationFrameNum = clamp(resolveParameters.accumulationFrameNum, SHARC_ACCUMULATED_FRAME_NUM_MIN, SHARC_ACCUMULATED_FRAME_NUM_MAX);
    if (accumulatedFrameNum > accumulationFrameNum)
    {
        float normalizedAccumulatedSampleNum = round(accumulatedSampleNum * float(accumulationFrameNum) / accumulatedFrameNum);
        float normalizationScale = normalizedAccumulatedSampleNum / accumulatedSampleNum;

        accumulatedSampleNum = uint(normalizedAccumulatedSampleNum);
        accumulatedRadiance = uint3(accumulatedRadiance * normalizationScale);
        accumulatedFrameNum = uint(accumulatedFrameNum * normalizationScale);
    }

    staleFrameNum = (sampleNum != 0) ? 0 : staleFrameNum + 1;

    uint4 packedData;
    packedData.xyz = accumulatedRadiance;

    packedData.w = min(accumulatedSampleNum, SHARC_SAMPLE_NUM_BIT_MASK);
    packedData.w |= (min(accumulatedFrameNum, SHARC_ACCUMULATED_FRAME_NUM_BIT_MASK) << SHARC_ACCUMULATED_FRAME_NUM_BIT_OFFSET);
    packedData.w |= (min(staleFrameNum, SHARC_STALE_FRAME_NUM_BIT_MASK) << SHARC_STALE_FRAME_NUM_BIT_OFFSET);

    bool isValidElement = (staleFrameNum < max(resolveParameters.staleFrameNumMax, SHARC_STALE_FRAME_NUM_MIN)) ? true : false;

    if (!isValidElement)
    {
        packedData = uint4(0, 0, 0, 0);
        BUFFER_AT_OFFSET(sharcParameters.hashMapData.hashEntriesBuffer, entryIndex) = HASH_GRID_INVALID_HASH_KEY;
    }

    {
        BUFFER_AT_OFFSET(sharcParameters.voxelDataBuffer, entryIndex) = packedData;
    }

#if !SHARC_BLEND_ADJACENT_LEVELS
    // Clear buffer entry for the next frame
    //BUFFER_AT_OFFSET(sharcParameters.voxelDataBufferPrev, entryIndex) = uint4(0, 0, 0, 0);
#endif // !SHARC_BLEND_ADJACENT_LEVELS
}

// Debug functions
float3 SharcDebugGetBitsOccupancyColor(float occupancy)
{
    if (occupancy < SHARC_DEBUG_BITS_OCCUPANCY_THRESHOLD_LOW)
        return float3(0.0f, 1.0f, 0.0f) * (occupancy + SHARC_DEBUG_BITS_OCCUPANCY_THRESHOLD_LOW);
    else if (occupancy < SHARC_DEBUG_BITS_OCCUPANCY_THRESHOLD_MEDIUM)
        return float3(1.0f, 1.0f, 0.0f) * (occupancy + SHARC_DEBUG_BITS_OCCUPANCY_THRESHOLD_MEDIUM);
    else
        return float3(1.0f, 0.0f, 0.0f) * occupancy;
}

// Debug visualization
float3 SharcDebugBitsOccupancySampleNum(in SharcParameters sharcParameters, in SharcHitData sharcHitData)
{
    HashGridIndex cacheIndex = HashMapFindEntry(sharcParameters.hashMapData, sharcHitData.positionWorld, sharcHitData.normalWorld, sharcParameters.gridParameters);
    SharcVoxelData voxelData = SharcGetVoxelData(sharcParameters.voxelDataBuffer, cacheIndex);

    float occupancy = float(voxelData.accumulatedSampleNum) / SHARC_SAMPLE_NUM_BIT_MASK;

    return SharcDebugGetBitsOccupancyColor(occupancy);
}

float3 SharcDebugBitsOccupancyRadiance(in SharcParameters sharcParameters, in SharcHitData sharcHitData)
{
    HashGridIndex cacheIndex = HashMapFindEntry(sharcParameters.hashMapData, sharcHitData.positionWorld, sharcHitData.normalWorld, sharcParameters.gridParameters);
    SharcVoxelData voxelData = SharcGetVoxelData(sharcParameters.voxelDataBuffer, cacheIndex);

    float occupancy = float(max(voxelData.accumulatedRadiance.x, max(voxelData.accumulatedRadiance.y, voxelData.accumulatedRadiance.z))) / 0xFFFFFFFF;

    return SharcDebugGetBitsOccupancyColor(occupancy);
}
