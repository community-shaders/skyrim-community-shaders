/*
 * Copyright (c) 2023-2025, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

// Constants
#define HASH_GRID_POSITION_BIT_NUM          17
#define HASH_GRID_POSITION_BIT_MASK         ((1u << HASH_GRID_POSITION_BIT_NUM) - 1)
#define HASH_GRID_LEVEL_BIT_NUM             10
#define HASH_GRID_LEVEL_BIT_MASK            ((1u << HASH_GRID_LEVEL_BIT_NUM) - 1)
#define HASH_GRID_NORMAL_BIT_NUM            3
#define HASH_GRID_NORMAL_BIT_MASK           ((1u << HASH_GRID_NORMAL_BIT_NUM) - 1)
#define HASH_GRID_HASH_MAP_BUCKET_SIZE      16
#define HASH_GRID_HASH_MAP_SEARCH_WINDOW    4
#define HASH_GRID_INVALID_HASH_KEY_LO       0u
#define HASH_GRID_INVALID_HASH_KEY_HI       0u
#define HASH_GRID_INVALID_CACHE_INDEX       0xFFFFFFFF

// Tweakable parameters
#ifndef HASH_GRID_USE_NORMALS
#define HASH_GRID_USE_NORMALS               1       // account for the normal data in the hash key
#endif

#ifndef HASH_GRID_POSITION_OFFSET
#define HASH_GRID_POSITION_OFFSET           float3(0.0f, 0.0f, 0.0f)
#endif

#ifndef HASH_GRID_POSITION_BIAS
#define HASH_GRID_POSITION_BIAS             1e-4f   // may require adjustment for extreme scene scales
#endif

#ifndef HASH_GRID_NORMAL_BIAS
#define HASH_GRID_NORMAL_BIAS               1e-3f
#endif

#define HashGridIndex uint
#define HashGridKey uint2

struct HashGridParameters
{
    float3 cameraPosition;
    float logarithmBase;
    float sceneScale;
    float levelBias;
};

float HashGridLogBase(float x, float base)
{
    return log(x) / log(base);
}

// http://burtleburtle.net/bob/hash/integer.html
uint HashGridHashJenkins32(uint a)
{
    a = (a + 0x7ed55d16) + (a << 12);
    a = (a ^ 0xc761c23c) ^ (a >> 19);
    a = (a + 0x165667b1) + (a << 5);
    a = (a + 0xd3a2646c) ^ (a << 9);
    a = (a + 0xfd7046c5) + (a << 3);
    a = (a ^ 0xb55a4f09) ^ (a >> 16);

    return a;
}

uint HashGridHash32(HashGridKey hashKey)
{
    return HashGridHashJenkins32(hashKey.x) ^ HashGridHashJenkins32(hashKey.y);
}

HashGridKey HashGridPackKey(uint gx, uint gy, uint gz, uint glevel, uint normalBits)
{
    const uint xMask = HASH_GRID_POSITION_BIT_MASK; // 17 bits mask
    const uint lowYBits = 32u - HASH_GRID_POSITION_BIT_NUM; // = 15
    const uint lowYMask = ((1u << lowYBits) - 1u);         // mask for y low 15 bits
    // low word:
    uint lo = (gx & xMask) | ((gy & lowYMask) << (HASH_GRID_POSITION_BIT_NUM * 1)); // <<17
    // high word:
    uint gy_high = (gy >> lowYBits) & ((1u << (HASH_GRID_POSITION_BIT_NUM - lowYBits)) - 1u); // remaining 2 bits
    uint hi = (gy_high) | ((gz & HASH_GRID_POSITION_BIT_MASK) << 2) | ((glevel & HASH_GRID_LEVEL_BIT_MASK) << 19);

#if HASH_GRID_USE_NORMALS
    hi |= ((normalBits & HASH_GRID_NORMAL_BIT_MASK) << 29);
#endif

    return uint2(lo, hi);
}

int4 HashGridUnpackKey_getPositionLevel(const HashGridKey hashKey)
{
    const int signBit      = 1 << (HASH_GRID_POSITION_BIT_NUM - 1);
    const int signMask     = ~((1 << HASH_GRID_POSITION_BIT_NUM) - 1);

    uint lo = hashKey.x;
    uint hi = hashKey.y;

    int gx = int(lo & HASH_GRID_POSITION_BIT_MASK);

    const uint lowYBits = 32u - HASH_GRID_POSITION_BIT_NUM; // 15
    uint y_lo = (lo >> (HASH_GRID_POSITION_BIT_NUM * 1)) & ((1u << lowYBits) - 1u); // lo >> 17
    uint y_hi = hi & ((1u << (HASH_GRID_POSITION_BIT_NUM - lowYBits)) - 1u); // 2 bits
    int gy = int((y_hi << lowYBits) | y_lo);

    uint gz = (hi >> 2) & HASH_GRID_POSITION_BIT_MASK;

    uint glevel = (hi >> 19) & HASH_GRID_LEVEL_BIT_MASK;

    gx = (gx & signBit) != 0 ? gx | signMask : gx;
    gy = (gy & signBit) != 0 ? gy | signMask : gy;
    int gz_signed = int(gz);
    gz_signed = (gz_signed & signBit) != 0 ? gz_signed | signMask : gz_signed;

    return int4(gx, gy, gz_signed, int(glevel));
}

uint HashGridGetBaseSlot(const HashGridKey hashKey, uint capacity)
{
    uint hash = HashGridHash32(hashKey);
    uint slot = hash % capacity;

    return min(slot, capacity - HASH_GRID_HASH_MAP_BUCKET_SIZE);
}

uint HashGridGetLevel(float3 samplePosition, HashGridParameters gridParameters)
{
    const float distance2 = dot(gridParameters.cameraPosition - samplePosition, gridParameters.cameraPosition - samplePosition);

    return uint(clamp(0.5f * HashGridLogBase(distance2, gridParameters.logarithmBase) + gridParameters.levelBias, 1.0f, float(HASH_GRID_LEVEL_BIT_MASK)));
}

float HashGridGetVoxelSize(uint gridLevel, HashGridParameters gridParameters)
{
    return pow(gridParameters.logarithmBase, gridLevel) / (gridParameters.sceneScale * pow(gridParameters.logarithmBase, gridParameters.levelBias));
}

// Based on logarithmic caching by Johannes Jendersie
int4 HashGridCalculatePositionLog(float3 samplePosition, HashGridParameters gridParameters)
{
    samplePosition += float3(HASH_GRID_POSITION_BIAS, HASH_GRID_POSITION_BIAS, HASH_GRID_POSITION_BIAS);

    uint  gridLevel     = HashGridGetLevel(samplePosition, gridParameters);
    float voxelSize     = HashGridGetVoxelSize(gridLevel, gridParameters);
    int3  gridPosition  = int3(floor(samplePosition / voxelSize));

    return int4(gridPosition.xyz, gridLevel);
}

HashGridKey HashGridComputeSpatialHash(float3 samplePosition, float3 sampleNormal, HashGridParameters gridParameters)
{
    uint4 gridPosition = uint4(HashGridCalculatePositionLog(samplePosition, gridParameters));

    uint gx = gridPosition.x & HASH_GRID_POSITION_BIT_MASK;
    uint gy = gridPosition.y & HASH_GRID_POSITION_BIT_MASK;
    uint gz = gridPosition.z & HASH_GRID_POSITION_BIT_MASK;
    uint glevel = gridPosition.w & HASH_GRID_LEVEL_BIT_MASK;

    uint normalBits = 0;
#if HASH_GRID_USE_NORMALS
    normalBits =
        (sampleNormal.x + HASH_GRID_NORMAL_BIAS >= 0 ? 0u : 1u) +
        (sampleNormal.y + HASH_GRID_NORMAL_BIAS >= 0 ? 0u : 2u) +
        (sampleNormal.z + HASH_GRID_NORMAL_BIAS >= 0 ? 0u : 4u);
#endif // HASH_GRID_USE_NORMALS

    return HashGridPackKey(gx, gy, gz, glevel, normalBits);
}

float3 HashGridGetPositionFromKey(const HashGridKey hashKey, HashGridParameters gridParameters)
{
    int4 gp = HashGridUnpackKey_getPositionLevel(hashKey);
    uint   gridLevel        = uint(gp.w);
    float  voxelSize        = HashGridGetVoxelSize(gridLevel, gridParameters);
    float3 samplePosition   = (float3(gp.xyz) + 0.5f) * voxelSize;

    return samplePosition;
}

struct HashMapData
{
    uint capacity;

    RW_STRUCTURED_BUFFER(hashEntriesBuffer, uint2);

#if !HASH_GRID_ENABLE_64_BIT_ATOMICS
    RW_STRUCTURED_BUFFER(lockBuffer, uint);
#endif // !HASH_GRID_ENABLE_64_BIT_ATOMICS
};

void HashMapAtomicCompareExchange(in HashMapData hashMapData, in uint dstOffset, in uint2 compareValue, in uint2 value, out uint2 originalValue)
{
#if HASH_GRID_ENABLE_64_BIT_ATOMICS
#if SHARC_ENABLE_GLSL
    originalValue.x = InterlockedCompareExchange(BUFFER_AT_OFFSET(hashMapData.hashEntriesBuffer, dstOffset), compareValue.x, value.x);
    originalValue.y = InterlockedCompareExchange(BUFFER_AT_OFFSET(hashMapData.hashEntriesBuffer, dstOffset).y, compareValue.y, value.y);
#else // !SHARC_ENABLE_GLSL
    InterlockedCompareExchange(BUFFER_AT_OFFSET(hashMapData.hashEntriesBuffer, dstOffset).x, compareValue.x, value.x, originalValue.x);
    InterlockedCompareExchange(BUFFER_AT_OFFSET(hashMapData.hashEntriesBuffer, dstOffset).y, compareValue.y, value.y, originalValue.y);
#endif // !SHARC_ENABLE_GLSL
#else // !HASH_GRID_ENABLE_64_BIT_ATOMICS
    // ANY rearangments to the code below lead to device hang if fuse is unlimited
    const uint cLock = 0xAAAAAAAA;
    uint fuse = 0;
    const uint fuseLength = 8;
    bool busy = true;
    while (busy && fuse < fuseLength)
    {
        uint state;
        InterlockedExchange(hashMapData.lockBuffer[dstOffset], cLock, state);
        busy = state != 0;

        if (state != cLock)
        {
            originalValue = BUFFER_AT_OFFSET(hashMapData.hashEntriesBuffer, dstOffset);
            if (originalValue.x == compareValue.x && originalValue.y == compareValue.y)
                BUFFER_AT_OFFSET(hashMapData.hashEntriesBuffer, dstOffset) = value;
            InterlockedExchange(hashMapData.lockBuffer[dstOffset], state, fuse);
            fuse = fuseLength;
        }
        ++fuse;
    }
#endif // !HASH_GRID_ENABLE_64_BIT_ATOMICS
}

bool HashMapInsert(in HashMapData hashMapData, const HashGridKey hashKey, out HashGridIndex cacheIndex)
{
    const uint baseSlot = HashGridGetBaseSlot(hashKey, hashMapData.capacity);
    for (uint bucketOffset = 0; bucketOffset < HASH_GRID_HASH_MAP_BUCKET_SIZE; ++bucketOffset)
    {
        HashGridKey prevHashGridKey;
        HashMapAtomicCompareExchange(hashMapData, baseSlot + bucketOffset, uint2(HASH_GRID_INVALID_HASH_KEY_LO, HASH_GRID_INVALID_HASH_KEY_HI), hashKey, prevHashGridKey);

        if (prevHashGridKey.x == HASH_GRID_INVALID_HASH_KEY_LO && prevHashGridKey.y == HASH_GRID_INVALID_HASH_KEY_HI || prevHashGridKey.x == hashKey.x && prevHashGridKey.y == hashKey.y)
        {
            cacheIndex = baseSlot + bucketOffset;
            return true;
        }
    }

    cacheIndex = hashMapData.capacity - 1;

    return false;
}

bool HashMapFind(in HashMapData hashMapData, const HashGridKey hashKey, inout HashGridIndex cacheIndex, out uint bucketOffset)
{
    const uint baseSlot = HashGridGetBaseSlot(hashKey, hashMapData.capacity);
    for (bucketOffset = 0; bucketOffset < HASH_GRID_HASH_MAP_BUCKET_SIZE; ++bucketOffset)
    {
        HashGridKey storedHashKey = BUFFER_AT_OFFSET(hashMapData.hashEntriesBuffer, baseSlot + bucketOffset);

        if (storedHashKey.x == hashKey.x && storedHashKey.y == hashKey.y)
        {
            cacheIndex = baseSlot + bucketOffset;
            return true;
        }
    }

    return false;
}

HashGridIndex HashMapInsertEntry(in HashMapData hashMapData, float3 samplePosition, float3 sampleNormal, HashGridParameters gridParameters)
{
    HashGridIndex cacheIndex    = HASH_GRID_INVALID_CACHE_INDEX;
    const HashGridKey hashKey   = HashGridComputeSpatialHash(samplePosition, sampleNormal, gridParameters);
    bool successful             = HashMapInsert(hashMapData, hashKey, cacheIndex);

    return cacheIndex;
}

HashGridIndex HashMapFindEntry(in HashMapData hashMapData, float3 samplePosition, float3 sampleNormal, HashGridParameters gridParameters)
{
    HashGridIndex cacheIndex    = HASH_GRID_INVALID_CACHE_INDEX;
    const HashGridKey hashKey   = HashGridComputeSpatialHash(samplePosition, sampleNormal, gridParameters);
    uint hashCollisionsNum;
    bool successful             = HashMapFind(hashMapData, hashKey, cacheIndex, hashCollisionsNum);

    return cacheIndex;
}

// Debug functions
float3 HashGridGetColorFromHash32(uint hash)
{
    float3 color;
    color.x = ((hash >>  0) & 0x3ff) / 1023.0f;
    color.y = ((hash >> 11) & 0x7ff) / 2047.0f;
    color.z = ((hash >> 22) & 0x7ff) / 2047.0f;

    return color;
}

// Debug visualization
float3 HashGridDebugColoredHash(float3 samplePosition, float3 sampleNormal, HashGridParameters gridParameters)
{
    HashGridKey hashKey     = HashGridComputeSpatialHash(samplePosition, sampleNormal, gridParameters);
    uint gridLevel          = HashGridGetLevel(samplePosition, gridParameters);
    float3 color            = HashGridGetColorFromHash32(HashGridHash32(hashKey)) * HashGridGetColorFromHash32(HashGridHashJenkins32(gridLevel)).xyz;

    return color;
}

float3 HashGridDebugOccupancy(uint2 pixelPosition, uint2 screenSize, HashMapData hashMapData)
{
    const uint elementSize = 7;
    const uint borderSize = 1;
    const uint blockSize = elementSize + borderSize;

    uint rowNum = screenSize.y / blockSize;
    uint rowIndex = pixelPosition.y / blockSize;
    uint columnIndex = pixelPosition.x / blockSize;
    uint elementIndex = (columnIndex / HASH_GRID_HASH_MAP_BUCKET_SIZE) * (rowNum * HASH_GRID_HASH_MAP_BUCKET_SIZE) + rowIndex * HASH_GRID_HASH_MAP_BUCKET_SIZE + (columnIndex % HASH_GRID_HASH_MAP_BUCKET_SIZE);

    if (elementIndex < hashMapData.capacity && ((pixelPosition.x % blockSize) < elementSize && (pixelPosition.y % blockSize) < elementSize))
    {
        HashGridKey storedHashGridKey = BUFFER_AT_OFFSET(hashMapData.hashEntriesBuffer, elementIndex);
        if (storedHashGridKey.x != HASH_GRID_INVALID_HASH_KEY_LO || storedHashGridKey.y != HASH_GRID_INVALID_HASH_KEY_HI)
            return float3(0.0f, 1.0f, 0.0f);
    }

    return float3(0.0f, 0.0f, 0.0f);
}

float3 HashGridDebugHashCollisions(float3 samplePosition, float3 sampleNormal, HashGridParameters gridParameters, HashMapData hashMapData)
{
    HashGridKey hashKey     = HashGridComputeSpatialHash(samplePosition, sampleNormal, gridParameters);
    uint gridLevel          = HashGridGetLevel(samplePosition, gridParameters);

    HashGridIndex cacheIndex = HASH_GRID_INVALID_CACHE_INDEX;
    uint hashCollisionsNum;
    HashMapFind(hashMapData, hashKey, cacheIndex, hashCollisionsNum);

    float3 debugColor;
    if (hashCollisionsNum == 0)
        debugColor = float3(0.0f, 0.0f, 1.0f);
    else if (hashCollisionsNum == 1)
        debugColor = float3(0.0f, 0.5f, 0.5f);
    else if (hashCollisionsNum == 2)
        debugColor = float3(0.0f, 1.0f, 0.0f);
    else if (hashCollisionsNum == 3)
        debugColor = float3(1.0f, 1.0f, 0.0f);
    else if (hashCollisionsNum == 4)
        debugColor = float3(0.75f, 0.25f, 0.0f);
    else
        debugColor = float3(1.0f, 0.0f, 0.0f);

    return debugColor;
}
