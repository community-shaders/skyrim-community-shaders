#include "ScreenSpaceReflections/ssr_common.hlsli"

Texture2D<float4> SSRColorTexture : register(t0);
Texture2D<float> DepthTexture : register(t4);

RWTexture2D<float4> BilateralOutput : register(u0);

cbuffer SSRCB : register(b1)
{
    uint MaxSteps;
    uint MaxMips;
    float Thickness;
    float SpatialRadius;
    float NormalBias;
    float TemporalScale;
    float TemporalWeight;
    float BilateralRadius;
    float ColorWeight;
    float DepthWeight;
    float NormalWeight;
    float BRDFBias;
};

static const float Bilateralkernel[25] = { float(1.0f / 256.0f), float(1.0f / 64.0f), float(3.0f / 128.0f), float(1.0f / 64.0f), float(1.0f / 256.0f), float(1.0f / 64.0f), float(1.0f / 16.0f), float(3.0f / 32.0f), float(1.0f / 16.0f), float(1.0f / 64.0f), float(3.0f / 128.0f), float(3.0f / 32.0f), float(9.0f / 64.0f), float(3.0f / 32.0f), float(3.0f / 128.0f), float(1.0f / 64.0f), float(1.0f / 16.0f), float(3.0f / 32.0f), float(1.0f / 16.0f), float(1.0f / 64.0f), float(1.0f / 256.0f), float(1.0f / 64.0f), float(3.0f / 128.0f), float(1.0f / 64.0f), float(1.0f / 256.0f) };
static const int2 BilateralOffset[25] = { int2(-2, -2), int2(-1, -2), int2(0, -2), int2(1, -2), int2(2, -2), int2(-2, -1), int2(-1, -1), int2(0, -1), int2(1, -1), int2(2, -1), int2(-2, 0), int2(-1, 0), int2(0, 0), int2(1, 0), int2(2, 0), int2(-2, 1), int2(-1, 1), int2(0, 1), int2(1, 1), int2(2, 1), int2(-2, 2), int2(-1, 2), int2(0, 2), int2(1, 2), int2(2, 2) };

[numthreads(16, 16, 1)] void main(uint3 DTid : SV_DispatchThreadID)
{
    float2 uv = float2(DTid.xy + 0.5) * SharedData::BufferDim.zw;
    uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);
    uv *= FrameBuffer::DynamicResolutionParams2.xy;  // Adjust for dynamic res
    uv = Stereo::ConvertFromStereoUV(uv, eyeIndex);

    float weightSum = 0.0f;
    float4 colorSum = float4(0.0f, 0.0f, 0.0f, 0.0f);

    float4 color = SSRColorTexture.SampleLevel(LinearSampler, uv, 0);
    float depth = SharedData::GetScreenDepth(DepthTexture.SampleLevel(LinearSampler, uv, 0));
    float3 normal;
    float roughness;
    GetNormalRoughness(DTid.xy, normal, roughness);

    for (int i = 0; i < 25; ++i)
    {
        float2 offsetUV = uv + BilateralOffset[i] * SharedData::BufferDim.zw * BilateralRadius;
        float4 offsetColor = SSRColorTexture.SampleLevel(LinearSampler, offsetUV, 0);
        float offsetDepth = SharedData::GetScreenDepth(DepthTexture.SampleLevel(LinearSampler, offsetUV, 0));
        float3 offsetNormal;
        float offsetRoughness;
        GetNormalRoughnessUV(offsetUV, offsetNormal, offsetRoughness);

        float3 distance = normal - offsetNormal;
        float distance2 = max(dot(distance, distance) / (BilateralRadius * BilateralRadius), 0.0001f);
        float weightNormal = min(1.0f, exp(-distance2 / max(NormalWeight, 0.0001f)));
        float weightDepth = DepthWeight == 0 ? 0.0f : abs(depth - offsetDepth) / max(DepthWeight, 0.0001f);
        float weightColor = abs(Color::RGBToLuminanceAlternative(color.xyz) - Color::RGBToLuminanceAlternative(offsetColor.xyz)) / max(ColorWeight, 0.0001f);
        float weight = exp(0 - max(weightColor, 0) - max(weightDepth, 0)) * weightNormal;

        colorSum += offsetColor * weight * Bilateralkernel[i];
        weightSum += weight * Bilateralkernel[i];
    }

    BilateralOutput[DTid.xy] = colorSum / max(weightSum, 0.0001f);
}