#include "ScreenSpaceReflections/ssr_common.hlsli"

#define SAMPLE_BATCH_SIZE 4
#define MAX_DISTANCE 4096.f

Texture2D<float4> ScreenColorTextureMips : register(t3);
Texture2D<float> DepthTexture : register(t4);
Texture2D<float> LinearDepthTextureMips : register(t5);

RWTexture2D<float4> SSRColorOutput : register(u0);
RWTexture2D<float> SSRHitDistanceOutput : register(u1);

// cbuffer PerFrame : register(b1)
// {
//     uint MaxSteps;
//     uint NumRays;
//     uint Glossy;
//     uint EyeIndex;
//     float RoughnessMask;
//     float3 pad;
// };

#define MaxSteps 16
#define NumRays 4
#define Glossy 1
#define EyeIndex 0
#define RoughnessMask 0.99f

struct ssrRay
{
    float3 startVS;
    float3 stepVS;
    float maxDelta;
};

ssrRay InitRay(float3 rayStartWS, float3 dirWS, float depth, float maxDistance, uint eyeIndex, out float3 debug)
{
    ssrRay ray;
    debug = float3(0, 0, 0);

    float3 dirVS = FrameBuffer::WorldToView(dirWS, false, eyeIndex);
    float rayDist = dirVS.z < 0.0 ? min(-depth / dirVS.z, maxDistance) : maxDistance;
    float3 rayEndWS = rayStartWS + dirWS * rayDist;

    ray.startVS = FrameBuffer::WorldToView(rayStartWS, true, eyeIndex);
    float3 rayEndVS = FrameBuffer::WorldToView(rayEndWS, true, eyeIndex);

    float3 rayDepth = ray.startVS + dirVS * (depth - ray.startVS.z);
    ray.stepVS = rayEndVS - ray.startVS;

    ray.maxDelta = max(
        length(ray.stepVS),
        (ray.startVS.z - rayDepth.z) * 4.f
    );

    return ray;
}

bool RayMarch(float3 rayStartWS, float3 dirWS, float depth,
    float roughness, float maxDistance, uint maxSteps, float startMipLevel, float offset,
    out float3 hitPointUVz, out float mipLevel, out float3 debug, uint eyeIndex = 0)
{
    float3 rayEndWS = rayStartWS + dirWS * 2 * maxDistance;
    float3 rayStartVS = FrameBuffer::WorldToView(rayStartWS, true, eyeIndex);
    float3 rayEndVS = FrameBuffer::WorldToView(rayEndWS, true, eyeIndex);
    float3 rayStepVS = rayEndVS - rayStartVS;

    float step = 1.0 / maxSteps;

    rayStepVS *= step;

    float maxDelta = max(
        length(rayStepVS),
        (rayStartVS.z - depth)
    );

    float3 currentVS = rayStartVS + rayStepVS * 0.5f * offset;

    for (uint i = 0; i < maxSteps; ++i)
    {
        currentVS += rayStepVS;
        float rayDepth = currentVS.z;
        if (rayDepth < 0.0 || rayDepth > maxDistance)
        {
            debug = float3(0, 0, 1); // Out of bounds
            return false;
        }
        float2 uv = FrameBuffer::ViewToUV(currentVS, eyeIndex);
        float sampleDepth = LinearDepthTextureMips.SampleLevel(LinearSampler, uv, startMipLevel).x;
        float depthDelta = rayDepth - sampleDepth;
        if (abs(depthDelta - maxDelta) < maxDelta)
        {
            hitPointUVz = float3(uv, sampleDepth);
            mipLevel = startMipLevel;
            debug = float3(uv, 0);
            return true;
        }
    }

    return false;
}

[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID)
{
    float4 outColor = float4(0, 0, 0, 0);
    
    float2 uv = float2(DTid.xy + 0.5) * SharedData::BufferDim.zw;
    uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);
	uv *= FrameBuffer::DynamicResolutionParams2.xy;  // Adjust for dynamic res
	uv = Stereo::ConvertFromStereoUV(uv, eyeIndex);

    float3 normalVS;
    float roughness;
    GetNormalRoughness(DTid.xy, normalVS, roughness);

    float3 N = normalize(mul(FrameBuffer::CameraViewInverse[eyeIndex], float4(normalVS, 0)).xyz);

    // if (roughness > RoughnessMask)
    // {
    //     SSRColorOutput[DTid.xy] = float4(0, 0, 0, 0);
    //     return;
    // }

    float depth = DepthTexture[DTid.xy].x;
    float4 positionWS = float4(2 * float2(uv.x, -uv.y + 1) - 1, depth, 1);
	positionWS = mul(FrameBuffer::CameraViewProjInverse[EyeIndex], positionWS);
	positionWS.xyz = positionWS.xyz / positionWS.w;

    depth = SharedData::GetScreenDepth(depth);

    const float3 V = normalize(positionWS.xyz);

    float a = roughness * roughness;
    float a2 = a * a;
    float NdotV = saturate(dot(N, V));
    float G_SmithV = 2 * NdotV / (NdotV + sqrt(a2 + (1 - a2) * NdotV * NdotV));

    float closestDepthSqr = MAX_DISTANCE * MAX_DISTANCE;
    uint maxSteps = MaxSteps;
    uint numRays = NumRays;

    float3 debug = float3(0, 0, 0);

    if (NumRays > 1)
    {
        float2 noise;
        noise.x = Random::InterleavedGradientNoise(SharedData::ConvertUVToSampleCoord(uv, EyeIndex).xy, SharedData::FrameCountAlwaysActive);
        noise.y = Random::InterleavedGradientNoise(SharedData::ConvertUVToSampleCoord(uv, EyeIndex).xy, SharedData::FrameCountAlwaysActive * 369);

        uint2 randomUint = Random::pcg3d(int3(SharedData::ConvertUVToSampleCoord(uv, EyeIndex).xy, SharedData::FrameCountAlwaysActive)).xy;

        float3x3 tangentBasis = GetTangentBasis(N);
        float3 tangentV = mul(tangentBasis, V);

        float count = 0;

        if (roughness < 0.1f)
        {
            maxSteps = min(maxSteps * numRays, 24u);
            numRays = 1;
        }

        for (uint i = 0; i < numRays; ++i)
        {
            float stepOffset = noise.x - 0.5f;
            float2 xi = Hammersley16(i, numRays, randomUint);
            float3 H = mul(ImportanceSampleGGX(xi, roughness), tangentBasis);
            float3 L = normalize(2 * dot(-V, H) * H + V);

            float3 hitUVz;
            float mipLevel = 0.0f;

            if (roughness < 0.1f)
            {
                L = reflect(V, N);
            }

            bool hit = RayMarch(positionWS.xyz, L, depth, roughness, depth, maxSteps, 0.0f, stepOffset, hitUVz, mipLevel, debug, EyeIndex);

            if (hit)
            {
                closestDepthSqr = min(closestDepthSqr, hitUVz.z * hitUVz.z);

                float2 hitUV = hitUVz.xy;
                float4 hitColor = ScreenColorTextureMips.SampleLevel(LinearSampler, hitUV, mipLevel);
                outColor += hitColor;
            }
        }

        outColor /= max(numRays, 0.0001f);
    } 
    else
    {
        float stepOffset = Random::InterleavedGradientNoise(SharedData::ConvertUVToSampleCoord(uv, EyeIndex).xy, SharedData::FrameCountAlwaysActive) - 0.5f;
        float3 hitUVz;
        float3 L;
        if (Glossy)
        {
            float2 xi = Hammersley16(0, 1, Random::pcg3d(int3(SharedData::ConvertUVToSampleCoord(uv, EyeIndex).xy, SharedData::FrameCountAlwaysActive)).xy);
            float3x3 tangentBasis = GetTangentBasis(N);
            float3 tangentV = mul(tangentBasis, V);

            float3 H = mul(ImportanceSampleGGX(xi, roughness), tangentBasis);
            L = normalize(2 * dot(-V, H) * H + V);
        } 
        else 
        {
            L = reflect(V, N);
        }

        float mipLevel = 0.0f;
        bool hit = RayMarch(positionWS.xyz, L, depth, roughness, depth, maxSteps, 0.0f, stepOffset, hitUVz, mipLevel, debug, EyeIndex);

        debug = L;

        if (hit)
        {
            closestDepthSqr = hitUVz.z * hitUVz.z;

            float2 hitUV = hitUVz.xy;
            if (hitUV.x < 0 || hitUV.x > 1 || hitUV.y < 0 || hitUV.y > 1)
            {
                outColor = float4(0, 0, 0, 0);
            }
            float4 hitColor = ScreenColorTextureMips.SampleLevel(LinearSampler, hitUV, mipLevel);
            outColor = hitColor;
        }
    }
    // SSRColorOutput[DTid.xy] = float4(0,0,0, 0);
    SSRColorOutput[DTid.xy] = outColor;
    // SSRColorOutput[DTid.xy] = float4(debug, 1);
    SSRHitDistanceOutput[DTid.xy] = sqrt(closestDepthSqr);
}