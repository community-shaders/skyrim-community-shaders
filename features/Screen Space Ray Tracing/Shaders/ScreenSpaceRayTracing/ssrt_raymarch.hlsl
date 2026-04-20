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

#include "ScreenSpaceRayTracing/ssrt_common.hlsli"
#include "NRD/NRDReblurSH.hlsli"

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

#if defined(SSRT_SPECULAR)
RWTexture2D<float4> OutSpecRadianceHitDist : register(u0);  // IN_SPEC_RADIANCE_HITDIST (NRD packed)
#else
RWTexture2D<float4> OutSH0 : register(u0);  // IN_DIFF_SH0
RWTexture2D<float4> OutSH1 : register(u1);  // IN_DIFF_SH1
#endif

cbuffer SSRTCB : register(b1)
{
    uint MaxSteps;
    uint MaxMips;
    uint UseDynamicCubemapsAsFallback;
    float Thickness;
    float NormalBias;
    float BRDFBias;
    float OcclusionStrength;
    float CubemapNormalization;

    float2 TexDim;
    float2 RcpTexDim;
    float2 FrameDim;
    float2 RcpFrameDim;
    float HitDistA;
    float HitDistB;
    float HitDistC;
    float HitDistD;
    uint FrameIndex;
    uint TracingMode;  // 0=FULL, 1=FULL_PROBABILISTIC, 2=HALF (checkerboard) — matches NRD Sample
};

#define TRACING_MODE_FULL               0
#define TRACING_MODE_FULL_PROBABILISTIC 1
#define TRACING_MODE_HALF               2

#define HIZ_MAX_ITERATIONS MaxSteps
#define HIZ_MIN_MIP 0
#define SSRT_FLOAT_MAX 3.402823466e+38
#define SSRT_DEPTH_HIERARCHY_MAX_MIP MaxMips

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

float2 SSRT_GetMipResolution(float2 screen_dimensions, int mip_level)
{
    return screen_dimensions * pow(0.5, mip_level);
    // uint2 dimensions;
    // uint levels;
    // DepthTextureMips.GetDimensions(mip_level, dimensions.x, dimensions.y, levels);
    // return float2(dimensions.x, dimensions.y);
}

float SSRT_LoadDepth(int2 pixel_coordinate, int mip)
{
    return DepthTextureMips.Load(int3(pixel_coordinate, mip /* + pc.depth_mip_bias*/)).x;
}

float3 SSRT_ScreenSpaceToViewSpace(float3 screen_space_position, uint eyeIndex)
{
    return InvProjectPosition(screen_space_position, FrameBuffer::CameraProjInverse[eyeIndex]);
}

void SSRT_InitialAdvanceRay(float3     origin,
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

bool SSRT_AdvanceRay(float3       origin,
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
#if SSRT_OPTION_INVERTED_DEPTH
    t.z = direction.z < 0 ? t.z : SSRT_FLOAT_MAX;
#else
    t.z = direction.z > 0 ? t.z : SSRT_FLOAT_MAX;
#endif

    // Choose nearest intersection with a boundary.
    float t_min = min(min(t.x, t.y), t.z);

#if SSRT_OPTION_INVERTED_DEPTH
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
float3 SSRT_HierarchicalRaymarch(float3 origin, float3 direction, bool is_mirror, float2 screen_size, int most_detailed_mip, float roughness, float thickness,
                                     uint max_traversal_intersections, out bool valid_hit, out uint _num_iters) {
    const float3 inv_direction = abs(direction) > float(1.0e-12) ? float(1.0) / direction : SSRT_FLOAT_MAX;

    // Start on mip with highest detail.
    int current_mip = most_detailed_mip;

    // Could recompute these every iteration, but it's faster to hoist them out and update them.
    float2 current_mip_resolution     = SSRT_GetMipResolution(screen_size, current_mip);
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
    SSRT_InitialAdvanceRay(origin, direction, inv_direction, current_mip_resolution, current_mip_resolution_inv, floor_offset, uv_offset, position, current_t);

    _num_iters                     = uint(0);
    while (_num_iters < max_traversal_intersections && current_mip >= most_detailed_mip) {
        if (any(position.xy > float2(1.0, 1.0)) || any(position.xy < float2(0.0, 0.0))) break;
#ifdef SSRT_INVERTED_DEPTH_RANGE
        if (position.z < f32(1.0e-6)) break;
#else
        if (position.z > float(1.0) - float(1.0e-6)) break;
#endif

        float2 current_mip_position = current_mip_resolution * position.xy;
        float  surface_z            = SSRT_LoadDepth(current_mip_position * FrameBuffer::DynamicResolutionParams1.xy, current_mip);
        bool skipped_tile =
            SSRT_AdvanceRay(origin, direction, inv_direction, current_mip_position, current_mip_resolution_inv, current_mip, floor_offset, uv_offset, surface_z, thickness, position, current_t);
        bool nextMipIsOutOfRange = skipped_tile && (current_mip >= SSRT_DEPTH_HIERARCHY_MAX_MIP);
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

float SSRT_ValidateHit(float3 hit, float2 uv, float3 world_space_ray_direction, float2 screen_size, float depth_buffer_thickness, uint eyeIndex, out float occlusion, out bool isBackfaceHit)
{
    occlusion = 1.f;
    isBackfaceHit = false;

    // Reject hits outside the view frustum
    if ((hit.x < 0.0f) || (hit.y < 0.0f) || (hit.x > 1.0f) || (hit.y > 1.0f))
    {
        return 0.0f;
    }

    // Don't lookup radiance from the background.
    int2  texel_coords = int2(screen_size * hit.xy * FrameBuffer::DynamicResolutionParams1.xy);
    float surface_z    = SSRT_LoadDepth(texel_coords / 2, 1);  // depth pyramid base is full-res; mip1 = fullRes/2
#if SSRT_OPTION_INVERTED_DEPTH
    if (surface_z == 0.0)
    {
#else
    if (surface_z == 1.0)
    {
#endif
        return 0;
    }

    float3 view_space_surface = SSRT_ScreenSpaceToViewSpace(float3(hit.xy, surface_z), eyeIndex);
    float3 view_space_hit     = SSRT_ScreenSpaceToViewSpace(hit, eyeIndex);
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
        occlusion = 1 - confidence;
        isBackfaceHit = true;
        return 0;
    }

    // Reject the hit if we didnt advance the ray significantly to avoid immediate self reflection
    float2 manhattan_dist = abs(hit.xy - uv);
    if ((manhattan_dist.x < (2.f / screen_size.x)) && (manhattan_dist.y < (2.f / screen_size.y)))
    {
        occlusion = 1 - confidence;
        return 0;
    }

    return confidence;
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

float2 SampleRandomVector2DBaked(uint2 pixel) {
    int3 seed = int3(pixel.xy, 0);
    seed.z = Random::pcg3d(int3(seed.xy, SharedData::FrameCount)).x;
    uint2 xi = Random::pcg3d(seed).xy / 0x10000;
    float2 E = Hammersley16(0, 1, xi);
#if defined(SSRT_SPECULAR)
    E.y = lerp(E.y, 0, BRDFBias);
#endif
    return E;
}

float3 SampleReflectionVector(float3 view_direction, float3 normal, float roughness, int2 dispatch_thread_id, out float pdf) {
    if (roughness < 0.001f) {
        pdf = 1.0f;
        return reflect(view_direction, normal);
    }
    float3x3 tbn_transform = CreateTBN(normal);
    float3   view_direction_tbn = mul(-view_direction, tbn_transform);
    float2   u = SampleRandomVector2DBaked(dispatch_thread_id);
    // float3   sampled_normal_tbn = Sample_GGX_VNDF_Hemisphere(view_direction_tbn, roughness, u.x, u.y);
#if defined(SSRT_SPECULAR)
    float4   sampled_normal_tbn = ImportanceSampleGGX(u, roughness * roughness * roughness * roughness);
#else
    float4   sampled_normal_tbn = CosineSampleHemisphereConcentric(u);
#endif
#ifdef PERFECT_REFLECTIONS
    sampled_normal_tbn.xyz = float3(0, 0, 1); // Overwrite normal sample to produce perfect reflection.
#endif
#if defined(SSRT_SPECULAR)
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

static const int2 offset[4] = {
    int2(-1, 0), int2(1, 0), int2(0, 1), int2(0, -1)
};

float LocalBRDF(float3 V, float3 L, float3 N, float roughness) {
#if defined(SSRT_SPECULAR)  // D_GGX only
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

[numthreads(8, 8, 1)] void main(uint3 groupID : SV_GroupID,
                                uint3 groupThreadID : SV_GroupThreadID,
                                uint3 DTid : SV_DispatchThreadID)
{
    uint2 screen_size = SharedData::BufferDim.xy;
    // texDepth Hi-Z pyramid base is full-res
    const float2 depth_screen_size = float2(screen_size);
    uint2 coords = DTid.xy;        // full-res pixel position
    uint2 fullResCoords = coords;  // dispatch is full-res, no 2× expansion

    float3 debug;

    float4 outColor = float4(0, 0, 0, 0);
    float4 outPDF = float4(0, 0, 0, 0);

    float2 uv = float2(fullResCoords + 0.5) * SharedData::BufferDim.zw * FrameBuffer::DynamicResolutionParams2.xy;
    uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);

    float3 normalVS;
    float roughness;
    GetNormalRoughness(fullResCoords, normalVS, roughness);
    roughness = clamp(roughness, 0.02f, 1.0f);

    // Tracing mode determines which lanes own this pixel and where in the NRD output it gets written.
    bool isMyPixel;
    uint2 outPixelPos;
    if (TracingMode == TRACING_MODE_HALF) {
        // Checkerboard: diffuse = WHITE (even sum), specular = BLACK (odd sum); data packed to left half.
        uint checker = (coords.x + coords.y + FrameIndex) & 1;
#if defined(SSRT_SPECULAR)
        isMyPixel = (checker == 1);
#else
        isMyPixel = (checker == 0);
#endif
        outPixelPos = uint2(coords.x >> 1, coords.y);
    } else if (TracingMode == TRACING_MODE_FULL_PROBABILISTIC) {
        // Roughness-weighted 1-ray-per-pixel split. Both diffuse and specular passes compute the same
        // decision from identical inputs so exactly one writes each pixel; NRD AREA_3X3 fills the gaps.
        float diffuseProbability = clamp(roughness, 0.25, 0.75);
        uint hash = Random::pcg3d(uint3(coords, FrameIndex)).x;
        float rnd = float(hash) * (1.0 / 4294967296.0);
        bool isDiffusePath = rnd < diffuseProbability;
#if defined(SSRT_SPECULAR)
        isMyPixel = !isDiffusePath;
#else
        isMyPixel = isDiffusePath;
#endif
        outPixelPos = coords;
    } else {
        // FULL: every pixel traces both diffuse and specular in their respective dispatches.
        isMyPixel = true;
        outPixelPos = coords;
    }

#if !defined(SSRT_SPECULAR)
    float3 albedo = AlbedoTexture[fullResCoords].xyz;
#endif

    bool is_mirror = IsMirrorReflection(roughness);
    int most_detailed_mip = HIZ_MIN_MIP;
    float2 mip_resolution = SSRT_GetMipResolution(depth_screen_size, most_detailed_mip);
    float z = SSRT_LoadDepth(uv * mip_resolution * FrameBuffer::DynamicResolutionParams1.xy, most_detailed_mip);
    float3 screen_uv_space_ray_origin = float3(uv, z);
    float3 view_space_ray = ScreenSpaceToViewSpace(screen_uv_space_ray_origin, FrameBuffer::CameraProjInverse[eyeIndex]);
    float3 world_space_normal = normalize(mul(FrameBuffer::CameraViewInverse[eyeIndex], float4(normalVS, 0)).xyz);
    float3 view_space_surface_normal = normalVS;
    float3 view_space_ray_direction = normalize(view_space_ray);
    float viewZ = abs(view_space_ray.z);  // save before bias for NRD hit dist normalization
    static const float4 kHitDistParams = float4(HitDistA, HitDistB, HitDistC, HitDistD);
    view_space_ray += view_space_surface_normal * NormalBias * view_space_ray.z * GAME_UNIT_TO_M;
    float pdf;
    float3 view_space_reflected_direction = SampleReflectionVector(view_space_ray_direction, view_space_surface_normal, roughness, coords, pdf);
    screen_uv_space_ray_origin = ProjectPosition(view_space_ray, FrameBuffer::CameraProj[eyeIndex]);
    float3 screen_space_ray_direction = ProjectDirection(view_space_ray, view_space_reflected_direction, screen_uv_space_ray_origin, FrameBuffer::CameraProj[eyeIndex]);
    float3 world_space_reflected_direction = mul(FrameBuffer::CameraViewInverse[eyeIndex], float4(view_space_reflected_direction, 0)).xyz;
    float3 world_space_origin = mul(FrameBuffer::CameraViewInverse[eyeIndex], float4(view_space_ray, 1)).xyz;
    float world_ray_length = 0.0;
    bool valid_ray = isMyPixel && all(coords < (uint2)(screen_size * FrameBuffer::DynamicResolutionParams1.xy));
    uint hit_counter = 0;
    float3 hit = float3(0.0, 0.0, 0.0);
    float confidence = 0.0;
    float3 world_space_hit = float3(0.0, 0.0, 0.0);
    float3 world_space_ray = float3(0.0, 0.0, 0.0);

    float depth = DepthTexture[fullResCoords].x;
    float4 positionWS = float4(2 * float2(uv.x, -uv.y + 1) - 1, depth, 1);
	positionWS = mul(FrameBuffer::CameraViewProjInverse[eyeIndex], positionWS);
	positionWS.xyz = positionWS.xyz / positionWS.w;

    float4 sampleData = 0.f;
    float3 sampleDirVS = 0.f;

    if (valid_ray)
    {
        bool valid_hit;
        bool go_through_thin = false;
        uint numIterations;
        float thickness  = Thickness  + roughness * 10.0;
        hit = SSRT_HierarchicalRaymarch(screen_uv_space_ray_origin,
                                            screen_space_ray_direction,
                                            is_mirror,
                                            depth_screen_size,
                                            most_detailed_mip,
                                            roughness,
                                            thickness,
                                            HIZ_MAX_ITERATIONS,
                                            valid_hit, numIterations);

        world_space_hit  = ScreenSpaceToWorldSpace(hit, FrameBuffer::CameraViewProjInverse[eyeIndex]);
        world_space_ray  = world_space_hit - world_space_origin.xyz;
        world_ray_length = length(world_space_ray);
        float occlusion;
        bool isBackfaceHit = false;
        confidence       = valid_hit ? SSRT_ValidateHit(hit,
                                                      uv,
                                                      world_space_ray,
                                                      screen_size,
                                                      thickness,
                                                      eyeIndex,
                                                      occlusion,
                                                      isBackfaceHit
                                                      )
                                     : 0;
        // Preserve before cubemap fallback overwrites. Backface hits still count as occluders
        // for AO — their geometry is valid even if radiance from screen is not trustworthy.
        float screenConfidence = isBackfaceHit ? 1.0 : confidence;
        float3 sampleColor = 0;
        if (confidence > 0.0f)
        {
            sampleColor = Color::IrradianceToLinear(ScreenColorTextureMips.SampleLevel(LinearSampler, hit.xy * FrameBuffer::DynamicResolutionParams1.xy, 0).xyz);
#if !defined(SSRT_SPECULAR)
            sampleColor *= SharedData::ssrtSettings.DiffuseMult;
#else
            sampleColor *= SharedData::ssrtSettings.SpecularMult;
#endif

            outPDF.xyz += hit * confidence;
            outPDF.w += pdf * confidence;
        }
        const float NdotV = saturate(dot(normalize(view_space_ray), view_space_surface_normal));
#if defined(DYNAMIC_CUBEMAPS) && !SHARC_UPDATE
        if (UseDynamicCubemapsAsFallback != 0 && (confidence < 0.999f))
        {
#   if defined(SSRT_SPECULAR)            
            const uint sampleMip = 0;
#   else
            const uint sampleMip = 2;
#   endif
            float directionalAmbientLuminance = Color::RGBToLuminance(max(0.0, mul(SharedData::DirectionalAmbient, float4(world_space_reflected_direction, 1.0)))) * Color::ReflectionNormalisationScale;
            float envLuminance;
            // Fallback to dynamic cubemaps
            float3 envColor = EnvReflectionsTexture.SampleLevel(LinearSampler, world_space_reflected_direction, sampleMip);
#	if defined(SKYLIGHTING)
            if (!SharedData::InInterior)
            {
                float3 positionMS = positionWS.xyz;

                sh2 skylighting = Skylighting::sample(SharedData::skylightingSettings, SkylightingProbeArray, stbn_vec3_2Dx1D_128x128x64, fullResCoords, positionMS.xyz, world_space_reflected_direction);
                float3 skylightingNormal = normalize(float3(world_space_normal.xy, max(0, world_space_normal.z)));
                float skylightingDiffuse = SphericalHarmonics::FuncProductIntegral(skylighting, SphericalHarmonics::EvaluateCosineLobe(skylightingNormal)) / Math::PI;
                skylightingDiffuse = saturate(skylightingDiffuse);

                skylightingDiffuse = lerp(1.0, skylightingDiffuse, Skylighting::getFadeOutFactor(positionMS.xyz));

                skylightingDiffuse *= 1.0 + saturate(world_space_normal.z) * (1.0 - SharedData::skylightingSettings.MinDiffuseVisibility);

                skylightingDiffuse = Skylighting::mixDiffuse(SharedData::skylightingSettings, skylightingDiffuse);
#       if defined(SSRT_SPECULAR)
                skylightingDiffuse = GetSpecularOcclusionFromAmbientOcclusion(NdotV, skylightingDiffuse, roughness);
#       endif
                float3 envNoSkyColor = EnvTexture.SampleLevel(LinearSampler, world_space_reflected_direction, sampleMip);
                float3 envSkyColor = envColor;
                float3 skyColor = max(envSkyColor - envNoSkyColor, 0);
                envLuminance = Color::RGBToLuminance(EnvTexture.SampleLevel(LinearSampler, world_space_reflected_direction, 15));
                envColor = lerp(envNoSkyColor, envNoSkyColor * (directionalAmbientLuminance / max(envLuminance, 1e-4)), CubemapNormalization);
                envColor += skyColor * skylightingDiffuse;
            } else {
                envLuminance = Color::RGBToLuminance(EnvReflectionsTexture.SampleLevel(LinearSampler, world_space_reflected_direction, 15));
                envColor = lerp(envColor, envColor * (directionalAmbientLuminance / max(envLuminance, 1e-4)), CubemapNormalization);
            }
#   else
            envLuminance = Color::RGBToLuminance(EnvReflectionsTexture.SampleLevel(LinearSampler, world_space_reflected_direction, 15).xyz);
            envColor = lerp(envColor, envColor * (directionalAmbientLuminance / max(envLuminance, 1e-4)), CubemapNormalization);
#   endif
            envColor = Color::IrradianceToLinear(envColor);
            float ao = lerp(1.0, occlusion, OcclusionStrength);
#   if defined(SSGI)
            ao *= 1 - saturate(SsgiAoTexture[fullResCoords].x);
#   endif
#   if defined(SSRT_SPECULAR)
            ao = GetSpecularOcclusionFromAmbientOcclusion(NdotV, ao, roughness);
            envColor *= ao;
#   else
            float3 multiBounceAO = MultiBounceAO(albedo, ao);
            envColor *= multiBounceAO;
#   endif
            sampleColor.xyz = lerp(envColor, sampleColor.xyz, confidence);
            confidence = 1;
        }
#endif
        // Raw world-space hit distance weighted by screen-space hit validity.
        // 0 for pure environment fallback samples. Normalized to normHitDist in output.
        float sampleW = world_ray_length * screenConfidence;
        sampleData = float4(sampleColor, sampleW);
        sampleDirVS = view_space_reflected_direction * confidence;
    }

#if defined(SSRT_SPECULAR)
    if (isMyPixel) {
        float rawHitDist  = sampleData.w;  // world_ray_length * screenConfidence; 0 = env-only
        float normHitDist = REBLUR_FrontEnd_GetNormHitDist(rawHitDist, viewZ, kHitDistParams, roughness);
        OutSpecRadianceHitDist[outPixelPos] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(
            sampleData.xyz, normHitDist, true);
    }
#else
    if (isMyPixel) {
        float3 radiance  = sampleData.xyz;
        float rawHitDist = sampleData.w;  // world_ray_length * screenConfidence; 0 = miss/off-screen
        bool hitInScreen = rawHitDist > 0.0 && all(hit.xy > 0.0) && all(hit.xy < 1.0);
        // Max out normHitDist when no screen-space hit — NRD treats 1 as "infinite distance / unoccluded"
        float normHitDist = hitInScreen
            ? REBLUR_FrontEnd_GetNormHitDist(rawHitDist, viewZ, kHitDistParams, 1.0)
            : 1.0;

        float3 dirVS_norm = normalize(sampleDirVS + float3(0, 0, NRD_EPS));
        float3 dirWS      = normalize(mul((float3x3)FrameBuffer::CameraViewInverse[eyeIndex], dirVS_norm));

        float4 sh1;
        float4 sh0 = REBLUR_FrontEnd_PackSh(radiance, normHitDist, dirWS, sh1, true);

        OutSH0[outPixelPos] = sh0;
        OutSH1[outPixelPos] = sh1;
    }
#endif
}