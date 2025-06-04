#include "ScreenSpaceReflections/ssr_common.hlsli"

Texture2D<float4> SSRColorTexture : register(t0);
Texture2D<float4> HitPDFTexture : register(t1);
Texture2D<float> DepthTextureMips : register(t5);
Texture2DArray<float> NoiseTexture : register(t6);

RWTexture2D<float4> SpatialOutput : register(u0);

cbuffer SSRCB : register(b1)
{
    uint MaxSteps;
    uint NumRays;
    uint Glossy;
    uint SpatialFilterSteps;
    float RoughnessMask;
    float3 pad;
};

float GetEdgeStoppNormalWeight(float3 normal_p, float3 normal_q, float sigma)
{
    return pow(max(dot(normal_p, normal_q), 0.0), sigma);
}

float GetEdgeStopDepthWeight(float x, float m, float sigma)
{
    float a = length(x - m) / sigma;
    a *= a;
    return exp(-0.5 * a);
}

[numthreads(16, 16, 1)] void main(uint3 DTid : SV_DispatchThreadID)
{
    float2 uv = float2(DTid.xy + 0.5) * SharedData::BufferDim.zw;
    uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);
	uv *= FrameBuffer::DynamicResolutionParams2.xy;  // Adjust for dynamic res
	uv = Stereo::ConvertFromStereoUV(uv, eyeIndex);

    uint2 pixelCoord = SharedData::ConvertUVToSampleCoord(uv, eyeIndex).xy;
    float sceneDepth = DepthTextureMips.SampleLevel(LinearSampler, uv, 0);

    if (sceneDepth <= 0.0f)
    {
        SpatialOutput[DTid.xy] = float4(0, 0, 0, 0);
        return;
    }

    float3 normalVS;
    float roughness;
    GetNormalRoughness(DTid.xy, normalVS, roughness);

    float3 worldNormal = normalize(mul(FrameBuffer::CameraViewInverse[eyeIndex], float4(normalVS, 0)).xyz);

    float numWeight;
    float4 spartialColor;

    float2 noise;
    if (SharedData::FrameCount)  // Test if TAA
    {
        uint FrameCountMod64 = uint(fmod(SharedData::FrameCount, 64));
        uint FrameCountMod64_2 = uint(fmod(SharedData::FrameCount + 32, 64));
        noise.x = NoiseTexture[uint3(SharedData::ConvertUVToSampleCoord(uv, eyeIndex).xy % 128, FrameCountMod64)].x;
        noise.y = NoiseTexture[uint3(SharedData::ConvertUVToSampleCoord(uv, eyeIndex).xy % 128, FrameCountMod64_2)].x;
    }
    else
    {
        noise.x = Random::InterleavedGradientNoise(SharedData::ConvertUVToSampleCoord(uv, eyeIndex).xy, 0);
        noise.y = Random::InterleavedGradientNoise(SharedData::ConvertUVToSampleCoord(uv, eyeIndex).xy, 64);
    }

    uint2 uintRandom = 0x10000 * noise;

    [loop]
    for (int i = 0; i < SpatialFilterSteps; ++i)
    {
        float2 offsetUV = uv + (kStackowiakSampleSet4[i] * SharedData::BufferDim.zw);

        float offsetDepth = DepthTextureMips.SampleLevel(LinearSampler, offsetUV, 0);
        float3 offsetNormalVS;
        float offsetRoughness;
        GetNormalRoughness(offsetUV, offsetNormalVS, offsetRoughness);
        float3 offsetWorldNormal = normalize(mul(FrameBuffer::CameraViewInverse[eyeIndex], float4(offsetNormalVS, 0)).xyz);
        float4 hitColor = SSRColorTexture.SampleLevel(LinearSampler, offsetUV, 0);

        float depthWeight = GetEdgeStopDepthWeight(sceneDepth, offsetDepth, 0.01f);
        float normalWeight = GetEdgeStoppNormalWeight(worldNormal, offsetWorldNormal, 64);
        float weight = depthWeight * normalWeight;
        numWeight += weight;
        spartialColor += float4(hitColor.rgb * weight, hitColor.a);
    }
    spartialColor /= max(numWeight, 1e-6f);

    SpatialOutput[DTid.xy] = spartialColor;
}