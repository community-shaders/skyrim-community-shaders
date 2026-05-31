#ifndef __EXPONENTIAL_HEIGHT_FOG_HLSLI__
#define __EXPONENTIAL_HEIGHT_FOG_HLSLI__

#include "Common/SharedData.hlsli"

#if defined(DYNAMIC_CUBEMAPS)
#	include "DynamicCubemaps/DynamicCubemaps.hlsli"
#endif

namespace ExponentialHeightFog
{
	float GetVanillaFogFade(float vanillaFogFade)
	{
		return SharedData::exponentialHeightFogSettings.respectVanillaFogFade != 0 ? vanillaFogFade : 1.0f;
	}

	bool ShouldDisableVanillaFog()
	{
		return SharedData::exponentialHeightFogSettings.enabled && SharedData::exponentialHeightFogSettings.disableVanillaFog != 0;
	}

	// Henyey-Greenstein phase function for physically-based inscattering.
	// g: asymmetry parameter [-1, 1]. Positive = forward scattering, 0 = isotropic.
	float HenyeyGreenstein(float cosTheta, float g)
	{
		float g2 = g * g;
		float denom = 1.0f + g2 - 2.0f * g * cosTheta;
		return (1.0f - g2) / (4.0f * Math::PI * pow(max(denom, 1e-5f), 1.5f));
	}

	float4 GetExponentialHeightFog(float3 positionWS, float3 cameraWS, float3 fogColor)
	{
		float fogHeightFalloff = SharedData::exponentialHeightFogSettings.fogHeightFalloff * 0.001f;
		float fogDensity = SharedData::exponentialHeightFogSettings.fogDensity * 0.001f;
		if (fogDensity <= 0.0f) {
			return 0.0f;
		}
		float3 viewToPos = positionWS;
		float viewToPosLength = length(viewToPos);
		float viewToPosLengthInv = rcp(viewToPosLength);

		float rayOriginTerms = fogDensity * exp2(-fogHeightFalloff * max(cameraWS.z - SharedData::exponentialHeightFogSettings.fogHeight, 0));
		float rayLength = viewToPosLength;
		float rayDirectionZ = viewToPos.z;

		if (SharedData::exponentialHeightFogSettings.startDistance > 0) {
			float excludeIntersectionTime = SharedData::exponentialHeightFogSettings.startDistance * viewToPosLengthInv;
			float cameraToExclusionIntersectionZ = excludeIntersectionTime * viewToPos.z;
			float exclusionIntersectionZ = cameraWS.z + cameraToExclusionIntersectionZ;
			rayLength = (1.0f - excludeIntersectionTime) * viewToPosLength;
			rayDirectionZ = viewToPos.z - cameraToExclusionIntersectionZ;
			float exponent = fogHeightFalloff * max(exclusionIntersectionZ - SharedData::exponentialHeightFogSettings.fogHeight, 0);
			rayOriginTerms = fogDensity * exp2(-exponent);
		}

		float falloff = fogHeightFalloff * rayDirectionZ;
		float lineIntegral = (1.0f - exp2(-falloff)) / falloff;
		float lineIntegralTaylor = 0.69314718056f - 0.24022650695f * falloff;  // log(2) - (0.5 * (log(2)^2)) * falloff
		float exponentialHeightLineIntegralCalc = rayOriginTerms * (abs(falloff) > 0.01f ? lineIntegral : lineIntegralTaylor);
		float exponentialHeightLineIntegral = exponentialHeightLineIntegralCalc * rayLength;

		float expFogFactor = saturate(exp2(-exponentialHeightLineIntegral));

		float3 fogInscatteringColor = fogColor * SharedData::exponentialHeightFogSettings.originalFogColorAmount;
		fogInscatteringColor += SharedData::exponentialHeightFogSettings.fogInscatteringColor.rgb * SharedData::exponentialHeightFogSettings.fogInscatteringColor.a;

#if defined(DYNAMIC_CUBEMAPS)
		if (SharedData::exponentialHeightFogSettings.useDynamicCubemaps > 0) {
			float3 cubemapColor = DynamicCubemaps::EnvReflectionsTexture.SampleLevel(SampColorSampler, normalize(lerp(positionWS, float3(0, 0, 1), saturate((SharedData::exponentialHeightFogSettings.cubemapMipLevel + 1) / 8))), SharedData::exponentialHeightFogSettings.cubemapMipLevel).xyz;
			fogInscatteringColor += cubemapColor * SharedData::exponentialHeightFogSettings.inscatteringTint.rgb * SharedData::exponentialHeightFogSettings.inscatteringTint.a;
		}
#endif

		fogColor = fogInscatteringColor * (1.0f - expFogFactor);

		float3 directionalInscattering = 0;

		// Calculate directional light inscattering using Henyey-Greenstein phase function
		if (SharedData::exponentialHeightFogSettings.directionalInscatteringMultiplier > 0) {
			float cosTheta = dot(normalize(positionWS), SharedData::DirLightDirection.xyz);
			float phase = HenyeyGreenstein(cosTheta, SharedData::exponentialHeightFogSettings.directionalInscatteringAnisotropy);
			float3 directionalLightInscattering = SharedData::DirLightColor.xyz * phase;
			float dirExponentialHeightLineIntegral = exponentialHeightLineIntegralCalc * max(rayLength - SharedData::exponentialHeightFogSettings.startDistance, 0);
			float dirExpFogFactor = saturate(exp2(-dirExponentialHeightLineIntegral));
			directionalInscattering = directionalLightInscattering * (1 - dirExpFogFactor) * SharedData::exponentialHeightFogSettings.directionalInscatteringMultiplier;
		}

		fogColor += directionalInscattering;
		return float4(fogColor, 1.0f - expFogFactor);
	}

	float GetSunlightFogAttenuation(float3 positionWS, float3 cameraWS)
	{
		float fogHeightFalloff = SharedData::exponentialHeightFogSettings.fogHeightFalloff * 0.001f;
		float fogDensity = SharedData::exponentialHeightFogSettings.fogDensity * 0.001f;
		if (fogDensity <= 0.0f) {
			return 1.0f;
		}

		float exponent = fogHeightFalloff * max(positionWS.z + cameraWS.z - SharedData::exponentialHeightFogSettings.fogHeight, 0.0f);
		float localDensity = fogDensity * exp2(-exponent);

		float3 lightDir = SharedData::DirLightDirection.xyz;
		float lightDirZ = lightDir.z;

		float sunlightFogAttenuation = 0.0f;

		// Integral = Density * (1 - exp2(-slope * inf)) / slope
		if (lightDirZ > 0.001f) {
			float slope = max(fogHeightFalloff * lightDirZ, 1e-8f);
			float exponentialHeightLineIntegral = localDensity / slope;
			sunlightFogAttenuation = saturate(exp2(-exponentialHeightLineIntegral));
		}

		return lerp(1.0f, sunlightFogAttenuation, SharedData::exponentialHeightFogSettings.sunlightAttenuationAmount);
	}
}
#endif
