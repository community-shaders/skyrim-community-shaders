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
#define SHARC_VERSION_MINOR                     6
#define SHARC_VERSION_BUILD                     3
#define SHARC_VERSION_REVISION                  0

// Constants
#define SHARC_ACCUMULATED_FRAME_NUM_BIT_OFFSET  0
#define SHARC_ACCUMULATED_FRAME_NUM_BIT_NUM     16
#define SHARC_ACCUMULATED_FRAME_NUM_BIT_MASK    ((1 << SHARC_ACCUMULATED_FRAME_NUM_BIT_NUM) - 1)
#define SHARC_STALE_FRAME_NUM_BIT_OFFSET        16
#define SHARC_STALE_FRAME_NUM_BIT_NUM           16
#define SHARC_STALE_FRAME_NUM_BIT_MASK          ((1 << SHARC_STALE_FRAME_NUM_BIT_NUM) - 1)
#define SHARC_GRID_LOGARITHM_BASE               2.0f
#define SHARC_BLEND_ADJACENT_LEVELS             1       // combine the data from adjacent levels on camera movement
#define SHARC_NORMALIZED_SAMPLE_NUM             (1u << (SHARC_SAMPLE_NUM_BIT_NUM - 1))
#define SHARC_ACCUMULATED_FRAME_NUM_MIN         1       // minimum number of frames to use for data accumulation
#define SHARC_ACCUMULATED_FRAME_NUM_MAX         1024    // maximum number of frames to use for data accumulation
#define SHARC_STALE_FRAME_NUM_MAX               1024    // maximum number of frames without new samples before the cache entry is evicted

// Tweakable parameters
#ifndef SHARC_SAMPLE_NUM_THRESHOLD
#define SHARC_SAMPLE_NUM_THRESHOLD              0       // elements with sample count above this threshold will be used for early-out/resampling
#endif

#ifndef SHARC_SEPARATE_EMISSIVE
#define SHARC_SEPARATE_EMISSIVE                 0       // if enabled, emissive values must be provided separately during updates. For cache queries, you can either supply them directly or include them in the query result
#endif

#ifndef SHARC_MATERIAL_DEMODULATION
#define SHARC_MATERIAL_DEMODULATION             0       // enable material demodulation to preserve material details
#endif

#ifndef SHARC_PROPAGATION_DEPTH
#define SHARC_PROPAGATION_DEPTH                 4       // controls the amount of vertices stored in memory for signal backpropagation
#endif

#ifndef SHARC_LINEAR_PROBE_WINDOW_SIZE
#define SHARC_LINEAR_PROBE_WINDOW_SIZE          4       // size of the linear search window for probe lookups
#endif

#ifndef SHARC_ENABLE_CACHE_RESAMPLING
#define SHARC_ENABLE_CACHE_RESAMPLING           (SHARC_UPDATE && (SHARC_PROPAGATION_DEPTH > 1)) // resamples the cache during update step
#endif

#ifndef SHARC_RESAMPLING_DEPTH_MIN
#define SHARC_RESAMPLING_DEPTH_MIN              1       // controls minimum path depth which can be used with cache resampling
#endif

#ifndef SHARC_STALE_FRAME_NUM_MIN
#define SHARC_STALE_FRAME_NUM_MIN               8       // minimum number of frames to keep the element in the cache
#endif

#ifndef SHARC_GRID_LEVEL_BIAS
#define SHARC_GRID_LEVEL_BIAS                   0       // LOD bias - positive adds extra magnified levels, negative reduces levels
#endif

#ifndef SHARC_USE_FP16
#define SHARC_USE_FP16                          0       // use fp16 for sample weights storage
#endif

#ifndef RW_STRUCTURED_BUFFER
#define RW_STRUCTURED_BUFFER(name, type) RWStructuredBuffer<type> name
#endif

#ifndef BUFFER_AT_OFFSET
#define BUFFER_AT_OFFSET(name, offset) name[offset]
#endif

#if SHARC_USE_FP16
#define SharcSampleWeight float16_t3
#else // !SHARC_USE_FP16
#define SharcSampleWeight float3
#endif // SHARC_USE_FP16

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
#include "SharcTypes.h"

struct SharcParameters
{
    HashGridParameters gridParameters;
    HashMapData hashMapData;
    float radianceScale;            // quantization factor for atomic radiance accumulation (u32 per channel during SHARC_UPDATE). Start with 1e3f; reduce for large radiance values to prevent overflow
    bool enableAntiFireflyFilter;

    RW_STRUCTURED_BUFFER(accumulationBuffer, SharcAccumulationData);
    RW_STRUCTURED_BUFFER(resolvedBuffer, SharcPackedData);
};

struct SharcState
{
#if SHARC_UPDATE
    HashGridIndex cacheIndices[SHARC_PROPAGATION_DEPTH];
    SharcSampleWeight sampleWeights[SHARC_PROPAGATION_DEPTH];
    uint pathLength;
#endif // SHARC_UPDATE
    uint placeholder;
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
    float3 accumulatedRadiance;
    float accumulatedSampleNum;
    uint accumulatedFrameNum;
    uint staleFrameNum;
    float luminanceM2;
};

struct SharcResolveParameters
{
    float3 cameraPositionPrev;      // previous camera position
    uint accumulationFrameNum;      // maximum number of frames for the temporal accumulation window
    uint staleFrameNumMax;          // maximum number of frames without new samples before the cache entry is evicted
    bool enableAntiFireflyFilter;   // not used
};

SharcPackedData SharcPackVoxelData(float3 radiance, float sampleNum, uint accumulatedFrameNum, uint staleFrameNum)
{
    const float float16Max = 65504.0f;

    SharcPackedData packedData;
    packedData.radianceData.x = float16_t(min(radiance.x, float16Max));
    packedData.radianceData.y = float16_t(min(radiance.y, float16Max));
    packedData.radianceData.z = float16_t(min(radiance.z, float16Max));
    packedData.radianceData.w = float16_t(min(sampleNum, float16Max));
    packedData.sampleData.x = accumulatedFrameNum | (staleFrameNum << SHARC_STALE_FRAME_NUM_BIT_OFFSET);
    packedData.luminanceM2 = 0; // not used

    return packedData;
}

SharcVoxelData SharcUnpackVoxelData(SharcPackedData packedData)
{
    SharcVoxelData voxelData;
    voxelData.accumulatedRadiance.x = float(packedData.radianceData.x);
    voxelData.accumulatedRadiance.y = float(packedData.radianceData.y);
    voxelData.accumulatedRadiance.z = float(packedData.radianceData.z);
    voxelData.accumulatedSampleNum = float(packedData.radianceData.w);
    voxelData.accumulatedFrameNum = (packedData.sampleData >> SHARC_ACCUMULATED_FRAME_NUM_BIT_OFFSET) & SHARC_ACCUMULATED_FRAME_NUM_BIT_MASK;
    voxelData.staleFrameNum = (packedData.sampleData >> SHARC_STALE_FRAME_NUM_BIT_OFFSET) & SHARC_STALE_FRAME_NUM_BIT_MASK;
    voxelData.luminanceM2 = asfloat(packedData.luminanceM2);

    return voxelData;
}

SharcVoxelData SharcGetVoxelData(RW_STRUCTURED_BUFFER(voxelDataBuffer, SharcPackedData), HashGridIndex cacheIndex)
{
    SharcVoxelData voxelData;
    voxelData.accumulatedRadiance = float3(0, 0, 0);
    voxelData.accumulatedSampleNum = 0;
    voxelData.accumulatedFrameNum = 0;
    voxelData.staleFrameNum = 0;

    if (cacheIndex == HASH_GRID_INVALID_CACHE_INDEX)
        return voxelData;

    SharcPackedData packedData = BUFFER_AT_OFFSET(voxelDataBuffer, cacheIndex);

    return SharcUnpackVoxelData(packedData);
}

void SharcAddVoxelData(in SharcParameters sharcParameters, HashGridIndex cacheIndex, float3 sampleValue, float3 sampleWeight, uint sampleData)
{
    if (cacheIndex == HASH_GRID_INVALID_CACHE_INDEX)
        return;

    if (sharcParameters.enableAntiFireflyFilter)
    {
        const float3 luma = float3(0.213f, 0.715f, 0.072f);
        float scalarWeight = dot(sampleWeight, luma);
        scalarWeight = max(scalarWeight, 1.0f);

        const float sampleWeightThreshold = 2.0f;
        if (scalarWeight > sampleWeightThreshold)
        {
            SharcPackedData dataPackedPrev = BUFFER_AT_OFFSET(sharcParameters.resolvedBuffer, cacheIndex);
            float sampleNumPrev = float(dataPackedPrev.radianceData.w);
            const float sampleConfidenceThreshold = 2.0f;
            if (sampleNumPrev > sampleConfidenceThreshold)
            {
                float luminancePrev = max(dot(float3(dataPackedPrev.radianceData.xyz), luma), 1.0f);
                float luminanceCur = max(dot(sampleValue * sampleWeight, luma), 1.0f);
                float t = saturate((sampleNumPrev - 2.0f) / 10.0f);
                float confidenceScale = lerp(5.0f, 10.0f, t);
                sampleWeight *= saturate(confidenceScale * luminancePrev / luminanceCur);
            }
            else
            {
                scalarWeight = pow(scalarWeight, 0.5f);
                sampleWeight /= scalarWeight;
            }
        }
    }

    uint3 scaledRadiance = uint3(sampleValue * sampleWeight * sharcParameters.radianceScale);

    if (scaledRadiance.x != 0) InterlockedAdd(BUFFER_AT_OFFSET(sharcParameters.accumulationBuffer, cacheIndex).data.x, scaledRadiance.x);
    if (scaledRadiance.y != 0) InterlockedAdd(BUFFER_AT_OFFSET(sharcParameters.accumulationBuffer, cacheIndex).data.y, scaledRadiance.y);
    if (scaledRadiance.z != 0) InterlockedAdd(BUFFER_AT_OFFSET(sharcParameters.accumulationBuffer, cacheIndex).data.z, scaledRadiance.z);
    if (sampleData != 0) InterlockedAdd(BUFFER_AT_OFFSET(sharcParameters.accumulationBuffer, cacheIndex).data.w, sampleData);
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
    float3 materialDemodulation = float3(1.0f, 1.0f, 1.0f);
#if SHARC_MATERIAL_DEMODULATION
    materialDemodulation = sharcHitData.materialDemodulation;
#endif // SHARC_MATERIAL_DEMODULATION

#if SHARC_ENABLE_CACHE_RESAMPLING
    uint resamplingDepth = uint(round(lerp(SHARC_RESAMPLING_DEPTH_MIN, SHARC_PROPAGATION_DEPTH - 1, random)));
    if (resamplingDepth <= sharcState.pathLength)
    {
        SharcVoxelData voxelData = SharcGetVoxelData(sharcParameters.resolvedBuffer, cacheIndex);
        if (voxelData.accumulatedSampleNum > SHARC_SAMPLE_NUM_THRESHOLD)
        {
            sharcRadiance = voxelData.accumulatedRadiance;
            sharcRadiance *= materialDemodulation;
            continueTracing = false;
        }
    }
#endif // SHARC_ENABLE_CACHE_RESAMPLING

    if (continueTracing)
        SharcAddVoxelData(sharcParameters, cacheIndex, directLighting / materialDemodulation, float3(1.0f, 1.0f, 1.0f), 1);

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
    sharcState.sampleWeights[0] = SharcSampleWeight(1.0f / materialDemodulation);
    sharcState.pathLength = min(++sharcState.pathLength, SHARC_PROPAGATION_DEPTH - 1);
#endif // SHARC_UPDATE
    return continueTracing;
}

void SharcSetThroughput(inout SharcState sharcState, float3 throughput)
{
#if SHARC_UPDATE
    for (uint i = 0; i < sharcState.pathLength; ++i)
        sharcState.sampleWeights[i] *= SharcSampleWeight(throughput);
#endif // SHARC_UPDATE
}

bool SharcGetCachedRadiance(in SharcParameters sharcParameters, in SharcHitData sharcHitData, out float3 radiance, bool debug)
{
    if (debug) radiance = float3(0, 0, 0);
    const uint sampleThreshold = debug ? 0 : SHARC_SAMPLE_NUM_THRESHOLD;

    HashGridIndex cacheIndex = HashMapFindEntry(sharcParameters.hashMapData, sharcHitData.positionWorld, sharcHitData.normalWorld, sharcParameters.gridParameters);
    if (cacheIndex == HASH_GRID_INVALID_CACHE_INDEX)
        return false;

    SharcVoxelData voxelData = SharcGetVoxelData(sharcParameters.resolvedBuffer, cacheIndex);
    if (voxelData.accumulatedSampleNum > sampleThreshold)
    {
        radiance = voxelData.accumulatedRadiance;
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

    SharcAccumulationData accumulatedData = BUFFER_AT_OFFSET(sharcParameters.accumulationBuffer, entryIndex);
    SharcPackedData resolvedData = BUFFER_AT_OFFSET(sharcParameters.resolvedBuffer, entryIndex);
    SharcVoxelData sharcVoxelData = SharcUnpackVoxelData(resolvedData);

    float sampleNum = float(accumulatedData.data.w);
    float sampleNumPrev = sharcVoxelData.accumulatedSampleNum;
    uint accumulatedFrameNum = sharcVoxelData.accumulatedFrameNum + 1;
    uint staleFrameNum = sharcVoxelData.staleFrameNum;

    staleFrameNum = (sampleNum != 0) ? 0 : staleFrameNum + 1;
    uint staleFrameNumMax = clamp(resolveParameters.staleFrameNumMax, SHARC_STALE_FRAME_NUM_MIN, SHARC_STALE_FRAME_NUM_MAX);
    bool isValidElement = (staleFrameNum < staleFrameNumMax) ? true : false;

    if (!isValidElement)
    {
        SharcAccumulationData zeroAccumulationData;
        zeroAccumulationData.data = uint4(0, 0, 0, 0);

        SharcPackedData zeroPackedData;
        zeroPackedData.radianceData = float16_t4(0, 0, 0, 0);
        zeroPackedData.sampleData = 0;
        zeroPackedData.luminanceM2 = 0;

        BUFFER_AT_OFFSET(sharcParameters.hashMapData.hashEntriesBuffer, entryIndex) = HASH_GRID_INVALID_HASH_KEY;
        BUFFER_AT_OFFSET(sharcParameters.accumulationBuffer, entryIndex) = zeroAccumulationData;
        BUFFER_AT_OFFSET(sharcParameters.resolvedBuffer, entryIndex) = zeroPackedData;
        return;
    }
    else if (sampleNum == 0)
    {
        InterlockedAdd(BUFFER_AT_OFFSET(sharcParameters.resolvedBuffer, entryIndex).sampleData, (1 << SHARC_ACCUMULATED_FRAME_NUM_BIT_OFFSET) | (1 << SHARC_STALE_FRAME_NUM_BIT_OFFSET));
        return;
    }

    // Hash map lookup to find previous data if there were hash collisions during previous insertion and this frame a new empty slot got assigned
    // This is a linear probe search with fixed window size
    if (sampleNumPrev == 0)
    {
        for (uint i = entryIndex + 1; i < min(entryIndex + 1 + SHARC_LINEAR_PROBE_WINDOW_SIZE, sharcParameters.hashMapData.capacity); ++i)
        {
            HashGridKey hashKeyOld = BUFFER_AT_OFFSET(sharcParameters.hashMapData.hashEntriesBuffer, i);
            if (hashKeyOld == hashKey)
            {
                resolvedData = BUFFER_AT_OFFSET(sharcParameters.resolvedBuffer, i);
                sharcVoxelData = SharcUnpackVoxelData(resolvedData);
                sampleNumPrev = sharcVoxelData.accumulatedSampleNum;
                accumulatedFrameNum = sharcVoxelData.accumulatedFrameNum + 1;
                staleFrameNum = 0;
                break;
            }
        }
    }

    float3 accumulatedRadiance = float3(accumulatedData.data.xyz) * rcp(sharcParameters.radianceScale);
    float3 accumulatedRadiancePrev = sharcVoxelData.accumulatedRadiance;

    uint accumulationFrameNum = clamp(resolveParameters.accumulationFrameNum, SHARC_ACCUMULATED_FRAME_NUM_MIN, SHARC_ACCUMULATED_FRAME_NUM_MAX);
    if (accumulatedFrameNum > accumulationFrameNum)
    {
        float normalizationScale = float(accumulationFrameNum) / float(accumulatedFrameNum);
        accumulatedFrameNum = accumulationFrameNum;
        sampleNumPrev *= normalizationScale;
    }

    float sampleTotalInv = rcp(sampleNumPrev + sampleNum);

    accumulatedRadiance = accumulatedRadiance / max(sampleNum, 1e-6f);
    accumulatedRadiance = sampleNumPrev * sampleTotalInv * accumulatedRadiancePrev + sampleNum * sampleTotalInv * accumulatedRadiance;
    float accumulatedSampleNum = sampleNumPrev + sampleNum;

#if SHARC_BLEND_ADJACENT_LEVELS
    // Reproject sample from adjacent level
    float3 cameraOffset = sharcParameters.gridParameters.cameraPosition.xyz - resolveParameters.cameraPositionPrev.xyz;
    if ((dot(cameraOffset, cameraOffset) > 1e-6f) && (accumulatedFrameNum < resolveParameters.accumulationFrameNum))
    {
        HashGridKey adjacentLevelHashKey = SharcGetAdjacentLevelHashKey(hashKey, sharcParameters.gridParameters, resolveParameters.cameraPositionPrev);

        HashGridIndex cacheIndex = HASH_GRID_INVALID_CACHE_INDEX;
        uint hashCollisionsNum;
        if (HashMapFind(sharcParameters.hashMapData, adjacentLevelHashKey, cacheIndex, hashCollisionsNum))
        {
            SharcPackedData adjacentPackedDataPrev = BUFFER_AT_OFFSET(sharcParameters.resolvedBuffer, cacheIndex);
            SharcVoxelData adjacentVoxelDataPrev = SharcUnpackVoxelData(adjacentPackedDataPrev);
            float adjacentSampleNum = adjacentVoxelDataPrev.accumulatedSampleNum;
            if (adjacentSampleNum > SHARC_SAMPLE_NUM_THRESHOLD)
            {
                float blendWeight = rcp(adjacentSampleNum + accumulatedSampleNum);
                accumulatedRadiance = adjacentSampleNum * blendWeight * adjacentVoxelDataPrev.accumulatedRadiance + accumulatedSampleNum * blendWeight * accumulatedRadiance.xyz;
                accumulatedSampleNum += adjacentSampleNum;
            }
        }
    }
#endif // SHARC_BLEND_ADJACENT_LEVELS

    BUFFER_AT_OFFSET(sharcParameters.resolvedBuffer, entryIndex) = SharcPackVoxelData(accumulatedRadiance, accumulatedSampleNum, accumulatedFrameNum, staleFrameNum);

    // Clear buffer entry for the next frame
    SharcAccumulationData zeroAccumulationData;
    zeroAccumulationData.data = uint4(0, 0, 0, 0);
    BUFFER_AT_OFFSET(sharcParameters.accumulationBuffer, entryIndex) = zeroAccumulationData;
}
