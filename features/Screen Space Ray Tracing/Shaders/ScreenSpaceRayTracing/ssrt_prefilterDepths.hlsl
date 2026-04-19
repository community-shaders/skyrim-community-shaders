#include "ScreenSpaceRayTracing/ssrt_common.hlsli"

Texture2D<float> srcNDCDepth : register(t4);

RWTexture2D<float> outDepth0 : register(u0);
RWTexture2D<float> outDepth1 : register(u1);
RWTexture2D<float> outDepth2 : register(u2);
RWTexture2D<float> outDepth3 : register(u3);
RWTexture2D<float> outDepth4 : register(u4);

float DepthMIPFilter(float d0, float d1, float d2, float d3)
{
    return min(min(d0, d1), min(d2, d3));
}

// Min of center 2x2 pixels within a 4x4 full-res block (Hi-Z conservative)
float MinDepth4x4(uint2 fullRegionTL)
{
    return DepthMIPFilter(
        srcNDCDepth[fullRegionTL + uint2(1, 1)],
        srcNDCDepth[fullRegionTL + uint2(2, 1)],
        srcNDCDepth[fullRegionTL + uint2(1, 2)],
        srcNDCDepth[fullRegionTL + uint2(2, 2)]);
}

groupshared float g_scratchDepths[8][8];

// Dispatch at (quarterResW/2 + 7)/8, (quarterResH/2 + 7)/8, 1
// Each thread writes a 2x2 block at quarter-res mip 0 and builds the pyramid.
[numthreads(8, 8, 1)] void main(uint2 DTid : SV_DispatchThreadID, uint2 GTid : SV_GroupThreadID)
{
    // baseCoord: half of quarter-res (= fullRes/8)
    uint2 baseCoord = DTid;
    // pixCoord: quarter-res (mip 0)
    uint2 pixCoord = baseCoord * 2;
    // fullCoord: top-left of the 8x8 full-res region this thread covers
    uint2 fullCoord = pixCoord * 4;

    float d00 = MinDepth4x4(fullCoord + uint2(0, 0));
    float d10 = MinDepth4x4(fullCoord + uint2(4, 0));
    float d01 = MinDepth4x4(fullCoord + uint2(0, 4));
    float d11 = MinDepth4x4(fullCoord + uint2(4, 4));

    outDepth0[pixCoord + uint2(0, 0)] = d00;
    outDepth0[pixCoord + uint2(1, 0)] = d10;
    outDepth0[pixCoord + uint2(0, 1)] = d01;
    outDepth0[pixCoord + uint2(1, 1)] = d11;

    // MIP 1
    float dm1 = DepthMIPFilter(d00, d10, d01, d11);
    outDepth1[baseCoord] = dm1;
    g_scratchDepths[GTid.x][GTid.y] = dm1;

    GroupMemoryBarrierWithGroupSync();

    // MIP 2
    [branch] if (all((GTid % 2) == 0))
    {
        float inTL = g_scratchDepths[GTid.x + 0][GTid.y + 0];
        float inTR = g_scratchDepths[GTid.x + 1][GTid.y + 0];
        float inBL = g_scratchDepths[GTid.x + 0][GTid.y + 1];
        float inBR = g_scratchDepths[GTid.x + 1][GTid.y + 1];
        float dm2 = DepthMIPFilter(inTL, inTR, inBL, inBR);
        outDepth2[baseCoord / 2] = dm2;
        g_scratchDepths[GTid.x][GTid.y] = dm2;
    }

    GroupMemoryBarrierWithGroupSync();

    // MIP 3
    [branch] if (all((GTid % 4) == 0))
    {
        float inTL = g_scratchDepths[GTid.x + 0][GTid.y + 0];
        float inTR = g_scratchDepths[GTid.x + 2][GTid.y + 0];
        float inBL = g_scratchDepths[GTid.x + 0][GTid.y + 2];
        float inBR = g_scratchDepths[GTid.x + 2][GTid.y + 2];
        float dm3 = DepthMIPFilter(inTL, inTR, inBL, inBR);
        outDepth3[baseCoord / 4] = dm3;
        g_scratchDepths[GTid.x][GTid.y] = dm3;
    }

    GroupMemoryBarrierWithGroupSync();

    // MIP 4
    [branch] if (all((GTid % 8) == 0))
    {
        float inTL = g_scratchDepths[GTid.x + 0][GTid.y + 0];
        float inTR = g_scratchDepths[GTid.x + 4][GTid.y + 0];
        float inBL = g_scratchDepths[GTid.x + 0][GTid.y + 4];
        float inBR = g_scratchDepths[GTid.x + 4][GTid.y + 4];
        outDepth4[baseCoord / 8] = DepthMIPFilter(inTL, inTR, inBL, inBR);
    }
}
