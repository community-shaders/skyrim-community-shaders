#ifndef RAYS_HLSI
#define RAYS_HLSI

#include "RaytracedGI/Includes/Types.hlsli"
#include "RaytracedGI/Includes/RT/CommonRT.hlsli"

float3 TraceRayIndirect(RaytracingAccelerationStructure scene, float3 origin, float3 direction, uint currentDepth, inout uint randomSeed)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.01f;
    ray.TMax = 1e30;
    
    IndirectPayload indirectPayload;
    indirectPayload.color = float3(0, 0, 0);
    indirectPayload.data = PayloadData::Create(false, currentDepth + 1, randomSeed);

    TraceRay(scene, RAY_FLAG_NONE, 0xFF, DIFFUSE_RAY_HITGROUP_IDX, 0, DIFFUSE_RAY_MISS_IDX, ray, indirectPayload);
    return indirectPayload.color;
}

float TraceRayShadow(RaytracingAccelerationStructure scene, float3 origin, float3 direction)
{
    /*float3 tangentSample = TangentSampleScaled(randomSeed, 0.5f);
    float3 randomDirection = SampleHemisphere(direction, tangentSample);*/
    
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.01f;
    ray.TMax = 1e30;

    ShadowPayload shadowPayload;
    shadowPayload.missed = 0.0f;
        
    TraceRay(scene,  RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, 0xFF, SHADOW_RAY_HITGROUP_IDX, 0, SHADOW_RAY_MISS_IDX, ray, shadowPayload);
    return shadowPayload.missed;
}

float TraceRayShadowFinite(RaytracingAccelerationStructure scene, float3 origin, float3 direction, float tmax)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.01f;
    ray.TMax = tmax;

    ShadowPayload shadowPayload;
    shadowPayload.missed = 0.0f;
        
    TraceRay(scene, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, 0xFF, SHADOW_RAY_HITGROUP_IDX, 0, SHADOW_RAY_MISS_IDX, ray, shadowPayload);
    return shadowPayload.missed;
}

#endif // RAYS_HLSI