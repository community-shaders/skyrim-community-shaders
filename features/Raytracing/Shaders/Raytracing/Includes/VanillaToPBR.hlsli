#ifndef VANILA_TO_PBR_HLSLI
#define VANILA_TO_PBR_HLSLI

#include "Common/Color.hlsli"

float CalcSpecularity(float3 specularColor, float glossiness)
{
	return saturate(max(specularColor.r, max(specularColor.g, specularColor.b)) * glossiness);	
}

float RemappedSpecularity(float specularity)
{
	return 1.0f - (specularity * 0.75f + 0.25f); 	
}

float CalcRoughness(float roughnessFromShininess, float specularity)
{
	return roughnessFromShininess * RemappedSpecularity(specularity);
}

float CalcMetallic(float3 albedo, float specularity, float roughnessFromShininess)
{
	const float albedoLuminance = saturate(Color::RGBToLuminance(albedo));
	return (1.0f - roughnessFromShininess) * (specularity * specularity) * (1.0f - albedoLuminance);
}

#endif