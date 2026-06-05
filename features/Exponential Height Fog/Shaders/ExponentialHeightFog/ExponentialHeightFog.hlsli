#ifndef __EXPONENTIAL_HEIGHT_FOG_HLSLI__
#define __EXPONENTIAL_HEIGHT_FOG_HLSLI__

#include "Common/Random.hlsli"
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

	float GetSceneDepthFromClip(float4 clipPosition)
	{
		return max(clipPosition.w, SharedData::CameraData.y);
	}

	float GetSceneDepthForFog(float3 positionWS, uint eyeIndex, out float2 volumeUV, out float projectedDepth)
	{
		float4 clipPosition = mul(FrameBuffer::CameraViewProj[eyeIndex], float4(positionWS, 1.0f));
		[branch] if (clipPosition.w <= 0.0f)
		{
			volumeUV = 0.0f.xx;
			projectedDepth = 0.0f;
			return 0.0f;
		}

		projectedDepth = GetSceneDepthFromClip(clipPosition);
		volumeUV = clipPosition.xy / clipPosition.w * float2(0.5f, -0.5f) + 0.5f;

		volumeUV = saturate(volumeUV);
		return projectedDepth;
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

		float2 volumeUV;
		float projectedDepth;
		float sceneDepth = GetSceneDepthForFog(positionWS, eyeIndex, volumeUV, projectedDepth);
		if (projectedDepth <= 0.0f)
			return float4(0.0f, 0.0f, 0.0f, 1.0f);

#if defined(VR)
		volumeUV = Stereo::ConvertToStereoUV(volumeUV, eyeIndex);
#endif

		float volumeZ = saturate(ComputeVolumetricNormalizedSlice(sceneDepth, float(volumeDepth)));

		float3 volumeTexelCenter = 0.5f / float3(volumeWidth, volumeHeight, volumeDepth);
		float2 volumeUVMin = volumeTexelCenter.xy;
		float2 volumeUVMax = 1.0f.xx - volumeTexelCenter.xy;
#if defined(VR)
		float eyeMinX = (eyeIndex == 0u ? 0.0f : 0.5f) + volumeTexelCenter.x;
		float eyeMaxX = (eyeIndex == 0u ? 0.5f : 1.0f) - volumeTexelCenter.x;
		volumeUVMin.x = eyeMinX;
		volumeUVMax.x = eyeMaxX;
#endif
		float3 volumeUVW = float3(clamp(volumeUV, volumeUVMin, volumeUVMax), clamp(volumeZ, volumeTexelCenter.z, 1.0f - volumeTexelCenter.z));
		float4 volumetricFog = ExponentialHeightFogIntegratedLightScattering.SampleLevel(SampColorSampler, volumeUVW, 0);
		return lerp(float4(0.0f, 0.0f, 0.0f, 1.0f), volumetricFog, saturate((sceneDepth - GetVolumetricStartDistance()) * 100000000.0f));
	}

	float2 GetVolumetricFogUVMax(float2 volumeSize, float gridPixelSize)
	{
		float2 physicalSize = max(volumeSize * gridPixelSize, 1.0f.xx);
		float2 viewSizeSafe = ceil(SharedData::BufferDim.xy / gridPixelSize) * gridPixelSize - (gridPixelSize * 0.5f + 1.0f);
		return saturate(viewSizeSafe / physicalSize);
	}

	float4 SampleVolumetricFog(float4 screenPosition, uint eyeIndex)
	{
		if (!ShouldApplyVolumetricFog())
			return float4(0.0f, 0.0f, 0.0f, 1.0f);

		uint volumeWidth;
		uint volumeHeight;
		uint volumeDepth;
		ExponentialHeightFogIntegratedLightScattering.GetDimensions(volumeWidth, volumeHeight, volumeDepth);
		if (volumeWidth == 0 || volumeHeight == 0 || volumeDepth == 0)
			return float4(0.0f, 0.0f, 0.0f, 1.0f);

		float sceneDepth = SharedData::GetScreenDepth(screenPosition.z);
		float volumeZ = saturate(ComputeVolumetricNormalizedSlice(sceneDepth, float(volumeDepth)));

		float2 volumeSize = float2(volumeWidth, volumeHeight);
		float2 inferredGridPixelSize = ceil(SharedData::BufferDim.xy / max(volumeSize, 1.0f.xx));
		float gridPixelSize = max(max(inferredGridPixelSize.x, inferredGridPixelSize.y), 1.0f);
		float2 jitter = 0.0f.xx;
		[branch] if (SharedData::exponentialHeightFogSettings.volumetricUpsampleJitterMultiplier > 0.0f)
		{
			float2 noise = float2(
				Random::InterleavedGradientNoise(screenPosition.xy, SharedData::FrameCount),
				Random::InterleavedGradientNoise(screenPosition.yx + 19.19f, SharedData::FrameCount));
			jitter = (noise * 2.0f - 1.0f) * SharedData::exponentialHeightFogSettings.volumetricUpsampleJitterMultiplier * gridPixelSize;
		}

		float2 volumeUV = (screenPosition.xy + jitter) / (volumeSize * gridPixelSize);
		float3 volumeTexelCenter = 0.5f / float3(volumeWidth, volumeHeight, volumeDepth);
		float2 volumeUVMin = volumeTexelCenter.xy;
		float2 volumeUVMax = max(GetVolumetricFogUVMax(volumeSize, gridPixelSize), volumeUVMin);
#if defined(VR)
		volumeUVMin.x = (eyeIndex == 0u ? 0.0f : 0.5f) + volumeTexelCenter.x;
		volumeUVMax.x = max(volumeUVMin.x, min(volumeUVMax.x, (eyeIndex == 0u ? 0.5f : 1.0f) - volumeTexelCenter.x));
#endif
		float3 volumeUVW = float3(clamp(volumeUV, volumeUVMin, volumeUVMax), clamp(volumeZ, volumeTexelCenter.z, 1.0f - volumeTexelCenter.z));
		float4 volumetricFog = ExponentialHeightFogIntegratedLightScattering.SampleLevel(SampColorSampler, volumeUVW, 0);
		return lerp(float4(0.0f, 0.0f, 0.0f, 1.0f), volumetricFog, saturate((sceneDepth - GetVolumetricStartDistance()) * 100000000.0f));
	}

	// Apply per-pixel directional light phase correction to volumetric fog.
	// The volumetric compute stores directional scattering with isotropic phase (1/4PI) to
	// avoid angular aliasing at coarse froxel XY resolution. Here we restore the correct
	// per-pixel HG phase, weighted by the estimated directional light fraction.
	float4 ApplyDirectionalPhaseCorrection(float4 volumetricFog, float3 viewDirection)
	{
		if (volumetricFog.r + volumetricFog.g + volumetricFog.b < 1e-7f)
			return volumetricFog;

		float g = SharedData::exponentialHeightFogSettings.volumetricFogScatteringDistribution;
		float cosTheta = dot(normalize(SharedData::DirLightDirection.xyz), viewDirection);
		float perPixelPhase = HenyeyGreenstein(cosTheta, g);
		float isotropicPhase = 1.0f / (4.0f * Math::PI);

		// Estimate directional light's fraction of total volumetric inscattering
		float dirStrength = dot(SharedData::DirLightColor.xyz, float3(0.2126f, 0.7152f, 0.0722f)) *
		                    SharedData::exponentialHeightFogSettings.volumetricDirectionalScatteringIntensity;
		float skyStrength = SharedData::exponentialHeightFogSettings.volumetricSkyLightingIntensity;
		float dirFraction = saturate(dirStrength / max(dirStrength + skyStrength, 1e-5f));

		// Apply phase correction only to the estimated directional portion
		float correction = lerp(1.0f, perPixelPhase / isotropicPhase, dirFraction);
		volumetricFog.rgb *= correction;
		return volumetricFog;
	}

	float4 CombineVolumetricFog(float4 analyticalFog, float3 positionWS, uint eyeIndex, float3 viewDirection)
	{
		float4 volumetricFog = SampleVolumetricFog(positionWS, eyeIndex);
		volumetricFog = ApplyDirectionalPhaseCorrection(volumetricFog, viewDirection);
		float analyticalTransmittance = 1.0f - analyticalFog.w;
		float combinedTransmittance = volumetricFog.a * analyticalTransmittance;
		float combinedOpacity = saturate(1.0f - combinedTransmittance);
		float3 analyticalPremultiplied = analyticalFog.rgb * analyticalFog.w;
		float3 combinedPremultiplied = volumetricFog.rgb + volumetricFog.a * analyticalPremultiplied;
		return float4(combinedOpacity > 1e-4f ? combinedPremultiplied / combinedOpacity : float3(0.0f, 0.0f, 0.0f), combinedOpacity);
	}

	float4 CombineVolumetricFog(float4 analyticalFog, float4 screenPosition, uint eyeIndex, float3 viewDirection)
	{
		float4 volumetricFog = SampleVolumetricFog(screenPosition, eyeIndex);
		volumetricFog = ApplyDirectionalPhaseCorrection(volumetricFog, viewDirection);
		float analyticalTransmittance = 1.0f - analyticalFog.w;
		float combinedTransmittance = volumetricFog.a * analyticalTransmittance;
		float combinedOpacity = saturate(1.0f - combinedTransmittance);
		float3 analyticalPremultiplied = analyticalFog.rgb * analyticalFog.w;
		float3 combinedPremultiplied = volumetricFog.rgb + volumetricFog.a * analyticalPremultiplied;
		return float4(combinedOpacity > 1e-4f ? combinedPremultiplied / combinedOpacity : float3(0.0f, 0.0f, 0.0f), combinedOpacity);
	}

	float4 GetExponentialHeightFogInternal(float3 positionWS, float3 cameraWS, float3 fogColor, bool useScreenPosition, float4 screenPosition, bool applyVolumetricFog)
	{
		float fogHeightFalloff = SharedData::exponentialHeightFogSettings.fogHeightFalloff * 0.001f;
		float fogDensity = SharedData::exponentialHeightFogSettings.fogDensity * 0.001f;
		if (fogDensity <= 0.0f) {
			return 0.0f;
		}
		uint eyeIndex = GetEyeIndexFromCameraWS(cameraWS);
		float3 viewToPos = positionWS;
		float2 volumeUV;
		float projectedDepth;
		float sceneDepth = GetSceneDepthForFog(positionWS, eyeIndex, volumeUV, projectedDepth);
		[branch] if (projectedDepth > 1e-4f && sceneDepth > projectedDepth)
		{
			viewToPos *= sceneDepth / projectedDepth;
		}

		float viewToPosLength = length(viewToPos);
		float viewToPosLengthInv = rcp(max(viewToPosLength, 1e-4f));

		float rayOriginTerms = fogDensity * exp2(-fogHeightFalloff * max(cameraWS.z - SharedData::exponentialHeightFogSettings.fogHeight, 0));
		float rayLength = viewToPosLength;
		float rayDirectionZ = viewToPos.z;

		float excludeDistance = SharedData::exponentialHeightFogSettings.startDistance;
		if (applyVolumetricFog && ShouldApplyVolumetricFog()) {
			float cosAngle = sceneDepth * viewToPosLengthInv;
			float invCosAngle = cosAngle > 0.001f ? rcp(cosAngle) : 0.0f;
			excludeDistance = max(excludeDistance, GetVolumetricEndDistance() * invCosAngle);
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

		float3 viewDirection = viewToPos * viewToPosLengthInv;

		// Calculate directional light inscattering using Henyey-Greenstein phase function
		if (SharedData::exponentialHeightFogSettings.directionalInscatteringMultiplier > 0) {
			float3 lightDirection = normalize(SharedData::DirLightDirection.xyz);
			float cosTheta = dot(lightDirection, viewDirection);
			float phase = HenyeyGreenstein(cosTheta, SharedData::exponentialHeightFogSettings.directionalInscatteringAnisotropy);
			float3 directionalLightInscattering = SharedData::DirLightColor.xyz * phase;
			directionalInscattering = directionalLightInscattering * (1.0f - expFogFactor) * SharedData::exponentialHeightFogSettings.directionalInscatteringMultiplier;
		}

		fogColor += directionalInscattering;
		float4 analyticalFog = float4(fogColor, 1.0f - expFogFactor);
		if (!applyVolumetricFog) {
			return analyticalFog;
		}
		return useScreenPosition ? CombineVolumetricFog(analyticalFog, screenPosition, eyeIndex, viewDirection) : CombineVolumetricFog(analyticalFog, positionWS, eyeIndex, viewDirection);
	}

	float4 GetExponentialHeightFog(float3 positionWS, float3 cameraWS, float3 fogColor)
	{
		return GetExponentialHeightFogInternal(positionWS, cameraWS, fogColor, false, 0.0f.xxxx, true);
	}

	float4 GetExponentialHeightFog(float3 positionWS, float3 cameraWS, float3 fogColor, float4 screenPosition)
	{
		return GetExponentialHeightFogInternal(positionWS, cameraWS, fogColor, true, screenPosition, true);
	}

	float4 GetExponentialHeightFogNoVolumetric(float3 positionWS, float3 cameraWS, float3 fogColor)
	{
		return GetExponentialHeightFogInternal(positionWS, cameraWS, fogColor, false, 0.0f.xxxx, false);
	}

	float4 GetExponentialHeightFogNoVolumetric(float3 positionWS, float3 cameraWS, float3 fogColor, float4 screenPosition)
	{
		return GetExponentialHeightFogInternal(positionWS, cameraWS, fogColor, true, screenPosition, false);
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
