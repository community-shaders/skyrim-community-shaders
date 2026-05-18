#ifndef SSS_COMMON_HLSLI
#define SSS_COMMON_HLSLI

#include "Common/Math.hlsli"

#define SSSS_N_SAMPLES 21

#define SSS_SCATTER_MODE_PRE 0
#define SSS_SCATTER_MODE_POST 1
#define SSS_SCATTER_MODE_PRE_POST 2

cbuffer PerFrameSSS : register(b1)
{
	float4 Kernels[SSSS_N_SAMPLES + SSSS_N_SAMPLES];
	float4 BaseProfile;
	float4 HumanProfile;
	float SSSS_FOVY;
	uint BurleySamples;
	uint ScatterMode;
	uint pad;
	float4 MeanFreePathBase;
	float4 MeanFreePathHuman;
};

float3 SSSRemoveAlbedo(float3 color, float3 albedo, uint mode)
{
	if (mode == SSS_SCATTER_MODE_PRE)
		return color;
	float3 divisor = (mode == SSS_SCATTER_MODE_PRE_POST) ? sqrt(albedo) : albedo;
	return color / max(divisor, EPSILON_SSS_ALBEDO);
}

float3 SSSApplyAlbedo(float3 irradiance, float3 albedo, uint mode)
{
	if (mode == SSS_SCATTER_MODE_PRE)
		return irradiance;
	float3 multiplier = (mode == SSS_SCATTER_MODE_PRE_POST) ? sqrt(albedo) : albedo;
	return irradiance * multiplier;
}

#endif
