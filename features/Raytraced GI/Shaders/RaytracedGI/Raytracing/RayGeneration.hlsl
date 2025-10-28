#include "Common/FastMath.hlsli"
#include "RaytracedGI/Raytracing/Common.hlsli"

RaytracingAccelerationStructure scene : register(t0);

RWTexture2D<float4> uav : register(u0);

static const float3 camera = float3(0, 1.5, -7);
static const float3 light = float3(0, 200, 0);
static const float3 skyTop = float3(0.24, 0.44, 0.72);
static const float3 skyBottom = float3(0.75, 0.86, 0.93);

[shader("raygeneration")]
void main()
{

    uint2 idx = DispatchRaysIndex().xy;
    float2 size = DispatchRaysDimensions().xy;

    float2 uv = idx / size;
    float3 target = float3((uv.x * 2 - 1) * 1.8 * (size.x / size.y),
                           (1 - uv.y) * 4 - 2 + camera.y,
                           0);

    RayDesc ray;
    ray.Origin = camera;
    ray.Direction = target - camera;
    ray.TMin = 0.001;
    ray.TMax = 1000;

    Payload payload;
    payload.allowReflection = true;
    payload.missed = false;

    TraceRay(scene, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);

    uav[idx] = float4(payload.color, 1);
}
