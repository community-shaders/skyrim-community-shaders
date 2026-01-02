#ifndef RAYS_HLSL
#define RAYS_HLSL

#include "Raytracing/Includes/Types.hlsli"
#include "Raytracing/Includes/Surface.hlsli"
#include "Raytracing/Includes/RT/CommonRT.hlsli"

float TraceRayShadow(RaytracingAccelerationStructure scene, Surface surface, float3 direction)
{
    RayDesc ray;
    ray.Origin = surface.Position + surface.GeomNormal * GN_BIAS;
    ray.Direction = direction;
    ray.TMin = 0.01f;
    ray.TMax = SHADOW_RAY_TMAX;

    ShadowPayload shadowPayload;
    shadowPayload.missed = 0.0f;

    TraceRay(scene, RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, 0xFF, SHADOW_RAY_HITGROUP_IDX, 0, SHADOW_RAY_MISS_IDX, ray, shadowPayload);
    return shadowPayload.missed;
}

float TraceRayShadowFinite(RaytracingAccelerationStructure scene, Surface surface, float3 direction, float tmax)
{
    RayDesc ray;
    ray.Origin = surface.Position + surface.GeomNormal * GN_BIAS;
    ray.Direction = direction;
    ray.TMin = 0.01f;
    ray.TMax = tmax;

    ShadowPayload shadowPayload;
    shadowPayload.missed = 0.0f;

    TraceRay(scene, RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, 0xFF, SHADOW_RAY_HITGROUP_IDX, 0, SHADOW_RAY_MISS_IDX, ray, shadowPayload);
    return shadowPayload.missed;
}

#endif // RAYS_HLSL