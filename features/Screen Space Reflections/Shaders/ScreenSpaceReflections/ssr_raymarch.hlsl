#include "ScreenSpaceReflections/ssr_common.hlsli"

#define SAMPLE_BATCH_SIZE 4
#define MAX_DISTANCE 8192.0f

Texture2D<float4> ScreenColorTextureMips : register(t3);
Texture2D<float> DepthTextureMips : register(t4);

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

#define MaxSteps 8
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

ssrRay InitRay(float3 rayStartWS, float3 dirWS, float depth, float maxDistance, uint eyeIndex)
{
    ssrRay ray;

    float3 dirVS = FrameBuffer::WorldToView(dirWS, false, eyeIndex);
    float rayDist = dirVS.z < 0.0 ? min(-depth / dirVS.z, maxDistance) : maxDistance;
    float3 rayEndWS = rayStartWS + dirWS * rayDist;

    ray.startVS = FrameBuffer::WorldToView(rayStartWS, true, eyeIndex);
    float3 rayEndVS = FrameBuffer::WorldToView(rayEndWS, true, eyeIndex);

    float3 rayDepth = ray.startVS + FrameBuffer::WorldToView(float3(0, 0, rayDist), true, eyeIndex);
    ray.stepVS = rayEndVS - ray.startVS;

    ray.maxDelta = max(
        abs(ray.stepVS.z),
        (ray.startVS.z - rayDepth.z) * 4.f
    );

    return ray;
}

bool RayMarch(float3 rayStartWS, float3 dirWS, float depth, float roughness, float maxDistance, uint maxSteps, float startMipLevel, float offset, out float3 hitPointUVz, out float mipLevel, uint eyeIndex = 0)
{
    ssrRay ray = InitRay(rayStartWS, dirWS, depth, maxDistance, eyeIndex);
    const float3 rayStartVS = ray.startVS;
    const float3 rayStepVS = ray.stepVS;

    float3 rayStartUVz = float3(FrameBuffer::ViewToUV(rayStartVS, eyeIndex), rayStartVS.z);
    float3 rayStepUVz = float3(FrameBuffer::ViewToUV(rayStepVS, eyeIndex), rayStepVS.z);

    const float step = 1.0 / maxSteps;
    float maxDelta = ray.maxDelta * step;

    float lastDiff = 0.0;
    mipLevel = startMipLevel;

    rayStepUVz *= step;
    float3 rayUVz = rayStartUVz + rayStepUVz * offset;

    float multipleSampleDepthDiff[SAMPLE_BATCH_SIZE];
	bool multipleSampleHit[SAMPLE_BATCH_SIZE];
    bool multipleSampleUncertain[SAMPLE_BATCH_SIZE];

    bool foundHit = false;
    bool uncertainHit = false;

    uint i;

    for (i = 0; i < maxSteps; i += SAMPLE_BATCH_SIZE)
    {
        float3 samplesUVz[SAMPLE_BATCH_SIZE];
        float samplesMip[SAMPLE_BATCH_SIZE];

        [unroll]
        for (uint j = 0; j < SAMPLE_BATCH_SIZE; ++j)
        {
            samplesUVz[j] = rayUVz + (float(i + j + 1) * rayStepUVz);
        }

        samplesMip[0] = mipLevel;

        [unroll]
        for (uint j = 1; j < SAMPLE_BATCH_SIZE; ++j)
        {
            mipLevel += (4.0 / maxSteps) * roughness;
            samplesMip[j] = mipLevel;
        }

        float sampleDepths[SAMPLE_BATCH_SIZE];

        [unroll]
        for (uint j = 0; j < SAMPLE_BATCH_SIZE; ++j)
        {
            sampleDepths[j] = DepthTextureMips.SampleLevel(LinearSampler, samplesUVz[j].xy, samplesUVz[j].z).x;
            multipleSampleDepthDiff[j] = samplesUVz[j].z - sampleDepths[j];
            multipleSampleHit[j] = abs(multipleSampleDepthDiff[j] + maxDelta) < maxDelta;
            multipleSampleUncertain[j] = multipleSampleDepthDiff[j] + maxDelta < -maxDelta;

            foundHit = foundHit || multipleSampleHit[j];
            uncertainHit = uncertainHit || (multipleSampleUncertain[j] && !multipleSampleHit[j]);
        }

        [branch]
        if (foundHit)
        {
            break;
        }

        lastDiff = multipleSampleDepthDiff[SAMPLE_BATCH_SIZE - 1];
    }

    [branch]
    if (foundHit)
    {
        float depthDiff0 = multipleSampleDepthDiff[2];
        float depthDiff1 = multipleSampleDepthDiff[3];
        float time0 = 3;

        if (multipleSampleHit[2])
        {
            time0 = 2;
            depthDiff0 = multipleSampleDepthDiff[1];
            depthDiff1 = multipleSampleDepthDiff[2];
        }
        if (multipleSampleHit[3])
        {
            time0 = 1;
            depthDiff0 = multipleSampleDepthDiff[0];
            depthDiff1 = multipleSampleDepthDiff[1];
        }
        if (multipleSampleHit[0])
        {
            time0 = 0;
            depthDiff0 = lastDiff;
            depthDiff1 = multipleSampleDepthDiff[0];
        }

        time0 += float(i);
        float time1 = time0 + 1.0;

        float timeLerp = saturate(depthDiff0) / (depthDiff0 - depthDiff1);
        float hitTime = lerp(time0, time1, timeLerp);

        hitPointUVz = rayUVz + (hitTime * rayStepUVz);
    }

    return foundHit;
}

[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID)
{
    float4 outColor = float4(0, 0, 0, 0);
    
    float2 uv = float2(DTid.xy + 0.5) * SharedData::BufferDim.zw;
    float3 N;
    float roughness;
    GetNormalRoughness(DTid.xy, N, roughness);

    if (roughness > RoughnessMask)
    {
        SSRColorOutput[DTid.xy] = float4(0, 0, 0, 0);
        return;
    }

    float depth = SharedData::GetDepth(uv, EyeIndex);
    float4 positionWS = float4(2 * float2(uv.x, -uv.y + 1) - 1, depth, 1);
	positionWS = mul(FrameBuffer::CameraViewProjInverse[EyeIndex], positionWS);
	positionWS.xyz = positionWS.xyz / positionWS.w;

    const float3 V = normalize(positionWS.xyz);

    float a = roughness * roughness;
    float a2 = a * a;
    float NdotV = saturate(dot(N, V));
    float G_SmithV = 2 * NdotV / (NdotV + sqrt(a2 + (1 - a2) * NdotV * NdotV));

    float closestDepthSqr = MAX_DISTANCE * MAX_DISTANCE;
    uint maxSteps = MaxSteps;
    uint numRays = NumRays;

    if (NumRays > 1)
    {
        float2 noise;
        noise.x = Random::InterleavedGradientNoise(uv, SharedData::FrameCountAlwaysActive);
        noise.y = Random::InterleavedGradientNoise(uv, SharedData::FrameCountAlwaysActive * 369);

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
            float3 H = mul(ImportanceSampleGGX(xi, roughness), tangentV);
            float3 L = normalize(2 * dot(V, H) * H - V);

            float3 hitUVz;
            float mipLevel = 0.0f;

            if (roughness < 0.1f)
            {
                L = reflect(-V, N);
            }

            bool hit = RayMarch(positionWS.xyz, L, depth, roughness, MAX_DISTANCE, maxSteps, 0.0f, stepOffset, hitUVz, mipLevel, EyeIndex);

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
        float stepOffset = Random::InterleavedGradientNoise(uv, SharedData::FrameCountAlwaysActive) - 0.5f;
        float3 hitUVz;
        float3 L;
        if (Glossy)
        {
            float2 xi = Hammersley16(0, 1, Random::pcg3d(int3(SharedData::ConvertUVToSampleCoord(uv, EyeIndex).xy, SharedData::FrameCountAlwaysActive)).xy);
            float3x3 tangentBasis = GetTangentBasis(N);
            float3 tangentV = mul(tangentBasis, V);

            float3 H = mul(ImportanceSampleGGX(xi, roughness), tangentV);
            L = normalize(2 * dot(V, H) * H - V);
        } 
        else 
        {
            L = reflect(-V, N);
        }

        float mipLevel = 0.0f;
        bool hit = RayMarch(positionWS.xyz, L, depth, roughness, MAX_DISTANCE, maxSteps, 0.0f, stepOffset, hitUVz, mipLevel, EyeIndex);

        if (hit)
        {
            closestDepthSqr = hitUVz.z * hitUVz.z;

            float2 hitUV = hitUVz.xy;
            float4 hitColor = ScreenColorTextureMips.SampleLevel(LinearSampler, hitUV, mipLevel);
            outColor = hitColor;
        }
    }

    SSRColorOutput[DTid.xy] = outColor;
    SSRHitDistanceOutput[DTid.xy] = sqrt(closestDepthSqr);
}