#include "ScreenSpaceRayTracing/ssrt_common.hlsli"

Texture2D<float4> DiffuseTexture : register(t0);
Texture2D<float3> SpecularTexture : register(t1);

RWTexture2D<float4> outColor0 : register(u0);
RWTexture2D<float4> outColor1 : register(u1);
RWTexture2D<float4> outColor2 : register(u2);
RWTexture2D<float4> outColor3 : register(u3);
RWTexture2D<float4> outColor4 : register(u4);

float4 CombineColor(uint2 coord)
{
    float3 color = Color::IrradianceToGamma(Color::IrradianceToLinear(DiffuseTexture[coord].xyz) + SpecularTexture[coord].xyz);
    return float4(color, 1.0f);
}

// Average of center 2x2 within a 4x4 full-res block, with color combination
float4 DownsampleColor4x4(uint2 fullRegionTL)
{
    float4 c0 = CombineColor(fullRegionTL + uint2(1, 1));
    float4 c1 = CombineColor(fullRegionTL + uint2(2, 1));
    float4 c2 = CombineColor(fullRegionTL + uint2(1, 2));
    float4 c3 = CombineColor(fullRegionTL + uint2(2, 2));
    return (c0 + c1 + c2 + c3) * 0.25f;
}

float4 ColorMIPFilter(float4 c0, float4 c1, float4 c2, float4 c3)
{
    return (c0 + c1 + c2 + c3) * 0.25f;
}

groupshared float4 g_scratchColor[8][8];

// Dispatch at (fullResW/8 + 7)/8, (fullResH/8 + 7)/8, 1
[numthreads(8, 8, 1)] void main(uint2 DTid : SV_DispatchThreadID, uint2 GTid : SV_GroupThreadID)
{
    uint2 baseCoord = DTid;
    uint2 pixCoord = baseCoord * 2;
    uint2 fullCoord = pixCoord * 4;

    float4 c00 = DownsampleColor4x4(fullCoord + uint2(0, 0));
    float4 c10 = DownsampleColor4x4(fullCoord + uint2(4, 0));
    float4 c01 = DownsampleColor4x4(fullCoord + uint2(0, 4));
    float4 c11 = DownsampleColor4x4(fullCoord + uint2(4, 4));

    outColor0[pixCoord + uint2(0, 0)] = c00;
    outColor0[pixCoord + uint2(1, 0)] = c10;
    outColor0[pixCoord + uint2(0, 1)] = c01;
    outColor0[pixCoord + uint2(1, 1)] = c11;

    // MIP 1
    float4 cm1 = ColorMIPFilter(c00, c10, c01, c11);
    outColor1[baseCoord] = cm1;
    g_scratchColor[GTid.x][GTid.y] = cm1;

    GroupMemoryBarrierWithGroupSync();

    // MIP 2
    [branch] if (all((GTid % 2) == 0))
    {
        float4 inTL = g_scratchColor[GTid.x + 0][GTid.y + 0];
        float4 inTR = g_scratchColor[GTid.x + 1][GTid.y + 0];
        float4 inBL = g_scratchColor[GTid.x + 0][GTid.y + 1];
        float4 inBR = g_scratchColor[GTid.x + 1][GTid.y + 1];
        float4 cm2 = ColorMIPFilter(inTL, inTR, inBL, inBR);
        outColor2[baseCoord / 2] = cm2;
        g_scratchColor[GTid.x][GTid.y] = cm2;
    }

    GroupMemoryBarrierWithGroupSync();

    // MIP 3
    [branch] if (all((GTid % 4) == 0))
    {
        float4 inTL = g_scratchColor[GTid.x + 0][GTid.y + 0];
        float4 inTR = g_scratchColor[GTid.x + 2][GTid.y + 0];
        float4 inBL = g_scratchColor[GTid.x + 0][GTid.y + 2];
        float4 inBR = g_scratchColor[GTid.x + 2][GTid.y + 2];
        float4 cm3 = ColorMIPFilter(inTL, inTR, inBL, inBR);
        outColor3[baseCoord / 4] = cm3;
        g_scratchColor[GTid.x][GTid.y] = cm3;
    }

    GroupMemoryBarrierWithGroupSync();

    // MIP 4
    [branch] if (all((GTid % 8) == 0))
    {
        float4 inTL = g_scratchColor[GTid.x + 0][GTid.y + 0];
        float4 inTR = g_scratchColor[GTid.x + 4][GTid.y + 0];
        float4 inBL = g_scratchColor[GTid.x + 0][GTid.y + 4];
        float4 inBR = g_scratchColor[GTid.x + 4][GTid.y + 4];
        outColor4[baseCoord / 8] = ColorMIPFilter(inTL, inTR, inBL, inBR);
    }
}
