#ifndef RAYS_HLSL
#define RAYS_HLSL

#include "Raytracing/Includes/Types.hlsli"
#include "Raytracing/Includes/Surface.hlsli"
#include "Raytracing/Includes/RT/CommonRT.hlsli"

// https://github.com/NVIDIAGameWorks/dxvk-remix/blob/main/src/dxvk/shaders/rtx/concept/ray/ray_utilities.h

/*
* Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

// This ray offsetting method is inspired by the "A Fast and Robust Method for Avoiding Self-Intersection"
// article from the Ray Tracing Gems book. The original implementation from that article was found to be
// flawed in multiple ways, but the logic behind it is solid.
//
// When we hit a triangle and compute a position on that triangle from the vertices and the barycentrics,
// the resulting position is inexact, it has some error. You can think of that as a "cloud" of points
// around the triangle plane, and the position can be anywhere inside that cloud, on either side of
// the triangle. In order to avoid self-intersection, we need to apply an offset along the triangle's
// geometric normal that is larger in magnitude than the thickness of this error cloud.
// The magnitude of this error depends primarily on the magnitude of the vertex positions, IOW,
// the further away our triangle is from the world origin, the larger the error will be.
//
// So, we take the maximum magnitude of the position and multiply it by some constant.
// This is different from the code in the RTG article which was dealing with position components
// independently, but that just distorts the normal direction for most triangles, and fails
// on triangles that are coplanar to one of the major planes like Y=0 in particular.
//
// The reason why dealing with per-component errors fails on triangles coplanar to a major plane
// is apparently in the ray intersection math that is happening inside the GPU. At least the approach
// documented in the Vulkan ray tracing spec (*) is based on transforming the primitive positions to
// ray space, which involves multiplying the positions by a matrix. That matrix propagates
// the error from reconstructing the X and Z positions (in case of Y=0 plane) into the ray T.
// * https://www.khronos.org/registry/vulkan/specs/1.1-khr-extensions/html/chap33.html#ray-intersection-candidate-determination


// Calculates the *scaled* approximate error of a float32 position.
// The error is scaled by the 1/kFloatULP constant defined in the calculateRayOffset(...) function,
// which is moved there for efficiency: we don't want to do extra multiplications when combining
// errors from different positions.
// This function should be used on every position value in the chain of transforms, like so:
//
//   float error = calculatePositionError(objectSpacePosition);
//   float3 worldSpacePosition = mul(objectToWorld, objectSpacePosition);
//   error = max(error, calculatePositionError(worldSpacePosition));       <-- update the error
//   ...
//   float3 offset = calculateRayOffset(error, triangleNormal);
//   float3 rayOrigin = worldSpacePosition + offset;
//
float CalculatePositionError(float3 p)
{
    const float maxAbsComponent = max(abs(p.x), max(abs(p.y), abs(p.z)));
    return maxAbsComponent;
}

// This function calculates a ray offset in the direction of the normal, given the error 
// previously computed with the calculatePositionError(p) function above.
// If the offset point is desired on the "inside" of a surface (for example when dealing 
// with translucency or double sided geometry), invert the normal passed in beforehand.
float3 CalculateRayOffset(float positionError, float3 triangleNormal)
{
    // A single ULP (Unit in the Last Place, or Unit of Least Precision) of 32-bit floats, calculated as
    //   ((asfloat(asuint(x) + 1) - x) / x)
    // The actual value is smaller for numbers that are not powers of 2, so we use the largest ULP.
    // It can also be substantially larger for denormals, but we don't really care about them.
    const float kFloatULP = 0.00000011920928955078125; // pow(2.0, -23.0);

    // The original RTG article found that there is some "baseline" error coming from non-position sources,
    // and we account for that by adding a max(kOrigin, ...) term where kOrigin is the point on the
    // error plot in the article where the error switches from plateau to linear dependency on position.
    const float kOrigin = 1.0f / 1024.0f;

    // The kOffsetScale value was determined experimentally as the smallest value that doesn't result in
    // self-intersections in practice. The article claimed that the relative error is in the order of 10^-7,
    // but that is hard to believe because that's just 1 ULP of float32. At the same time, the article
    // was effectively multiplying the normal by 1 / 32768.0 with some cryptic integer math, and that
    // is often too large.
    const float kOffsetScale = 4.0;

    return (max(kOrigin, positionError) * (kFloatULP * kOffsetScale)) * triangleNormal;
}

float3 OffsetRay(float3 position, float3 normal, bool hasTransmission = false)
{
    float3 offset = CalculateRayOffset(CalculatePositionError(position), normal);
    if (hasTransmission)
        offset = -offset;
    return position + offset;
}

float3 TraceRayShadow(RaytracingAccelerationStructure scene, Surface surface, float3 direction, inout uint randomSeed)
{
    RayDesc ray;
    bool hasTransmission = any(surface.TransmissionColor) > 0.0f && dot(surface.FaceNormal, direction) < 0.0f;
    ray.Origin = OffsetRay(surface.Position, surface.FaceNormal, hasTransmission);
    ray.Direction = direction;
    ray.TMin = 0.0f;
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
    bool hasTransmission = any(surface.TransmissionColor) > 0.0f && dot(surface.FaceNormal, direction) < 0.0f;
    ray.Origin = OffsetRay(surface.Position, surface.FaceNormal, hasTransmission);
    ray.Direction = direction;
    ray.TMin = 0.0f;
    ray.TMax = tmax;

    ShadowPayload shadowPayload;
    shadowPayload.missed = 0.0f;
    shadowPayload.randomSeed = randomSeed;
    shadowPayload.transmission = float3(1.0f, 1.0f, 1.0f);

    TraceRay(scene, RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, 0xFF, SHADOW_RAY_HITGROUP_IDX, 0, SHADOW_RAY_MISS_IDX, ray, shadowPayload);
    
    randomSeed = shadowPayload.randomSeed;
    return shadowPayload.transmission * shadowPayload.missed;
}

Payload SampleSubsurface(RaytracingAccelerationStructure scene, const float3 samplePosition, const float3 surfaceNormal, const float tmax, inout uint randomSeed)
{
    RayDesc ray;
    ray.Origin = samplePosition;
    ray.Direction = -surfaceNormal; // Shooting ray towards the surface
    ray.TMin = 0.0f;
    ray.TMax = tmax;

    Payload payload;
    payload.hitDistance = -1.0f;
    payload.primitiveIndex = 0;
    payload.PackBarycentrics(float2(0.0f, 0.0f));
    payload.PackInstanceGeometryIndex(0, 0);
    payload.randomSeed = randomSeed;

    TraceRay(scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, DIFFUSE_RAY_HITGROUP_IDX, 0, DIFFUSE_RAY_MISS_IDX, ray, payload);
    randomSeed = payload.randomSeed;

    return payload;
}

#endif // RAYS_HLSL