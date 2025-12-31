#include "Raytracing/Denoiser/SVGF/Common.hlsli"

Texture2D<float4>   HistoryTexture          : register(t0);
Texture2D<float2>   MotionVectorTexture     : register(t1);
Texture2D<float4>   HistoryMomentsTexture   : register(t5); // moments in RG, frame count in B
Texture2D<float4>   HistoryNormalsTexture   : register(t6);

RWTexture2D<float4> FilteredOutput          : register(u0);
RWTexture2D<float4> MomentsOutput           : register(u1);

SamplerState        LinearSampler           : register(s0);

bool IsValidHistory(uint2 pixel, float2 uv, float3 currNormalWS)
{
    const uint2 screenSize = Resolution;
    if (uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1)
        return false;

    if (pixel.x >= screenSize.x || pixel.y >= screenSize.y)
        return false;

    float3 prevNormalWS;
    float roughness;
    GetNormalRoughness(HistoryNormalsTexture, pixel, prevNormalWS, roughness);
    float normalDiff = dot(currNormalWS, prevNormalWS);
    if (normalDiff < 0.866f) // cos 30
        return false;

    return true;
}

[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint2 screenSize = Resolution;
    if (DTid.x >= screenSize.x || DTid.y >= screenSize.y)
        return;

    const float2 uv = float2(DTid.xy + 0.5) * ResolutionRcp;

    float3 blendedColor = 0;
    float4 ssrColor = SSRColorTexture[DTid.xy];
    float depthCenter = DepthTexture[DTid.xy];

    float3 normalWS;
    float roughness;
    GetNormalRoughness(DTid.xy, normalWS, roughness);

    float luminance = Color::RGBToLuminance(ssrColor.rgb);
    float2 curMoment = float2(luminance, luminance * luminance) * 0.5;

    // Reproject UVs using motion vectors
    float2 prevUV = uv;
    ReprojectHit(MotionVectorTexture, LinearSampler, float3(uv, depthCenter), 0u, prevUV);

    float4 prevColor = 0.f;
    float prevAccumFrames = 0.f;
    float2 prevMoments = float2(0.f, 0.f);
    uint2 prevPixel = uint2(prevUV * screenSize);
    bool valid = false;

    if (IsValidHistory(prevPixel, prevUV, normalWS))
    {
        prevColor = HistoryTexture[prevPixel];
        prevAccumFrames = HistoryMomentsTexture[prevPixel].z;
        prevMoments = HistoryMomentsTexture[prevPixel].xy;
        valid = true;
    }

    if (!valid)
    {
        int2 bilinOffset[4] = { int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1) };
        float weightSum = 0.f;
        [unroll(4)]
        for (int i = 0; i < 4; i++)
        {
            int2 neighborPixel = int2(prevPixel) + bilinOffset[i];
            if (IsValidHistory(uint2(neighborPixel), prevUV, normalWS))
            {
                float4 neighborColor = HistoryTexture[uint2(neighborPixel)];
                float neighborAccumFrames = HistoryMomentsTexture[uint2(neighborPixel)].z;
                if (neighborAccumFrames > 0.f)
                {
                    prevColor += neighborColor;
                    prevAccumFrames += neighborAccumFrames;
                    prevMoments += HistoryMomentsTexture[uint2(neighborPixel)].xy;
                    weightSum += 1.f;
                }
            }
        }

        if (weightSum > 0.f)
        {
            prevColor /= weightSum;
            prevAccumFrames /= weightSum;
            prevMoments /= weightSum;
            valid = true;
        }
    }

    if (!valid)
    {
        float weightSum = 0.f;

        int2 offsets[8] =
        {
            int2(0, 2),
            int2(0, -2),
            int2(1, 1),
            int2(1, -1),
            int2(-1, 1),
            int2(-1, -1),
            int2(2, 0),
            int2(-2, 0)
        };

        [unroll(8)]
        for (int i = 0; i < 8; i++)
        {
            int2 neighborPixel = int2(prevPixel) + offsets[i];
            if (IsValidHistory(uint2(neighborPixel), prevUV, normalWS))
            {
                float4 neighborColor = HistoryTexture[uint2(neighborPixel)];
                float neighborAccumFrames = HistoryMomentsTexture[uint2(neighborPixel)].z;
                if (neighborAccumFrames > 0.f)
                {
                    prevColor += neighborColor;
                    prevAccumFrames += neighborAccumFrames;
                    prevMoments += HistoryMomentsTexture[uint2(neighborPixel)].xy;
                    weightSum += 1.f;
                }
            }
        }
        if (weightSum > 0.f)
        {
            prevColor /= weightSum;
            prevAccumFrames /= weightSum;
            prevMoments /= weightSum;
            valid = true;
        }
    }

    if (valid)
    {
        float alpha = max(1.0f / (prevAccumFrames + 1.0f), Frame.InvMaxAccumulatedFrames);
        blendedColor = lerp(prevColor.rgb, ssrColor.rgb, alpha);

        float prevLuminance = Color::RGBToLuminance(prevColor.rgb);
        float2 prevMoment = float2(prevLuminance, prevLuminance * prevLuminance);

        float momentAlpha = max(1.0f / (prevAccumFrames + 1.0f), Frame.InvMaxAccumulatedFrames);
        float2 moment = lerp(prevMoment, curMoment, momentAlpha);
        float variance = moment.y - (moment.x * moment.x);
        variance = max(variance, 0.f);
        FilteredOutput[DTid.xy] = float4(blendedColor, variance);
        MomentsOutput[DTid.xy] = float4(moment, prevAccumFrames + 1.0f, 0.f);
        return;
    }
    MomentsOutput[DTid.xy] = float4(curMoment, 1.0f, 0.f);
    FilteredOutput[DTid.xy] = float4(ssrColor.rgb, abs(curMoment.y - (curMoment.x * curMoment.x)));
}