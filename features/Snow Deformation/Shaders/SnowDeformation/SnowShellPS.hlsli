#define TERRAIN_SHADOWS
#define CLOUD_SHADOWS
#define VOLUMETRIC_SHADOWS
SamplerState ShellLinearSampler : register(s1);
#define LinearSampler ShellLinearSampler
#include "Common/ShadowSampling.hlsli"
#include "ScreenSpaceShadows/ScreenSpaceShadows.hlsli"
#include "Skylighting/Skylighting.hlsli"
#include "SnowDeformation/SnowShadow.hlsli"
#define EM_PARALLAX_CUSTOM_HEIGHT
float EMParallaxCustomHeight(float2 uv, float mip);
#ifndef TERRAIN_VARIATION
#	define TERRAIN_VARIATION
#	define SNOW_EM_TERRAIN_VARIATION
#endif
#include "ExtendedMaterials/ExtendedMaterials.hlsli"
#ifdef SNOW_EM_TERRAIN_VARIATION
#	undef TERRAIN_VARIATION
#	undef SNOW_EM_TERRAIN_VARIATION
#endif
#include "SnowDeformation/SnowShading.hlsli"

#include "SnowDeformation/SnowLights.hlsli"

#define SNOW_GBUFFER_TARGETS              \
	float4 Diffuse : SV_Target0;          \
	float4 MotionVectors : SV_Target1;    \
	float4 NormalGlossiness : SV_Target2; \
	float4 Albedo : SV_Target3;           \
	float4 Specular : SV_Target4;         \
	float4 Reflectance : SV_Target5;      \
	float4 Masks : SV_Target6;            \
	float4 Masks2 : SV_Target7;
