#ifndef RAYS_HLSI
#define RAYS_HLSI

#include "Raytracing/Includes/Types.hlsli"
#include "Raytracing/Includes/RT/CommonRT.hlsli"

float4 TraceRay(RaytracingAccelerationStructure scene, float3 origin, float3 direction, uint currentDepth, inout uint randomSeed)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.01f;
    ray.TMax = 1e30;
    
    Payload payload;
    payload.color = float4(0, 0, 0, 0);
    payload.data = PayloadData::Create(false, currentDepth + 1, randomSeed);

    TraceRay(scene, RAY_FLAG_NONE, 0xFF, DIFFUSE_RAY_HITGROUP_IDX, 0, DIFFUSE_RAY_MISS_IDX, ray, payload);
    return payload.color;
}

float TraceRayShadow(RaytracingAccelerationStructure scene, float3 origin, float3 direction)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.01f;
    ray.TMax = 1e30;

    ShadowPayload shadowPayload;
    shadowPayload.missed = 0.0f;
        
    TraceRay(scene, RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, 0xFF, SHADOW_RAY_HITGROUP_IDX, 0, SHADOW_RAY_MISS_IDX, ray, shadowPayload);
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
        
    TraceRay(scene, RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, 0xFF, SHADOW_RAY_HITGROUP_IDX, 0, SHADOW_RAY_MISS_IDX, ray, shadowPayload);
    return shadowPayload.missed;
}

#endif // RAYS_HLSI