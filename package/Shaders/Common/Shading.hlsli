#ifndef __SHADING_HLSLI__
#define __SHADING_HLSLI__

// [Jimenez et al. 2016, "Practical Realtime Strategies for Accurate Indirect Occlusion"]
float3 MultiBounceAO(float3 baseColor, float ao)
{
	float3 a = 2.0404 * baseColor - 0.3324;
	float3 b = -4.7951 * baseColor + 0.6417;
	float3 c = 2.7552 * baseColor + 0.6903;
	return max(ao, ((ao * a + b) * ao + c) * ao);
}

// [Lagarde et al. 2014, "Moving Frostbite to Physically Based Rendering 3.0"]
float SpecularAOLagarde(float NdotV, float ao, float roughness)
{
	return saturate(pow(abs(NdotV + ao), exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
}

float SpecularOcclusion(float NdotV, float alpha, float occlusion)
{
	return saturate(pow(abs(NdotV + occlusion), alpha) - 1.0 + occlusion);
}

#endif  // __SHADING_HLSLI__