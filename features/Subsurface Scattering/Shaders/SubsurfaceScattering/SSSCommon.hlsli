#ifndef SSS_COMMON_HLSLI
#define SSS_COMMON_HLSLI

#include "Common/Math.hlsli"

float3 SSSRemoveAlbedo(float3 color, float3 albedo)
{
	return color / max(albedo, EPSILON_SSS_ALBEDO);
}

float3 SSSApplyAlbedo(float3 irradiance, float3 albedo)
{
	return irradiance * albedo;
}

#endif
