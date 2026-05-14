// Hi-Z hierarchical specular ray march for screen-space reflections.
// Ported from sssrupdate branch (ssrt_raymarch.hlsl / ssrt_common.hlsli).
//
// Copyright (C) 2024 Advanced Micro Devices, Inc.  (Hi-Z ray march)
// SPDX-License-Identifier: MIT

#include "Common/BRDF.hlsli"
#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"
#include "Common/Game.hlsli"
#include "Common/Math.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/VR.hlsli"
#include "NRD/NRDReblurSH.hlsli"
#include "ScreenSpaceGI/common.hlsli"

Texture2D<float> DepthTexture : register(t0);
Texture2D<unorm float3> NormalRoughnessTexture : register(t1);
Texture2D<float4> ScreenColorTexture : register(t2);
Texture2D<float> DepthTextureMips : register(t3);
#if defined(DYNAMIC_CUBEMAPS)
TextureCube<float3> EnvTexture : register(t4);
TextureCube<float3> EnvReflectionsTexture : register(t5);
#	if defined(SKYLIGHTING)
#		define SKYLIGHTING_PROBE_REGISTER t6
#		include "Skylighting/Skylighting.hlsli"
#	endif
#endif

RWTexture2D<float4> OutSpecRadianceHitDist : register(u0);

SamplerState LinearSampler : register(s1);

///////////////////////////////////////////////////////////////////////////////
// Importance sampling helpers (from ssrt_common.hlsli)
///////////////////////////////////////////////////////////////////////////////

#define Pow2(x) ((x) * (x))

float4 ImportanceSampleGGX(float2 E, float a2)
{
	float Phi = 2 * Math::PI * E.x;
	float CosTheta = sqrt((1 - E.y) / (1 + (a2 - 1) * E.y));
	float SinTheta = sqrt(1 - CosTheta * CosTheta);

	float3 H;
	H.x = SinTheta * cos(Phi);
	H.y = SinTheta * sin(Phi);
	H.z = CosTheta;

	float d = (CosTheta * a2 - CosTheta) * CosTheta + 1;
	float D = a2 / (Math::PI * d * d);
	float PDF = D * CosTheta;

	return float4(H, PDF);
}

float3 SampleGGXVNDF(float3 Ve, float alpha_x, float alpha_y, float U1, float U2)
{
	float3 Vh = normalize(float3(alpha_x * Ve.x, alpha_y * Ve.y, Ve.z));
	float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
	float3 T1 = lensq > 0 ? float3(-Vh.y, Vh.x, 0) * rsqrt(lensq) : float3(1, 0, 0);
	float3 T2 = cross(Vh, T1);
	float r = sqrt(U1);
	float phi = 2.0 * Math::PI * U2;
	float t1 = r * cos(phi);
	float t2 = r * sin(phi);
	float s = 0.5 * (1.0 + Vh.z);
	t2 = (1.0 - s) * sqrt(1.0 - t1 * t1) + s * t2;
	float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - t1 * t1 - t2 * t2)) * Vh;
	float3 Ne = normalize(float3(alpha_x * Nh.x, alpha_y * Nh.y, max(0.0, Nh.z)));
	return Ne;
}

float2 Hammersley16(uint Index, uint NumSamples, uint2 Random)
{
	float E1 = frac((float)Index / NumSamples + float(Random.x) * (1.0 / 65536.0));
	float E2 = float((reversebits(Index) >> 16) ^ Random.y) * (1.0 / 65536.0);
	return float2(E1, E2);
}

float GetSpecularOcclusionFromAmbientOcclusion(float NdotV, float ao, float roughness)
{
	return saturate(pow(abs(NdotV + ao), exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
}

void GetNormalRoughness(uint2 dtid, out float3 normal, out float roughness)
{
	float3 normalGlossiness = NormalRoughnessTexture[dtid];
	normal = GBuffer::DecodeNormal(normalGlossiness.xy);
	roughness = 1.0f - normalGlossiness.z;
}

float3x3 CreateSpecTBN(float3 N)
{
	float3 U;
	if (abs(N.z) > 0.0) {
		float k = sqrt(N.y * N.y + N.z * N.z);
		U.x = 0.0;
		U.y = -N.z / k;
		U.z = N.y / k;
	} else {
		float k = sqrt(N.x * N.x + N.y * N.y);
		U.x = N.y / k;
		U.y = -N.x / k;
		U.z = 0.0;
	}

	float3x3 TBN;
	TBN[0] = U;
	TBN[1] = cross(N, U);
	TBN[2] = N;
	return transpose(TBN);
}

///////////////////////////////////////////////////////////////////////////////
// Noise generation
///////////////////////////////////////////////////////////////////////////////

float2 SampleRandomVector2DBaked(uint2 pixel)
{
	int3 seed = int3(pixel.xy, 0);
	seed.z = Random::pcg3d(int3(seed.xy, SharedData::FrameCount)).x;
	uint2 xi = Random::pcg3d(seed).xy / 0x10000;
	float2 E = Hammersley16(0, 1, xi);
	E.y = lerp(E.y, 0, BRDFBias);
	return E;
}

///////////////////////////////////////////////////////////////////////////////
// Reflection sampling
///////////////////////////////////////////////////////////////////////////////

float3 Sample_GGX_VNDF_Hemisphere(float3 Ve, float alpha, float U1, float U2) { return SampleGGXVNDF(Ve, alpha, alpha, U1, U2); }

float3 SampleReflectionVector(float3 view_direction, float3 normal, float roughness, int2 dispatch_thread_id, out float pdf)
{
	if (roughness < 0.001f) {
		pdf = 1.0f;
		return reflect(view_direction, normal);
	}
	float3x3 tbn_transform = CreateSpecTBN(normal);
	float3 view_direction_tbn = mul(-view_direction, tbn_transform);
	float2 u = SampleRandomVector2DBaked(dispatch_thread_id);
	float4 sampled_normal_tbn = ImportanceSampleGGX(u, roughness * roughness * roughness * roughness);
	float3 reflected_direction_tbn = reflect(-view_direction_tbn, sampled_normal_tbn.xyz);
	float3x3 inv_tbn_transform = transpose(tbn_transform);
	pdf = sampled_normal_tbn.w;
	return mul(reflected_direction_tbn, inv_tbn_transform);
}

///////////////////////////////////////////////////////////////////////////////
// Hi-Z ray march
///////////////////////////////////////////////////////////////////////////////

#define SSRT_FLOAT_MAX 3.402823466e+38
#define HIZ_MAX_ITERATIONS SpecMaxSteps
#define HIZ_MIN_MIP 0
#define SSRT_DEPTH_HIERARCHY_MAX_MIP (SpecMaxMips - 1)

float3 ProjectPosition(float3 origin, float4x4 mat)
{
	float4 projected = mul(mat, float4(origin, 1));
	projected.xyz /= projected.w;
	projected.xy = 0.5 * projected.xy + 0.5;
	projected.y = (1 - projected.y);
	return projected.xyz;
}

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
}

float SSRT_LoadDepth(int2 pixel_coordinate, int mip)
{
	return DepthTextureMips.Load(int3(pixel_coordinate, mip)).x;
}

float3 SSRT_ScreenSpaceToViewSpace(float3 screen_space_position, uint eyeIndex)
{
	return InvProjectPosition(screen_space_position, FrameBuffer::CameraProjInverse[eyeIndex]);
}

void SSRT_InitialAdvanceRay(float3 origin,
	float3 direction,
	float3 inv_direction,
	float2 current_mip_resolution,
	float2 current_mip_resolution_inv,
	float2 floor_offset,
	float2 uv_offset,
	out float3 position,
	out float current_t)
{
	float2 current_mip_position = current_mip_resolution * origin.xy;
	float2 xy_plane = floor(current_mip_position) + floor_offset;
	xy_plane = xy_plane * current_mip_resolution_inv + uv_offset;
	float2 t = xy_plane * inv_direction.xy - origin.xy * inv_direction.xy;
	current_t = min(t.x, t.y);
	position = origin + current_t * direction;
}

bool SSRT_AdvanceRay(float3 origin,
	float3 direction,
	float3 inv_direction,
	float2 current_mip_position,
	float2 current_mip_resolution_inv,
	uint current_mip_level,
	float2 floor_offset,
	float2 uv_offset,
	float surface_z,
	float thickness,
	inout float3 position,
	inout float current_t)
{
	float2 xy_plane = floor(current_mip_position) + floor_offset;
	xy_plane = xy_plane * current_mip_resolution_inv + uv_offset;
	float3 boundary_planes = float3(xy_plane, surface_z);

	float3 t = boundary_planes * inv_direction - origin * inv_direction;
	t.z = direction.z > 0 ? t.z : SSRT_FLOAT_MAX;

	float t_min = min(min(t.x, t.y), t.z);
	bool above_surface = surface_z > position.z;
	bool skipped_tile = asuint(t_min) != asuint(t.z) && above_surface;
	current_t = above_surface ? t_min : current_t;
	position = origin + current_t * direction;

	return skipped_tile;
}

float3 SSRT_HierarchicalRaymarch(float3 origin, float3 direction, float2 screen_size, int most_detailed_mip, float roughness, float thickness,
	uint max_traversal_intersections, out bool valid_hit, out uint _num_iters)
{
	const float3 inv_direction = abs(direction) > float(1.0e-12) ? float(1.0) / direction : SSRT_FLOAT_MAX;

	int current_mip = most_detailed_mip;
	float2 current_mip_resolution = SSRT_GetMipResolution(screen_size, current_mip);
	float2 current_mip_resolution_inv = rcp(current_mip_resolution);

	float2 uv_offset = 0.005 * exp2(most_detailed_mip) / screen_size;
	uv_offset = direction.xy < 0 ? -uv_offset : uv_offset;

	float2 floor_offset = direction.xy < 0 ? 0 : 1;

	float current_t;
	float3 position;
	SSRT_InitialAdvanceRay(origin, direction, inv_direction, current_mip_resolution, current_mip_resolution_inv, floor_offset, uv_offset, position, current_t);

	_num_iters = uint(0);
	while (_num_iters < max_traversal_intersections && current_mip >= most_detailed_mip) {
		if (any(position.xy > float2(1.0, 1.0)) || any(position.xy < float2(0.0, 0.0)))
			break;
		if (position.z > float(1.0) - float(1.0e-6))
			break;

		float2 current_mip_position = current_mip_resolution * position.xy;
		float surface_z = SSRT_LoadDepth(current_mip_position * FrameBuffer::DynamicResolutionParams1.xy, current_mip);
		bool skipped_tile =
			SSRT_AdvanceRay(origin, direction, inv_direction, current_mip_position, current_mip_resolution_inv, current_mip, floor_offset, uv_offset, surface_z, thickness, position, current_t);
		bool nextMipIsOutOfRange = skipped_tile && (current_mip >= SSRT_DEPTH_HIERARCHY_MAX_MIP);
		if (!nextMipIsOutOfRange) {
			current_mip += skipped_tile ? 1 : -1;
			current_mip_resolution *= skipped_tile ? 0.5 : 2;
			current_mip_resolution_inv *= skipped_tile ? 2 : 0.5;
		}
		++_num_iters;
	}

	valid_hit = (_num_iters <= max_traversal_intersections);
	return position;
}

///////////////////////////////////////////////////////////////////////////////
// Hit validation
///////////////////////////////////////////////////////////////////////////////

float SSRT_ValidateHit(float3 hit, float2 uv, float3 world_space_ray_direction, float2 screen_size, float depth_buffer_thickness, uint eyeIndex, out float occlusion, out bool isBackfaceHit)
{
	occlusion = 1.f;
	isBackfaceHit = false;

	if ((hit.x < 0.0f) || (hit.y < 0.0f) || (hit.x > 1.0f) || (hit.y > 1.0f)) {
		return 0.0f;
	}

	int2 texel_coords = int2(screen_size * hit.xy * FrameBuffer::DynamicResolutionParams1.xy);
	float surface_z = SSRT_LoadDepth(texel_coords / 2, 1);
	if (surface_z == 1.0) {
		return 0;
	}

	float3 view_space_surface = SSRT_ScreenSpaceToViewSpace(float3(hit.xy, surface_z), eyeIndex);
	float3 view_space_hit = SSRT_ScreenSpaceToViewSpace(hit, eyeIndex);
	float distance = length(view_space_surface - view_space_hit);

	float confidence = 1.0f - smoothstep(0.0f, depth_buffer_thickness, distance);
	confidence *= confidence;

	float3 hit_normalVS;
	float hit_roughness;
	GetNormalRoughness(texel_coords, hit_normalVS, hit_roughness);
	float3 hit_normal = normalize(mul(FrameBuffer::CameraViewInverse[eyeIndex], float4(hit_normalVS, 0)).xyz);
	if (dot(hit_normal, world_space_ray_direction) > 0) {
		occlusion = 1 - confidence;
		isBackfaceHit = true;
		return 0;
	}

	float2 manhattan_dist = abs(hit.xy - uv);
	if ((manhattan_dist.x < (2.f / screen_size.x)) && (manhattan_dist.y < (2.f / screen_size.y))) {
		occlusion = 1 - confidence;
		return 0;
	}

	return confidence;
}

///////////////////////////////////////////////////////////////////////////////
// Main
///////////////////////////////////////////////////////////////////////////////

[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID) {
	uint2 screen_size = SharedData::BufferDim.xy;
	const float2 depth_screen_size = float2(screen_size);

	uint2 fullResCoords;
	uint2 outPixelPos;

#if defined(SSGI_HALF)
	uint colOffset = ((DTid.y + FrameIndex) & 1) ^ 1;
	fullResCoords = uint2(DTid.x * 2 + colOffset, DTid.y);
	outPixelPos = DTid.xy;
#else
	fullResCoords = DTid.xy;
	outPixelPos = DTid.xy;
#endif

	float2 uv = float2(fullResCoords + 0.5) * SharedData::BufferDim.zw * FrameBuffer::DynamicResolutionParams2.xy;
	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(uv);

	float3 normalVS;
	float roughness;
	GetNormalRoughness(fullResCoords, normalVS, roughness);
	roughness = clamp(roughness, 0.02f, 1.0f);

	float depth = DepthTexture[fullResCoords].x;

	bool isSky = depth >= 1.0 - 1e-6;
	if (isSky || any(fullResCoords >= (uint2)(screen_size * FrameBuffer::DynamicResolutionParams1.xy))) {
		OutSpecRadianceHitDist[outPixelPos] = 0;
		return;
	}

	int most_detailed_mip = min(1, (int)SSRT_DEPTH_HIERARCHY_MAX_MIP);

	float z;
	if (most_detailed_mip == 0) {
		z = depth;
	} else {
		float2 mip_resolution = SSRT_GetMipResolution(depth_screen_size, most_detailed_mip);
		z = SSRT_LoadDepth(uv * mip_resolution * FrameBuffer::DynamicResolutionParams1.xy, most_detailed_mip);
	}

	float3 screen_uv_space_ray_origin = float3(uv, z);
	float3 view_space_ray = InvProjectPosition(screen_uv_space_ray_origin, FrameBuffer::CameraProjInverse[eyeIndex]);
	float3 world_space_normal = normalize(mul(FrameBuffer::CameraViewInverse[eyeIndex], float4(normalVS, 0)).xyz);
	float3 view_space_surface_normal = normalVS;
	float3 view_space_ray_direction = normalize(view_space_ray);
	float viewZ = abs(view_space_ray.z);
	static const float4 kHitDistParams = float4(HitDistA, HitDistB, HitDistC, HitDistD);

	float3 world_space_origin = mul(FrameBuffer::CameraViewInverse[eyeIndex], float4(view_space_ray, 1)).xyz;

	view_space_ray += view_space_surface_normal * NormalBias * view_space_ray.z * GAME_UNIT_TO_M;

	float pdf;
	float3 view_space_reflected_direction = SampleReflectionVector(view_space_ray_direction, view_space_surface_normal, roughness, fullResCoords, pdf);
	screen_uv_space_ray_origin = ProjectPosition(view_space_ray, FrameBuffer::CameraProj[eyeIndex]);
	float3 screen_space_ray_direction = ProjectDirection(view_space_ray, view_space_reflected_direction, screen_uv_space_ray_origin, FrameBuffer::CameraProj[eyeIndex]);
	float3 world_space_reflected_direction = mul(FrameBuffer::CameraViewInverse[eyeIndex], float4(view_space_reflected_direction, 0)).xyz;

	float3 world_space_ray_origin = mul(FrameBuffer::CameraViewInverse[eyeIndex], float4(view_space_ray, 1)).xyz;

	// Ray march
	bool valid_hit;
	uint numIterations;
	float thickness = SpecThickness + roughness * 10.0;
	float3 hit = SSRT_HierarchicalRaymarch(screen_uv_space_ray_origin,
		screen_space_ray_direction,
		depth_screen_size,
		most_detailed_mip,
		roughness,
		thickness,
		HIZ_MAX_ITERATIONS,
		valid_hit, numIterations);

	float3 world_space_hit = InvProjectPosition(hit, FrameBuffer::CameraViewProjInverse[eyeIndex]);
	float3 world_space_ray = world_space_hit - world_space_ray_origin;
	float world_ray_length = length(world_space_ray);
	float occlusion;
	bool isBackfaceHit = false;
	float confidence = valid_hit ? SSRT_ValidateHit(hit,
									   uv,
									   world_space_ray,
									   screen_size,
									   thickness,
									   eyeIndex,
									   occlusion,
									   isBackfaceHit) :
	                               0;
	float screenConfidence = isBackfaceHit ? 1.0 : confidence;
	float3 sampleColor = 0;
	if (confidence > 0.0f) {
		sampleColor = Color::IrradianceToLinear(ScreenColorTexture.SampleLevel(LinearSampler, hit.xy * FrameBuffer::DynamicResolutionParams1.xy, 0).xyz);
		sampleColor *= SharedData::ssgiSettings.SpecularMult;
	}

	const float NdotV = saturate(dot(normalize(view_space_ray), view_space_surface_normal));

#if defined(DYNAMIC_CUBEMAPS)
	if (SpecUseDynamicCubemap != 0 && (confidence < 0.999f)) {
		float3 envColor = EnvReflectionsTexture.SampleLevel(LinearSampler, world_space_reflected_direction, 0);
#	if defined(SKYLIGHTING)
		if (!SharedData::InInterior) {
			float3 positionMS = world_space_origin;

			sh2 skylightingSH = Skylighting::Sample(positionMS, world_space_reflected_direction);
			float fadeOutFactor = Skylighting::GetFadeOutFactor(positionMS);
			float3 skylightingNormal = normalize(float3(world_space_normal.xy, max(0, world_space_normal.z)));
			float skylightingDiffuse = Skylighting::EvaluateDiffuse(skylightingSH, skylightingNormal, fadeOutFactor);

			skylightingDiffuse *= 1.0 + saturate(world_space_normal.z) * (1.0 - SharedData::skylightingSettings.MinDiffuseVisibility);
			skylightingDiffuse = GetSpecularOcclusionFromAmbientOcclusion(NdotV, skylightingDiffuse, roughness);
			float3 envNoSkyColor = EnvTexture.SampleLevel(LinearSampler, world_space_reflected_direction, 0);
			float3 envSkyColor = envColor;
			float3 skyColor = max(envSkyColor - envNoSkyColor, 0);
			envColor = envNoSkyColor * skylightingDiffuse;
			envColor += skyColor * skylightingDiffuse;
		}
#	endif
		envColor = Color::IrradianceToLinear(envColor);
		float ao = lerp(1.0, occlusion, OcclusionStrength);
		ao = GetSpecularOcclusionFromAmbientOcclusion(NdotV, ao, roughness);
		envColor *= ao;
		sampleColor.xyz = lerp(envColor, sampleColor.xyz, confidence);
		confidence = 1;
	}
#endif

	float sampleW = world_ray_length * screenConfidence;
	float normHitDist = REBLUR_FrontEnd_GetNormHitDist(sampleW, viewZ, kHitDistParams, roughness);
	OutSpecRadianceHitDist[outPixelPos] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(sampleColor, normHitDist, true);
}
