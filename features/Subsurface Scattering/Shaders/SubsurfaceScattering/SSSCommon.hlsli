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

float3 SSSDecodeAlbedo(float3 encodedAlbedo)
{
	float3 albedo = encodedAlbedo / Color::PBRLightingScale;
	return max(Color::IrradianceToLinear(albedo), 0.0f);
}

float3 SSSGetAlbedoFactor(float3 albedo, uint mode)
{
	if (mode == SSS_SCATTER_MODE_PRE)
		return 1.0f;
	return (mode == SSS_SCATTER_MODE_PRE_POST) ? sqrt(max(albedo, 0.0f)) : max(albedo, 0.0f);
}

float3 SSSGetAlbedoParticipation(float3 albedo)
{
	// Keep the existing low-albedo protection, but transition all channels
	// continuously instead of changing semantics at one quantized UNORM code.
	// The threshold is converted to the same linear domain as albedo so non-linear
	// lighting retains its previous effective cutoff.
	float threshold = Color::IrradianceToLinear(EPSILON_SSS_ALBEDO);
	return smoothstep(threshold, threshold * 4.0f, albedo);
}

float3 SSSRemoveAlbedo(float3 linearColor, float3 albedo, uint mode)
{
	if (mode == SSS_SCATTER_MODE_PRE)
		return linearColor;

	float3 factor = SSSGetAlbedoFactor(albedo, mode);
	float3 participation = SSSGetAlbedoParticipation(albedo);
	return linearColor * participation / max(factor, EPSILON_DIVISION);
}

float3 SSSApplyAlbedo(float3 irradiance, float3 originalLinearColor, float3 albedo, uint mode)
{
	if (mode == SSS_SCATTER_MODE_PRE)
		return irradiance;

	float3 factor = SSSGetAlbedoFactor(albedo, mode);
	float3 participation = SSSGetAlbedoParticipation(albedo);

	// Participation gates both emission into and reception from the blur. Squaring
	// it here makes the operation an exact identity without blur, while zero-albedo
	// channels retain the original color and cannot acquire unpremultiplied energy.
	return irradiance * factor * participation + originalLinearColor * (1.0f - participation * participation);
}

#endif
