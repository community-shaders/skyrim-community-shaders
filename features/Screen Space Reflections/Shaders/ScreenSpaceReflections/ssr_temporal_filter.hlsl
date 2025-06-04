#include "ScreenSpaceReflections/ssr_common.hlsli"

#define VARIANCE_THRESHOLD 0.0005

Texture2D<float4> SSRColor : register(t0);
Texture2D<float4> MotionVectors : register(t1);
Texture2D<float4> HistoryRadiance : register(t4);
Texture2D<float> DepthTextureMips : register(t5);

RWTexture2D<float4> OutputTemporalRadiance : register(u0);

cbuffer SSRCB : register(b1)
{
    uint MaxSteps;
    uint NumRays;
    uint Glossy;
    uint SpatialFilterSteps;
    float RoughnessMask;
    float TemporalScale;
    float TemporalWeight;
};

static const int2 TemportalOffsets[9] = { int2(-1, -1), int2(0, -1), int2(1, -1), int2(-1, 0), int2(0, 0), int2(1, 0), int2(-1, 1), int2(0, 1), int2(1, 1) };

float ComputeTemporalVariance(float3 History_Radiance, float3 Radiance)
{
    // Check temporal variance. 
    float history_luminance = Color::RGBToLuminanceAlternative(History_Radiance);
    float luminance = Color::RGBToLuminanceAlternative(Radiance);
    return abs(history_luminance - luminance) / max(max(history_luminance, luminance), 0.00001);
}

[numthreads(16, 16, 1)] void main(uint3 DTid : SV_DispatchThreadID)
{
    float2 uv = float2(DTid.xy + 0.5) * SharedData::BufferDim.zw;
    uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);
	uv *= FrameBuffer::DynamicResolutionParams2.xy;  // Adjust for dynamic res
	uv = Stereo::ConvertFromStereoUV(uv, eyeIndex);

    float hitDepth = DepthTextureMips[DTid.xy];
    float2 hitMotion = GetMotionVector(hitDepth, uv, FrameBuffer::CameraPreviousViewProjUnjittered[eyeIndex], FrameBuffer::CameraViewProjUnjittered[eyeIndex]);
    float2 depthMotion = MotionVectors[DTid.xy].xy;

    float4 sampleColors[9]; 
    float4 currColor, prevColor, momentA, momentB;

    [loop]
    for (int i = 0; i < 9; ++i)
    {
        float2 offset = TemportalOffsets[i] * FrameBuffer::DynamicResolutionParams2.zw;
        sampleColors[i] = SSRColor.SampleLevel(LinearSampler, uv + offset, 0);
        momentA += sampleColors[i];
        momentB += sampleColors[i] * sampleColors[i];
    }

    float4 mean = momentA / 9.0;
    float4 stdev = sqrt(max(momentB / 9.0 - mean * mean, 0.0));

    currColor = sampleColors[4];
    float4 minColor = mean - stdev * TemporalScale;
    float4 maxColor = mean + stdev * TemporalScale;
    minColor = min(minColor, currColor);
    maxColor = max(maxColor, currColor);

    float2 prevUV = uv - depthMotion;
    float2 rayPrevUV = uv - hitMotion;

    if (any(prevUV < 0.0) && any(prevUV > 1.0) ||
        any(rayPrevUV < 0.0) && any(rayPrevUV > 1.0))
    {
        prevColor = currColor;
    }
    else
    {
        float4 rayProjPrevColor = HistoryRadiance.SampleLevel(LinearSampler, rayPrevUV, 0);
        float4 rayProjDist = (rayProjPrevColor - mean) * stdev;
        float rayProjWeight = exp2(-10.0 * Color::RGBToLuminanceAlternative(rayProjDist));

        float4 depthProjPrevColor = HistoryRadiance.SampleLevel(LinearSampler, prevUV, 0);
        float4 depthProjDist = (depthProjPrevColor - mean) * stdev;
        float depthProjWeight = exp2(-10.0 * Color::RGBToLuminanceAlternative(depthProjDist));

        prevColor = (rayProjPrevColor * rayProjWeight + depthProjPrevColor * depthProjWeight) / (rayProjWeight + depthProjWeight);
        prevColor = clamp(prevColor, minColor, maxColor);
    }

    float BlendWeight = saturate(TemporalWeight * (1.0 - length(hitMotion) * 8) * (1.0 - length(depthMotion) * 8));

    float4 radiance = max(1e-6, lerp(prevColor, currColor, BlendWeight));
    float variance = ComputeTemporalVariance(prevColor.xyz, radiance.xyz) > VARIANCE_THRESHOLD ? 0.0 : 1.0;

    OutputTemporalRadiance[DTid.xy] = radiance;
}