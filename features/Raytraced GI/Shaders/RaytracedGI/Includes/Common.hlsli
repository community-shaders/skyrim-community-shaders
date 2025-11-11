#ifndef COMMON_HLSI
#define COMMON_HLSI

#include "Common/Game.hlsli"
#include "RaytracedGI/Includes/Types.hlsli"

#define M_TO_GAME_UNIT (1.0f / (GAME_UNIT_TO_M))

#define MAX_DEPTH 2
#define SHADOW_MAX_DEPTH 1

#define COMMON_RAY_HIT_IDX 0
#define COMMON_RAY_MISS_IDX 0

#define SHADOW_RAY_HITGROUP_IDX 1
#define SHADOW_RAY_MISS_IDX 1

#define REFLECTION_RAY_HITGROUP_IDX 2
#define REFLECTION_RAY_MISS_IDX 2

uint InitRandomSeed(uint2 coord, uint2 size, uint frameCount)
{
    // Simple hash function
    uint seed = coord.x + coord.y * size.x + frameCount * 719393;
    return seed;
}

uint PCGHash(uint seed)
{
    uint state = seed * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float Random(inout uint seed)
{
    seed = PCGHash(seed);
    return float(seed) / 4294967296.0; // Divide by 2^32
}

void CreateOrthonormalBasis(in float3 normal, out float3 tangent, out float3 bitangent)
{
    //float sign = copysign(1.0f, normal.z);
    float sign = normal.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (sign + normal.z);
    float b = normal.x * normal.y * a;
    
    tangent = float3(1.0 + sign * normal.x * normal.x * a, sign * b, -sign * normal.x);
    bitangent = float3(b, sign + normal.y * normal.y * a, -normal.y);
}

float3 TangentSample(inout uint randomSeed)
{
    float r1 = Random(randomSeed);
    float r2 = Random(randomSeed);
    
    float phi = 2.0 * 3.14159265359 * r1;
    
    float cosTheta = sqrt(1.0 - r2);
    float sinTheta = sqrt(r2);

    return float3(
        cos(phi) * sinTheta,
        sin(phi) * sinTheta,
        cosTheta
    );
}

float3 TangentSampleScaled(inout uint randomSeed, float roughness)
{
    float r1 = Random(randomSeed);
    float r2 = Random(randomSeed);

    float phi = 2.0 * 3.14159265359 * r1;
    
    float alpha = roughness * roughness;
    float cosTheta = sqrt((1.0 - r2) * alpha + (1.0 - alpha));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    return float3(
        cos(phi) * sinTheta,
        sin(phi) * sinTheta,
        cosTheta
    );
}

float3 SampleHemisphere(float3 normal, float3 tangentSample)
{   
    float3 tangent;
    float3 bitangent;
    CreateOrthonormalBasis(normal, tangent, bitangent);

    return tangent * tangentSample.x +
           bitangent * tangentSample.y +
           normal * tangentSample.z;
}


float3 TraceRayDiffuse(RaytracingAccelerationStructure scene, float3 origin, float3 direction, uint currentDepth, inout uint randomSeed)
{
    float3 tangentSample = TangentSample(randomSeed);
    float3 randomDirection = SampleHemisphere(direction, tangentSample);
            
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = randomDirection;
    ray.TMin = 0.01f;
    ray.TMax = 1e30;
    
    DiffusePayload diffusePayload;
    diffusePayload.color = float3(0, 0, 0);
    diffusePayload.data = PayloadData::Create(false, currentDepth + 1, randomSeed);

    TraceRay(scene, RAY_FLAG_NONE, 0xFF, COMMON_RAY_HIT_IDX, 0, COMMON_RAY_MISS_IDX, ray, diffusePayload);
    return diffusePayload.color * 2.0f;
}

float TraceRayShadow(RaytracingAccelerationStructure scene, float3 origin, float3 direction, inout uint randomSeed)
{
    float3 tangentSample = TangentSampleScaled(randomSeed, 0.5f);
    float3 randomDirection = SampleHemisphere(direction, tangentSample);
    
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = randomDirection;
    ray.TMin = 0.01f;
    ray.TMax = 1e30;

    ShadowPayload shadowPayload;
    shadowPayload.missed = 0.0f;
        
    TraceRay(scene,  RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, 0xFF, SHADOW_RAY_HITGROUP_IDX, 0, COMMON_RAY_MISS_IDX, ray, shadowPayload);
    return shadowPayload.missed;
}

float4 TraceRaySpecular(RaytracingAccelerationStructure scene, float3 origin, float3 direction, uint currentDepth, inout uint randomSeed, float roughness)
{
    float3 tangentSample = TangentSampleScaled(randomSeed, roughness);
    float3 randomDirection = SampleHemisphere(direction, tangentSample);

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = randomDirection;
    ray.TMin = 0.01f;
    ray.TMax = 1e30;
    
    SpecularPayload specularPayload;
    specularPayload.color = float3(0, 0, 0);
    specularPayload.distance = 0.0f;
    specularPayload.data = PayloadData::Create(false, currentDepth + 1, randomSeed);

    TraceRay(scene, RAY_FLAG_NONE, 0xFF, REFLECTION_RAY_HITGROUP_IDX, 0, REFLECTION_RAY_MISS_IDX, ray, specularPayload);
    return float4(specularPayload.color, specularPayload.distance);
}  
#endif