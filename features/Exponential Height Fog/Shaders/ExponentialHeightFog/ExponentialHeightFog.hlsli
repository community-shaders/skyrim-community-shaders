#ifndef __EXPONENTIAL_HEIGHT_FOG_HLSLI__
#define __EXPONENTIAL_HEIGHT_FOG_HLSLI__

#include "Common/SharedData.hlsli"
#include "ExponentialHeightFog/VolumetricFogCommon.hlsli"

#if defined(DYNAMIC_CUBEMAPS)
#	include "DynamicCubemaps/DynamicCubemaps.hlsli"
#endif

Texture3D<float4> ExponentialHeightFogIntegratedLightScattering : register(t19);

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

	uint GetEyeIndexFromCameraWS(float3 cameraWS)
	{
#if defined(VR)
		return distance(cameraWS, FrameBuffer::CameraPosAdjust[1].xyz) < distance(cameraWS, FrameBuffer::CameraPosAdjust[0].xyz) ? 1u : 0u;
#else
		return 0u;
#endif
	}

	bool ShouldApplyVolumetricFog()
	{
		return SharedData::exponentialHeightFogSettings.enabled != 0 &&
		       SharedData::exponentialHeightFogSettings.volumetricFogEnabled != 0 &&
		       SharedData::exponentialHeightFogSettings.volumetricFogDistance > SharedData::exponentialHeightFogSettings.volumetricFogStartDistance + 1.0f;
	}

	float4 SampleVolumetricFog(float3 positionWS, uint eyeIndex)
	{
		if (!ShouldApplyVolumetricFog())
			return float4(0.0f, 0.0f, 0.0f, 1.0f);

		uint volumeWidth;
		uint volumeHeight;
		uint volumeDepth;
		ExponentialHeightFogIntegratedLightScattering.GetDimensions(volumeWidth, volumeHeight, volumeDepth);
		if (volumeWidth == 0 || volumeHeight == 0 || volumeDepth == 0)
			return float4(0.0f, 0.0f, 0.0f, 1.0f);

		float viewDistance = length(positionWS);
		if (viewDistance <= GetVolumetricStartDistance())
			return float4(0.0f, 0.0f, 0.0f, 1.0f);

		float4 clipPosition = mul(FrameBuffer::CameraViewProj[eyeIndex], float4(positionWS, 1.0f));
		if (clipPosition.w <= 0.0f)
			return float4(0.0f, 0.0f, 0.0f, 1.0f);

		float2 volumeUV = clipPosition.xy / clipPosition.w * float2(0.5f, -0.5f) + 0.5f;
		if (any(volumeUV < 0.0f) || any(volumeUV > 1.0f))
			return float4(0.0f, 0.0f, 0.0f, 1.0f);

#if defined(VR)
		volumeUV = Stereo::ConvertToStereoUV(volumeUV, eyeIndex);
#endif

		float volumeZ = ComputeVolumetricNormalizedSlice(viewDistance);
		return ExponentialHeightFogIntegratedLightScattering.SampleLevel(SampColorSampler, float3(volumeUV, volumeZ), 0);
	}

	float4 CombineVolumetricFog(float4 analyticalFog, float3 positionWS, uint eyeIndex)
	{
		float4 volumetricFog = SampleVolumetricFog(positionWS, eyeIndex);
		float volumetricOpacity = 1.0f - volumetricFog.a;
		if (volumetricOpacity <= 1e-4f)
			return analyticalFog;

		float analyticalTransmittance = 1.0f - analyticalFog.w;
		float combinedTransmittance = volumetricFog.a * analyticalTransmittance;
		float combinedOpacity = saturate(1.0f - combinedTransmittance);
		float3 analyticalPremultiplied = analyticalFog.rgb * analyticalFog.w;
		float3 combinedPremultiplied = volumetricFog.rgb + volumetricFog.a * analyticalPremultiplied;
		float3 combinedColor = combinedPremultiplied / max(combinedOpacity, 1e-4f);
		return float4(combinedColor, combinedOpacity);
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

		float excludeDistance = SharedData::exponentialHeightFogSettings.startDistance;
		if (ShouldApplyVolumetricFog()) {
			excludeDistance = max(excludeDistance, min(GetVolumetricEndDistance(), viewToPosLength));
		}

		if (excludeDistance > 0) {
			excludeDistance = min(excludeDistance, viewToPosLength);
			float excludeIntersectionTime = excludeDistance * viewToPosLengthInv;
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
		uint eyeIndex = GetEyeIndexFromCameraWS(cameraWS);
		return CombineVolumetricFog(float4(fogColor, 1.0f - expFogFactor), positionWS, eyeIndex);
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
