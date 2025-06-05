// This file is rewritten from AMD's FidelityFX SDK.
//
// Copyright (C) 2024 Advanced Micro Devices, Inc.
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#ifndef DOWNSAMPLE_FILTER
#define DOWNSAMPLE_FILTER 1 // 0=Average, 1=Min, 2=Max
#endif

#ifndef USE_LINEAR_SAMPLER
#define USE_LINEAR_SAMPLER 0
#endif

#ifndef MAX_MIP_LEVELS
#define MAX_MIP_LEVELS 12
#endif

cbuffer DownsampleCB : register(b2)
{
    uint2 srcDimensions;
    uint  numMips;
    uint  slice;  // unused
    uint2 workGroupOffset;
    uint  numWorkGroups;
    uint  _padding;
};

Texture2D<float4>       srcTexture      : register(t5);
RWTexture2D<float4>     dstMips[12]     : register(u0);

SamplerState linearSampler  : register(s0);
SamplerState pointSampler   : register(s1);

groupshared uint spdCounter = 0;
groupshared float spdIntermediateR[16][16];
groupshared float spdIntermediateG[16][16];
groupshared float spdIntermediateB[16][16];
groupshared float spdIntermediateA[16][16];

void SpdIncreaseAtomicCounter()
{
    InterlockedAdd(spdCounter, 1);
}

uint SpdGetAtomicCounter()
{
    return spdCounter;
}

void SpdResetAtomicCounter()
{
    if (0)
    {
        spdCounter = 0;
    }
    GroupMemoryBarrierWithGroupSync();
}

float4 SpdLoadSourceImage(int2 coord)
{
#if USE_LINEAR_SAMPLER
    float2 uv = (coord + 0.5) / float2(srcDimensions);
    return srcTexture.SampleLevel(linearSampler, uv, 0);
#else
    return srcTexture.Load(int3(coord, 0));
#endif
}

float4 SpdLoadMip(int2 coord, uint mipLevel)
{
    if (mipLevel > 0 && mipLevel <= MAX_MIP_LEVELS)
    {
        return dstMips[mipLevel - 1].Load(int3(coord, 0));
    }
    return float4(0, 0, 0, 0);
}

void SpdStoreMip(int2 coord, float4 value, uint mipLevel)
{
    if (mipLevel > 0 && mipLevel <= MAX_MIP_LEVELS)
    {
        dstMips[mipLevel - 1][coord] = value;
    }
}

float4 SpdLoadIntermediate(uint x, uint y)
{
    return float4(
        spdIntermediateR[x][y],
        spdIntermediateG[x][y],
        spdIntermediateB[x][y],
        spdIntermediateA[x][y]
    );
}

void SpdStoreIntermediate(uint x, uint y, float4 value)
{
    spdIntermediateR[x][y] = value.x;
    spdIntermediateG[x][y] = value.y;
    spdIntermediateB[x][y] = value.z;
    spdIntermediateA[x][y] = value.w;
}

float4 SpdReduce4(float4 v0, float4 v1, float4 v2, float4 v3)
{
#if DOWNSAMPLE_FILTER == 1
    return min(min(v0, v1), min(v2, v3));
#elif DOWNSAMPLE_FILTER == 2
    return max(max(v0, v1), max(v2, v3));
#else
    return (v0 + v1 + v2 + v3) * 0.25;
#endif
}

uint2 SpdGetLocalInvocationID(uint localIndex)
{
    return uint2(localIndex % 16, localIndex / 16);
}

void SpdDownsample(uint2 workGroupID, uint localInvocationIndex)
{
    uint2 sub_xy = SpdGetLocalInvocationID(localInvocationIndex);
    uint x = sub_xy.x;
    uint y = sub_xy.y;
    
    uint2 workGroupOffset2 = workGroupOffset;
    uint2 coord = workGroupOffset2 + (workGroupID << 4) + sub_xy;
    
    // Mip 0 -> Mip 1
    uint2 pix = coord * 2;
    float4 v0 = SpdLoadSourceImage(int2(pix));
    float4 v1 = SpdLoadSourceImage(int2(pix + uint2(1, 0)));
    float4 v2 = SpdLoadSourceImage(int2(pix + uint2(0, 1)));
    float4 v3 = SpdLoadSourceImage(int2(pix + uint2(1, 1)));
    float4 value = SpdReduce4(v0, v1, v2, v3);
    
    SpdStoreMip(int2(coord), value, 1);
    SpdStoreIntermediate(x, y, value);
    
    GroupMemoryBarrierWithGroupSync();
    
    // Mip 1 -> Mip 2
    if (localInvocationIndex < 64)
    {
        uint2 newCoord = uint2(x % 8, y % 8);
        uint2 sourceCoord = newCoord * 2;
        
        v0 = SpdLoadIntermediate(sourceCoord.x, sourceCoord.y);
        v1 = SpdLoadIntermediate(sourceCoord.x + 1, sourceCoord.y);
        v2 = SpdLoadIntermediate(sourceCoord.x, sourceCoord.y + 1);
        v3 = SpdLoadIntermediate(sourceCoord.x + 1, sourceCoord.y + 1);
        value = SpdReduce4(v0, v1, v2, v3);
        
        uint2 destCoord = (workGroupID * 8) + newCoord;
        SpdStoreMip(int2(destCoord), value, 2);
        SpdStoreIntermediate(newCoord.x, newCoord.y, value);
    }
    
    GroupMemoryBarrierWithGroupSync();
    
    // Mip 2 -> Mip 3
    if (localInvocationIndex < 16)
    {
        uint2 newCoord = uint2(x % 4, y % 4);
        uint2 sourceCoord = newCoord * 2;
        
        v0 = SpdLoadIntermediate(sourceCoord.x, sourceCoord.y);
        v1 = SpdLoadIntermediate(sourceCoord.x + 1, sourceCoord.y);
        v2 = SpdLoadIntermediate(sourceCoord.x, sourceCoord.y + 1);
        v3 = SpdLoadIntermediate(sourceCoord.x + 1, sourceCoord.y + 1);
        value = SpdReduce4(v0, v1, v2, v3);
        
        uint2 destCoord = (workGroupID * 4) + newCoord;
        SpdStoreMip(int2(destCoord), value, 3);
        SpdStoreIntermediate(newCoord.x, newCoord.y, value);
    }
    
    GroupMemoryBarrierWithGroupSync();
    
    // Mip 3 -> Mip 4
    if (localInvocationIndex < 4)
    {
        uint2 newCoord = uint2(x % 2, y % 2);
        uint2 sourceCoord = newCoord * 2;
        
        v0 = SpdLoadIntermediate(sourceCoord.x, sourceCoord.y);
        v1 = SpdLoadIntermediate(sourceCoord.x + 1, sourceCoord.y);
        v2 = SpdLoadIntermediate(sourceCoord.x, sourceCoord.y + 1);
        v3 = SpdLoadIntermediate(sourceCoord.x + 1, sourceCoord.y + 1);
        value = SpdReduce4(v0, v1, v2, v3);
        
        uint2 destCoord = (workGroupID * 2) + newCoord;
        SpdStoreMip(int2(destCoord), value, 4);
        SpdStoreIntermediate(newCoord.x, newCoord.y, value);
    }
    
    GroupMemoryBarrierWithGroupSync();
    
    // Mip 4 -> Mip 5
    if (localInvocationIndex < 1)
    {
        v0 = SpdLoadIntermediate(0, 0);
        v1 = SpdLoadIntermediate(1, 0);
        v2 = SpdLoadIntermediate(0, 1);
        v3 = SpdLoadIntermediate(1, 1);
        value = SpdReduce4(v0, v1, v2, v3);
        
        SpdStoreMip(int2(workGroupID), value, 5);
        SpdStoreIntermediate(0, 0, value);
        
        SpdIncreaseAtomicCounter();
    }
    
    GroupMemoryBarrierWithGroupSync();
    
    uint currentMip = 6;
    [unroll(256)]
    while (currentMip <= numMips && SpdGetAtomicCounter() >= numWorkGroups)
    {
        GroupMemoryBarrierWithGroupSync();
        
        if (localInvocationIndex == 0)
        {
            SpdResetAtomicCounter();
        }
        
        GroupMemoryBarrierWithGroupSync();
        
        uint2 currentDim = max(uint2(1, 1), srcDimensions >> currentMip);
        uint2 workGroupsNeeded = (currentDim + 15) / 16;
        
        if (workGroupID.x < workGroupsNeeded.x && workGroupID.y < workGroupsNeeded.y)
        {
            uint2 pixelCoord = workGroupID * 16 + sub_xy;
            if (pixelCoord.x < currentDim.x && pixelCoord.y < currentDim.y)
            {
                uint2 sourceCoord = pixelCoord * 2;
                
                v0 = SpdLoadMip(int2(sourceCoord), currentMip - 1);
                v1 = SpdLoadMip(int2(sourceCoord + uint2(1, 0)), currentMip - 1);
                v2 = SpdLoadMip(int2(sourceCoord + uint2(0, 1)), currentMip - 1);
                v3 = SpdLoadMip(int2(sourceCoord + uint2(1, 1)), currentMip - 1);
                value = SpdReduce4(v0, v1, v2, v3);
                
                SpdStoreMip(int2(pixelCoord), value, currentMip);
            }
        }
        
        currentMip++;
        GroupMemoryBarrierWithGroupSync();
    }
}

[numthreads(256, 1, 1)]
void main(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint3 workGroupID : SV_GroupID,
    uint  localInvocationIndex : SV_GroupIndex
)
{
    SpdDownsample(workGroupID.xy, localInvocationIndex);
}
