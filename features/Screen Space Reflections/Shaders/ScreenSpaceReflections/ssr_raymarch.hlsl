// This file is rewritten from AMD's FidelityFX SDK.
//
// Copyright (C) 2024 Advanced Micro Devices, Inc.
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "ScreenSpaceReflections/ssr_common.hlsli"

#if SHARC_UPDATE || SHARC_RENDER
#   define SHARC_ENABLE_64_BIT_ATOMICS 1
#   include "ScreenSpaceReflections/sharc/SharcCommon.h"
#endif

Texture2D<float4> HistoryTexture : register(t0);
Texture2D<float4> MotionVectorTexture : register(t1);
Texture2D<float4> ScreenColorTextureMips : register(t3);
Texture2D<float> DepthTexture : register(t4);
Texture2D<float> DepthTextureMips : register(t5);
Texture2DArray<float> NoiseTexture : register(t6);
#if defined(DYNAMIC_CUBEMAPS)
TextureCube<float3> EnvTexture : register(t7);
TextureCube<float3> EnvReflectionsTexture : register(t8);
#   if defined(SSGI)
Texture2D<float> SsgiAoTexture : register(t9);
#   endif
#   if defined(SKYLIGHTING)
#	    include "Skylighting/Skylighting.hlsli"
Texture3D<sh2> SkylightingProbeArray : register(t10);
Texture2DArray<float3> stbn_vec3_2Dx1D_128x128x64 : register(t11);
#   endif
#endif
Texture2D<float3> AlbedoTexture : register(t12);

RWTexture2D<float4> SSRColorOutput : register(u0);
RWTexture2D<float4> SSRPDFOutput : register(u1);

RWStructuredBuffer<uint2> u_SharcHashEntriesBuffer : register(u2);
RWStructuredBuffer<uint> u_HashCopyOffsetBuffer : register(u3);
RWStructuredBuffer<uint4> u_SharcVoxelDataBuffer : register(u4);
RWStructuredBuffer<uint4> u_SharcVoxelDataBufferPrev : register(u5);

cbuffer SSRCB : register(b1)
{
    uint MaxSteps;
    uint MaxMips;
    uint UseDynamicCubemapsAsFallback;
    float Thickness;
    float NormalBias;
    float BRDFBias;
    float OcclusionStrength;
    float pad;
};

#define HIZ_MAX_ITERATIONS MaxSteps
#define HIZ_MIN_MIP 0
#define FFX_SSSR_FLOAT_MAX 3.402823466e+38
#define FFX_SSSR_DEPTH_HIERARCHY_MAX_MIP MaxMips
#if defined(SSSR_SPECULAR)
#   define SAMPLES_PER_PIXEL 1
#elif SHARC_UPDATE
#   define SAMPLES_PER_PIXEL 1
#else
#   define SAMPLES_PER_PIXEL DIFFUSE_SPP
#endif

float3 ProjectPosition(float3 origin, float4x4 mat)
{
    float4 projected = mul(mat, float4(origin, 1));
    projected.xyz /= projected.w;
    projected.xy = 0.5 * projected.xy + 0.5;
    projected.y = (1 - projected.y);
    return projected.xyz;
}

// Origin and direction must be in the same space and mat must be able to transform from that space into clip space.
float3 ProjectDirection(float3 origin, float3 direction, float3 screen_space_origin, float4x4 mat) 
{
    float3 offsetted = ProjectPosition(origin + direction, mat);
    return offsetted - screen_space_origin;
}

float3 InvProjectPosition(float3 coord, float4x4 mat) 
{
    coord.y = (1 - coord.y);
    coord.xy = 2 * coord.xy - 1;
    float4 projected = mul(mat, float4(coord, 1));
    projected.xyz /= projected.w;
    return projected.xyz;
}

float2 FFX_SSSR_GetMipResolution(float2 screen_dimensions, int mip_level)
{
    return screen_dimensions * pow(0.5, mip_level);
    // uint2 dimensions;
    // uint levels;
    // DepthTextureMips.GetDimensions(mip_level, dimensions.x, dimensions.y, levels);
    // return float2(dimensions.x, dimensions.y);
}

float FFX_SSSR_LoadDepth(int2 pixel_coordinate, int mip)
{
    return DepthTextureMips.Load(int3(pixel_coordinate, mip /* + pc.depth_mip_bias*/)).x;
}

float3 FFX_SSSR_ScreenSpaceToViewSpace(float3 screen_space_position, uint eyeIndex)
{
    return InvProjectPosition(screen_space_position, FrameBuffer::CameraProjInverse[eyeIndex]);
}

void FFX_SSSR_InitialAdvanceRay(float3     origin,
                                float3     direction,
                                float3     inv_direction,
                                float2     current_mip_resolution,
                                float2     current_mip_resolution_inv,
                                float2     floor_offset,
                                float2     uv_offset,
                                out float3 position,
                                out float  current_t)
{
    float2 current_mip_position = current_mip_resolution * origin.xy;

    // Intersect ray with the half box that is pointing away from the ray origin.
    float2 xy_plane = floor(current_mip_position) + floor_offset;
    xy_plane        = xy_plane * current_mip_resolution_inv + uv_offset;

    // o + d * t = p' => t = (p' - o) / d
    float2 t  = xy_plane * inv_direction.xy - origin.xy * inv_direction.xy;
    current_t = min(t.x, t.y);
    position  = origin + current_t * direction;
}

bool FFX_SSSR_AdvanceRay(float3       origin,
                         float3       direction,
                         float3       inv_direction,
                         float2       current_mip_position,
                         float2       current_mip_resolution_inv,
                         uint         current_mip_level,
                         float2       floor_offset,
                         float2       uv_offset,
                         float        surface_z,
                         float        thickness,
                         inout float3 position,
                         inout float  current_t)
{
    // Create boundary planes
    float2 xy_plane        = floor(current_mip_position) + floor_offset;
    xy_plane               = xy_plane * current_mip_resolution_inv + uv_offset;
    float3 boundary_planes = float3(xy_plane, surface_z);

    // Intersect ray with the half box that is pointing away from the ray origin.
    // o + d * t = p' => t = (p' - o) / d
    float3 t = boundary_planes * inv_direction - origin * inv_direction;

    // Prevent using z plane when shooting out of the depth buffer.
#if FFX_SSSR_OPTION_INVERTED_DEPTH
    t.z = direction.z < 0 ? t.z : FFX_SSSR_FLOAT_MAX;
#else
    t.z = direction.z > 0 ? t.z : FFX_SSSR_FLOAT_MAX;
#endif

    // Choose nearest intersection with a boundary.
    float t_min = min(min(t.x, t.y), t.z);

#if FFX_SSSR_OPTION_INVERTED_DEPTH
    // Larger z means closer to the camera.
    bool above_surface = surface_z < position.z;
#else
    // Smaller z means closer to the camera.
    bool above_surface = surface_z > position.z;
#endif

    // Decide whether we are able to advance the ray until we hit the xy boundaries or if we had to clamp it at the surface.
    // We use the asuint comparison to avoid NaN / Inf logic, also we actually care about bitwise equality here to see if t_min is the t.z we fed into the min3 above.
    bool skipped_tile = asuint(t_min) != asuint(t.z) && above_surface;

    // Make sure to only advance the ray if we're still above the surface.
    current_t = above_surface ? t_min : current_t;

    // Advance ray
    position = origin + current_t * direction;

    return skipped_tile;
}

// Requires origin and direction of the ray to be in screen space [0, 1] x [0, 1]
float3 FFX_SSSR_HierarchicalRaymarch(float3 origin, float3 direction, bool is_mirror, float2 screen_size, int most_detailed_mip, float roughness, float thickness,
                                     uint max_traversal_intersections, out bool valid_hit, out uint _num_iters) {
    const float3 inv_direction = abs(direction) > float(1.0e-12) ? float(1.0) / direction : FFX_SSSR_FLOAT_MAX;

    // Start on mip with highest detail.
    int current_mip = most_detailed_mip;

    // Could recompute these every iteration, but it's faster to hoist them out and update them.
    float2 current_mip_resolution     = FFX_SSSR_GetMipResolution(screen_size, current_mip);
    float2 current_mip_resolution_inv = rcp(current_mip_resolution);

    // Offset to the bounding boxes uv space to intersect the ray with the center of the next pixel.
    // This means we ever so slightly over shoot into the next region.
    float2 uv_offset = 0.005 * exp2(most_detailed_mip) / screen_size;
    uv_offset        = direction.xy < 0 ? -uv_offset : uv_offset;

    // Offset applied depending on current mip resolution to move the boundary to the left/right upper/lower border depending on ray direction.
    float2 floor_offset = direction.xy < 0 ? 0 : 1;

    // valid_hit = false;
    // if (direction.z < f32(1.0e-6)) return f32x3(0.0, 0.0, 0.0);

    // Initially advance ray to avoid immediate self intersections.
    float  current_t;
    float3 position;
    FFX_SSSR_InitialAdvanceRay(origin, direction, inv_direction, current_mip_resolution, current_mip_resolution_inv, floor_offset, uv_offset, position, current_t);

    _num_iters                     = uint(0);
    while (_num_iters < max_traversal_intersections && current_mip >= most_detailed_mip) {
        if (any(position.xy > float2(1.0, 1.0)) || any(position.xy < float2(0.0, 0.0))) break;
#ifdef FFX_SSSR_INVERTED_DEPTH_RANGE
        if (position.z < f32(1.0e-6)) break;
#else
        if (position.z > float(1.0) - float(1.0e-6)) break;
#endif

        float2 current_mip_position = current_mip_resolution * position.xy;
        float  surface_z            = FFX_SSSR_LoadDepth(current_mip_position * FrameBuffer::DynamicResolutionParams1.xy, current_mip);
        bool skipped_tile =
            FFX_SSSR_AdvanceRay(origin, direction, inv_direction, current_mip_position, current_mip_resolution_inv, current_mip, floor_offset, uv_offset, surface_z, thickness, position, current_t);
        bool nextMipIsOutOfRange = skipped_tile && (current_mip >= FFX_SSSR_DEPTH_HIERARCHY_MAX_MIP);
        if (!nextMipIsOutOfRange)
        {
            current_mip += skipped_tile ? 1 : -1;
            current_mip_resolution *= skipped_tile ? 0.5 : 2;
            current_mip_resolution_inv *= skipped_tile ? 2 : 0.5;
        }
        ++_num_iters;
    }

    valid_hit = (_num_iters <= max_traversal_intersections);

    return position;
}

float FFX_SSSR_ValidateHit(float3 hit, float2 uv, float3 world_space_ray_direction, float2 screen_size, float depth_buffer_thickness, uint eyeIndex, out float occlusion)
{
    occlusion = 1.f;

    // Reject hits outside the view frustum
    if ((hit.x < 0.0f) || (hit.y < 0.0f) || (hit.x > 1.0f) || (hit.y > 1.0f))
    {
        return 0.0f;
    }

    // Don't lookup radiance from the background.
    int2  texel_coords = int2(screen_size * hit.xy * FrameBuffer::DynamicResolutionParams1.xy);
    float surface_z    = FFX_SSSR_LoadDepth(texel_coords / 2, 1);
#if FFX_SSSR_OPTION_INVERTED_DEPTH
    if (surface_z == 0.0)
    {
#else
    if (surface_z == 1.0)
    {
#endif
        return 0;
    }

    float3 view_space_surface = FFX_SSSR_ScreenSpaceToViewSpace(float3(hit.xy, surface_z), eyeIndex);
    float3 view_space_hit     = FFX_SSSR_ScreenSpaceToViewSpace(hit, eyeIndex);
    float  distance           = length(view_space_surface - view_space_hit);

    // We accept all hits that are within a reasonable minimum distance below the surface.
    // Add constant in linear space to avoid growing of the reflections toward the reflected objects.
    float confidence = 1.0f - smoothstep(0.0f, depth_buffer_thickness, distance);
    confidence *= confidence;

    // We check if we hit the surface from the back, these should be rejected.
    float3 hit_normalVS;
    float hit_roughness;
    GetNormalRoughness(texel_coords, hit_normalVS, hit_roughness);
    float3 hit_normal = normalize(mul(FrameBuffer::CameraViewInverse[eyeIndex], float4(hit_normalVS, 0)).xyz);
    if (dot(hit_normal, world_space_ray_direction) > 0)
    {
        occlusion = distance > depth_buffer_thickness ? 1.f : 0.f;
        return 0;
    }

    // Reject the hit if we didnt advance the ray significantly to avoid immediate self reflection
    float2 manhattan_dist = abs(hit.xy - uv);
    if ((manhattan_dist.x < (2.f / screen_size.x)) && (manhattan_dist.y < (2.f / screen_size.y)))
    {
        occlusion = distance > depth_buffer_thickness ? 1.f : 0.f;
        return 0;
    }

    // Fade out hits near the screen borders
    float2 fov      = 0.01 * float2(screen_size.y / screen_size.x, 1);
    float2 border   = smoothstep(float2(0.0f, 0.0f), fov, hit.xy) * (1 - smoothstep(float2(1.0f, 1.0f) - fov, float2(1.0f, 1.0f), hit.xy));
    float  vignette = border.x * border.y;

    return vignette * confidence;
}

bool IsMirrorReflection(float roughness)
{
    return roughness < 0.1;
}

float3 Sample_GGX_VNDF_Ellipsoid(float3 Ve, float alpha_x, float alpha_y, float U1, float U2) { return SampleGGXVNDF(Ve, alpha_x, alpha_y, U1, U2); }

float3 Sample_GGX_VNDF_Hemisphere(float3 Ve, float alpha, float U1, float U2) { return Sample_GGX_VNDF_Ellipsoid(Ve, alpha, alpha, U1, U2); }

float3x3 CreateTBN(float3 N) {
    float3 U;
    if (abs(N.z) > 0.0) {
        float k = sqrt(N.y * N.y + N.z * N.z);
        U.x     = 0.0;
        U.y     = -N.z / k;
        U.z     = N.y / k;
    } else {
        float k = sqrt(N.x * N.x + N.y * N.y);
        U.x     = N.y / k;
        U.y     = -N.x / k;
        U.z     = 0.0;
    }

    float3x3 TBN;
    TBN[0] = U;
    TBN[1] = cross(N, U);
    TBN[2] = N;
    return transpose(TBN);
}

#define GOLDEN_RATIO 1.61803398875f

float2 SampleRandomVector2DBaked(uint2 pixel, uint index, uint numSamples) {
    // int2   coord = int2(pixel.x & 127u, pixel.y & 127u);
    // float2 xi    = float2(NoiseTexture[uint3(coord, 0)].x, NoiseTexture[uint3(coord, 64)].x);
    // float2 u     = float2(fmod(xi.x + (((int)(pixel.x / 128)) & 0xFFu) * GOLDEN_RATIO, 1.0f), fmod(xi.y + (((int)(pixel.y / 128)) & 0xFFu) * GOLDEN_RATIO, 1.0f));
    // return u;
    int3 seed = int3(pixel.xy, 0);
    seed.z = Random::pcg3d(int3(seed.xy, SharedData::FrameCount)).x;
    uint2 xi = Random::pcg3d(seed).xy / 0x10000;
    float2 E = Hammersley16(index, numSamples, xi);
#if defined(SSSR_SPECULAR)
    E.y = lerp(E.y, 0, BRDFBias);
#endif
    return E;
}

float3 SampleReflectionVector(float3 view_direction, float3 normal, float roughness, int2 dispatch_thread_id, uint index, uint numSamples, out float pdf) {
    if (roughness < 0.001f) {
        pdf = 1.0f;
        return reflect(view_direction, normal);
    }
    float3x3 tbn_transform = CreateTBN(normal);
    float3   view_direction_tbn = mul(-view_direction, tbn_transform);
    float2   u = SampleRandomVector2DBaked(dispatch_thread_id, index, numSamples);
    // float3   sampled_normal_tbn = Sample_GGX_VNDF_Hemisphere(view_direction_tbn, roughness, u.x, u.y);
#if defined(SSSR_SPECULAR)
    float4   sampled_normal_tbn = ImportanceSampleGGX(u, roughness * roughness * roughness * roughness);
#else
    float4   sampled_normal_tbn = CosineSampleHemisphereConcentric(u);
#endif
#ifdef PERFECT_REFLECTIONS
    sampled_normal_tbn.xyz = float3(0, 0, 1); // Overwrite normal sample to produce perfect reflection.
#endif
#if defined(SSSR_SPECULAR)
    float3 reflected_direction_tbn = reflect(-view_direction_tbn, sampled_normal_tbn.xyz);
#else
    float3 reflected_direction_tbn = sampled_normal_tbn.xyz;
#endif
    // Transform reflected_direction back to the initial space.
    float3x3 inv_tbn_transform = transpose(tbn_transform);
    pdf = sampled_normal_tbn.w;
    return mul(reflected_direction_tbn, inv_tbn_transform);
}

float3 ScreenSpaceToWorldSpace(float3 screen_space_position, float4x4 invViewProj)
{
    return InvProjectPosition(screen_space_position, invViewProj);
}

float3 ScreenSpaceToViewSpace(float3 screen_uv_coord, float4x4 invProj)
{
    return InvProjectPosition(screen_uv_coord, invProj);
}

groupshared float4 samples[64][SAMPLES_PER_PIXEL];
groupshared float4 weights[64][SAMPLES_PER_PIXEL];

bool IsInGroup(int2 groupThreadID)
{
    return groupThreadID.x < 8 && groupThreadID.y < 8 && groupThreadID.x >= 0 && groupThreadID.y >= 0;
}

static const int2 offset[4] = {
    int2(-1, 0), int2(1, 0), int2(0, 1), int2(0, -1)
};

float LocalBRDF(float3 V, float3 L, float3 N, float roughness) {
#if defined(SSSR_SPECULAR)  // D_GGX only
    float3 H = normalize(V + L);
    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    float NdotH = saturate(dot(N, H));
    float D = BRDF::D_GGX(roughness, NdotH);
    float G = BRDF::Vis_SmithJointApprox(roughness, NdotV, NdotL);
    return D * G;
#else  // Lambert
    float NdotL = saturate(dot(N, L));
    return NdotL * BRDF::Diffuse_Lambert();
#endif
}

#if SHARC_UPDATE
uint Hash(uint2 pos, uint seed)
{
    uint hash = pos.x + pos.y * 8 + seed * 64;
    hash = hash * 1103515245u + 12345u;
    return hash;
}
bool ShouldProcessPixel(uint2 GroupThreadID, uint FrameCount)
{
    uint hash = Hash(GroupThreadID, FrameCount);
    return (hash % 4) == 0;
}
#endif

[numthreads(8, 8, SAMPLES_PER_PIXEL)] void main(uint3 groupID : SV_GroupID,
                                                uint3 groupThreadID : SV_GroupThreadID,
                                                uint3 DTid : SV_DispatchThreadID)
{
    uint2 screen_size = SharedData::BufferDim.xy;
    uint2 coords = DTid.xy;
#if defined(SSSR_SPECULAR)
    uint sample_id = 0;
#else
    uint sample_id = groupThreadID.z;
#endif
    float3 debug;

    float4 outColor = float4(0, 0, 0, 0);
    float4 outPDF = float4(0, 0, 0, 0);

    float2 uv = float2(coords.xy + 0.5) * SharedData::BufferDim.zw * FrameBuffer::DynamicResolutionParams2.xy;
    uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);

    float3 normalVS;
    float roughness;
    GetNormalRoughness(coords.xy, normalVS, roughness);
    roughness = clamp(roughness, 0.02f, 1.0f);

#if !defined(SSSR_SPECULAR)
    float3 albedo = AlbedoTexture[coords.xy].xyz;
#endif

    bool is_mirror = IsMirrorReflection(roughness);
    int most_detailed_mip = HIZ_MIN_MIP;
    float2 mip_resolution = FFX_SSSR_GetMipResolution(screen_size, most_detailed_mip);
    float z = FFX_SSSR_LoadDepth(uv * mip_resolution * FrameBuffer::DynamicResolutionParams1.xy, most_detailed_mip);
    float3 screen_uv_space_ray_origin = float3(uv, z);
    float3 view_space_ray = ScreenSpaceToViewSpace(screen_uv_space_ray_origin, FrameBuffer::CameraProjInverse[eyeIndex]);
    float3 world_space_normal = normalize(mul(FrameBuffer::CameraViewInverse[eyeIndex], float4(normalVS, 0)).xyz);
    float3 view_space_surface_normal = normalVS;
    float3 view_space_ray_direction = normalize(view_space_ray);
    view_space_ray += view_space_surface_normal * NormalBias * view_space_ray.z * GAME_UNIT_TO_M;
    float pdf;
    float3 view_space_reflected_direction = SampleReflectionVector(view_space_ray_direction, view_space_surface_normal, roughness, coords, sample_id, SAMPLES_PER_PIXEL, pdf);
    screen_uv_space_ray_origin = ProjectPosition(view_space_ray, FrameBuffer::CameraProj[eyeIndex]);
    float3 screen_space_ray_direction = ProjectDirection(view_space_ray, view_space_reflected_direction, screen_uv_space_ray_origin, FrameBuffer::CameraProj[eyeIndex]);
    float3 world_space_reflected_direction = mul(FrameBuffer::CameraViewInverse[eyeIndex], float4(view_space_reflected_direction, 0)).xyz;
    float3 world_space_origin = mul(FrameBuffer::CameraViewInverse[eyeIndex], float4(view_space_ray, 1)).xyz;
    float world_ray_length = 0.0;
    bool valid_ray = all(coords < int2(screen_size * FrameBuffer::DynamicResolutionParams1.xy)) && all(coords >= int2(0, 0));
#if SHARC_UPDATE
    valid_ray = valid_ray && ShouldProcessPixel(coords.xy, SharedData::FrameCount);
#endif
    uint hit_counter = 0;
    float3 hit = float3(0.0, 0.0, 0.0);
    float confidence = 0.0;
    float3 world_space_hit = float3(0.0, 0.0, 0.0);
    float3 world_space_ray = float3(0.0, 0.0, 0.0);

    float depth = DepthTexture[coords.xy].x;
    float4 positionWS = float4(2 * float2(uv.x, -uv.y + 1) - 1, depth, 1);
	positionWS = mul(FrameBuffer::CameraViewProjInverse[eyeIndex], positionWS);
	positionWS.xyz = positionWS.xyz / positionWS.w;

    samples[groupThreadID.x * 8 + groupThreadID.y][sample_id] = 0.f;
    float localWeight = pdf == 0 ? 0 : LocalBRDF(-view_space_ray_direction, view_space_surface_normal, view_space_reflected_direction, roughness) / max(pdf, 1e-4);
    weights[groupThreadID.x * 8 + groupThreadID.y][sample_id] = float4(view_space_surface_normal, localWeight);

#if SHARC_RENDER
    SharcParameters sharcParameters;

    sharcParameters.gridParameters.cameraPosition = FrameBuffer::CameraPosAdjust[0].xyz;
    sharcParameters.gridParameters.sceneScale = GAME_UNIT_TO_M;
    sharcParameters.gridParameters.logarithmBase = SHARC_GRID_LOGARITHM_BASE;
    sharcParameters.gridParameters.levelBias = SHARC_GRID_LEVEL_BIAS;

    sharcParameters.hashMapData.capacity = 0x100000;
    sharcParameters.hashMapData.hashEntriesBuffer = u_SharcHashEntriesBuffer;
#if !SHARC_ENABLE_64_BIT_ATOMICS
    sharcParameters.hashMapData.lockBuffer = u_HashCopyOffsetBuffer;
#endif

    sharcParameters.voxelDataBuffer = u_SharcVoxelDataBuffer;
    sharcParameters.voxelDataBufferPrev = u_SharcVoxelDataBufferPrev;

    SharcHitData hitData;
    hitData.positionWorld = positionWS.xyz + FrameBuffer::CameraPosAdjust[0].xyz;
    hitData.normalWorld = world_space_normal;

    float3 sharcColor = 0;

    if (valid_ray && SharcGetCachedRadiance(sharcParameters, hitData, sharcColor, true))
    {
        samples[groupThreadID.x * 8 + groupThreadID.y][sample_id] = float4(sharcColor, 1);
    }
    else
#endif
    if (valid_ray)
    {
        bool valid_hit;
        bool go_through_thin = false;
        uint numIterations;
        float thickness  = Thickness  + roughness * 10.0;
        hit = FFX_SSSR_HierarchicalRaymarch(screen_uv_space_ray_origin,
                                            screen_space_ray_direction,
                                            is_mirror,
                                            screen_size,
                                            most_detailed_mip,
                                            roughness,
                                            thickness,
                                            HIZ_MAX_ITERATIONS,
                                            valid_hit, numIterations);

        world_space_hit  = ScreenSpaceToWorldSpace(hit, FrameBuffer::CameraViewProjInverse[eyeIndex]);
        world_space_ray  = world_space_hit - world_space_origin.xyz;
        world_ray_length = length(world_space_ray);
        float occlusion;
        confidence       = valid_hit ? FFX_SSSR_ValidateHit(hit,
                                                      uv,
                                                      world_space_ray,
                                                      screen_size,
                                                      thickness,
                                                      eyeIndex,
                                                      occlusion
                                                      )
                                     : 0;
        float3 sampleColor = 0;
        if (confidence > 0.0f)
        {
            // float2 projUV;
            // ReprojectHit(MotionVectorTexture, LinearSampler, hit, eyeIndex, projUV);

            sampleColor = ScreenColorTextureMips.SampleLevel(LinearSampler, hit.xy * FrameBuffer::DynamicResolutionParams1.xy, 0).xyz;

            outPDF.xyz += hit * confidence;
            outPDF.w += pdf * confidence;
        }
        const float NdotV = saturate(dot(normalize(view_space_ray), view_space_surface_normal));
#if defined(DYNAMIC_CUBEMAPS) && !SHARC_UPDATE
        if (UseDynamicCubemapsAsFallback != 0 && (confidence < 0.999f))
        {
#   if defined(SSSR_SPECULAR)            
            const uint sampleMip = 0;
#   else
            const uint sampleMip = 2;
#   endif
            // Fallback to dynamic cubemaps
            float3 envColor = EnvReflectionsTexture.SampleLevel(LinearSampler, world_space_reflected_direction, sampleMip);
#	if defined(SKYLIGHTING)
            if (!SharedData::InInterior)
            {
                float3 positionMS = positionWS.xyz;

                sh2 skylighting = Skylighting::sample(SharedData::skylightingSettings, SkylightingProbeArray, stbn_vec3_2Dx1D_128x128x64, coords.xy, positionMS.xyz, world_space_reflected_direction);
                float3 skylightingNormal = normalize(float3(world_space_normal.xy, max(0, world_space_normal.z)));
                float skylightingDiffuse = SphericalHarmonics::FuncProductIntegral(skylighting, SphericalHarmonics::EvaluateCosineLobe(skylightingNormal)) / Math::PI;
                skylightingDiffuse = saturate(skylightingDiffuse);

                skylightingDiffuse = lerp(1.0, skylightingDiffuse, Skylighting::getFadeOutFactor(positionMS.xyz));

                skylightingDiffuse *= 1.0 + saturate(world_space_normal.z) * (1.0 - SharedData::skylightingSettings.MinDiffuseVisibility);

                skylightingDiffuse = Skylighting::mixDiffuse(SharedData::skylightingSettings, skylightingDiffuse);
#       if defined(SSSR_SPECULAR)
                skylightingDiffuse = GetSpecularOcclusionFromAmbientOcclusion(NdotV, skylightingDiffuse, roughness);
#       endif
                if (skylightingDiffuse < 0.99) {
                    float3 envNoSkyColor = EnvTexture.SampleLevel(LinearSampler, world_space_reflected_direction, sampleMip).xyz;
                    envColor = lerp(envColor, envNoSkyColor, 1 - skylightingDiffuse);
                }
            }
#   endif
            float ao = lerp(1.0, occlusion, OcclusionStrength);
#   if defined(SSGI)
            ao *= 1 - saturate(SsgiAoTexture[coords.xy].x);
#   endif
#   if defined(SSSR_SPECULAR)
            ao = GetSpecularOcclusionFromAmbientOcclusion(NdotV, ao, roughness);
            envColor *= ao;
#   else
            float3 multiBounceAO = Color::MultiBounceAO(albedo, ao);
            envColor *= multiBounceAO;
#   endif
            sampleColor.xyz = lerp(envColor, sampleColor.xyz, confidence);
            confidence = 1;
        }
#endif
        samples[groupThreadID.x * 8 + groupThreadID.y][sample_id] = float4(sampleColor, confidence);

#if SHARC_UPDATE
        if (confidence > 0.99f)
        {
            SharcParameters sharcParameters;

            sharcParameters.gridParameters.cameraPosition = FrameBuffer::CameraPosAdjust[0].xyz;
            sharcParameters.gridParameters.sceneScale = GAME_UNIT_TO_M;
            sharcParameters.gridParameters.logarithmBase = SHARC_GRID_LOGARITHM_BASE;
            sharcParameters.gridParameters.levelBias = SHARC_GRID_LEVEL_BIAS;

            sharcParameters.hashMapData.capacity = 0x100000;
            sharcParameters.hashMapData.hashEntriesBuffer = u_SharcHashEntriesBuffer;
#   if !SHARC_ENABLE_64_BIT_ATOMICS
            sharcParameters.hashMapData.lockBuffer = u_HashCopyOffsetBuffer;
#   endif

            sharcParameters.voxelDataBuffer = u_SharcVoxelDataBuffer;
            sharcParameters.voxelDataBufferPrev = u_SharcVoxelDataBufferPrev;

            SharcState sharcState;
            for (int i = 0; i < 4; ++i) {
                sharcState.cacheIndices[i] = 0;
                sharcState.sampleWeights[i] = 0;
            }
            sharcState.pathLength = 0;

            SharcHitData hitData;
            hitData.positionWorld = positionWS.xyz + FrameBuffer::CameraPosAdjust[0].xyz;
            hitData.normalWorld = world_space_normal;

            float random = Random::InterleavedGradientNoise(uv, SharedData::FrameCount);
            SharcUpdateHit(sharcParameters, sharcState, hitData, sampleColor, random);
        }
#endif
    }
#if !SHARC_UPDATE
    GroupMemoryBarrierWithGroupSync();
#endif

#if defined(SSSR_SPECULAR)
    outColor.xyz = samples[groupThreadID.x * 8 + groupThreadID.y][0].xyz;
    outColor.w = samples[groupThreadID.x * 8 + groupThreadID.y][0].w;
    SSRColorOutput[coords.xy] = outColor;
    SSRPDFOutput[coords.xy] = outPDF;
#elif SHARC_UPDATE
#else

    if (sample_id == 0) {
        outColor = 0.f;
        for (int i = 0; i < SAMPLES_PER_PIXEL; ++i) {
            outColor.xyz += samples[groupThreadID.x * 8 + groupThreadID.y][i].xyz;
            outColor.w += samples[groupThreadID.x * 8 + groupThreadID.y][i].w;
        }
        outColor.xyz /= SAMPLES_PER_PIXEL;
        outColor.w = saturate(outColor.w / SAMPLES_PER_PIXEL);
        SSRColorOutput[coords.xy] = outColor;
        SSRPDFOutput[coords.xy] = outPDF;
    }
#endif
}