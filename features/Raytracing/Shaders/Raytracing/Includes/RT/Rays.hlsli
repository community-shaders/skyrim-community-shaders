#ifndef RAYS_HLSL
#define RAYS_HLSL

#include "Raytracing/Includes/Types.hlsli"
#include "Raytracing/Includes/Surface.hlsli"
#include "Raytracing/Includes/RT/CommonRT.hlsli"

float3 TraceRayShadow(RaytracingAccelerationStructure scene, Surface surface, float3 direction, inout uint randomSeed)
{
    RayDesc ray;
    bool hasTransmission = any(surface.TransmissionColor) > 0.0f;
    ray.Origin = OffsetRay(surface.Position, surface.GeomNormal, direction, hasTransmission);
    ray.Direction = direction;
    ray.TMin = 0.01f;
    ray.TMax = SHADOW_RAY_TMAX;

    ShadowPayload shadowPayload;
    shadowPayload.missed = 0.0f;
    shadowPayload.randomSeed = randomSeed;
    shadowPayload.transmission = float3(1.0f, 1.0f, 1.0f);

    TraceRay(scene, RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, 0xFF, SHADOW_RAY_HITGROUP_IDX, 0, SHADOW_RAY_MISS_IDX, ray, shadowPayload);
    
    randomSeed = shadowPayload.randomSeed;
    return shadowPayload.transmission * shadowPayload.missed;
}

float3 TraceRayShadowFinite(RaytracingAccelerationStructure scene, Surface surface, float3 direction, float tmax, inout uint randomSeed)
{
    RayDesc ray;
    bool hasTransmission = any(surface.TransmissionColor) > 0.0f;
    ray.Origin = OffsetRay(surface.Position, surface.GeomNormal, direction, hasTransmission);
    ray.Direction = direction;
    ray.TMin = 0.01f;
    ray.TMax = tmax;

    ShadowPayload shadowPayload;
    shadowPayload.missed = 0.0f;
    shadowPayload.randomSeed = randomSeed;
    shadowPayload.transmission = float3(1.0f, 1.0f, 1.0f);

    TraceRay(scene, RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, 0xFF, SHADOW_RAY_HITGROUP_IDX, 0, SHADOW_RAY_MISS_IDX, ray, shadowPayload);
    
    randomSeed = shadowPayload.randomSeed;
    return shadowPayload.transmission * shadowPayload.missed;
}

#endif // RAYS_HLSL