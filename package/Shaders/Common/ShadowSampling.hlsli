#ifndef __SHADOW_SAMPLING_DEPENDENCY_HLSL__
#define __SHADOW_SAMPLING_DEPENDENCY_HLSL__

#include "Common/Math.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/Color.hlsli"

#if defined(TERRAIN_SHADOWS)
#	include "TerrainShadows/TerrainShadows.hlsli"
#endif

#if defined(CLOUD_SHADOWS)
#	include "CloudShadows/CloudShadows.hlsli"
#endif

#if defined(IBL)
#	include "IBL/IBL.hlsli"
#endif

#if defined(EFFECT_SHADOWS)
#	include "EffectShadows/EffectShadows.hlsli"
#endif

namespace ShadowSampling
{
	float GetWorldShadow(float3 positionWS, float3 offset, uint eyeIndex)
	{
		if (SharedData::InInterior || SharedData::HideSky)
			return 1.0;

		float worldShadow = 1.0;
#if defined(TERRAIN_SHADOWS)
		worldShadow = TerrainShadows::GetTerrainShadow(positionWS + offset, LinearSampler);
#endif

#if defined(CLOUD_SHADOWS)
		if (!SharedData::InMapMenu)
			worldShadow *= CloudShadows::GetCloudShadowMult(positionWS, LinearSampler);
#endif

		return worldShadow;
	}

	float Get3DFilteredShadow(float3 positionWS, float3 viewDirection, float2 screenPosition, uint eyeIndex, float depth, out float surfaceShadow)
	{
		const float stepSize = 32.0;  // Fixed step size in world units

		float noise = Random::InterleavedGradientNoise(screenPosition, SharedData::FrameCount);
		float2 rotation;
		sincos(Math::TAU * noise, rotation.y, rotation.x);
		float2x2 rotationMatrix = float2x2(rotation.x, rotation.y, -rotation.y, rotation.x);

		float screenDepth = SharedData::GetScreenDepth(depth);
		float objectDepth = length(positionWS);
		float maxDistance = max(0, screenDepth - objectDepth);

	#if defined(EFFECT)
		float viewRayLength = min(Permutation::EffectRadius * 0.2, 256);
		float3 startPosition = positionWS - viewDirection * viewRayLength;
		float3 endPosition = positionWS + viewDirection * min(viewRayLength, maxDistance);
	#elif defined(UNDERWATER)
		float viewRayLength = 128.0;
		float3 startPosition = positionWS;
		float3 endPosition = positionWS - viewDirection * viewRayLength;
	#else
		float viewRayLength = 128.0;
		float3 startPosition = positionWS;
		float3 endPosition = positionWS + viewDirection * min(viewRayLength, maxDistance);
	#endif

		float totalRayLength = distance(endPosition, startPosition);
		uint sampleCount = clamp(uint(totalRayLength / stepSize + 0.5), 1, 4);
		float rcpSampleCount = rcp(sampleCount);

		startPosition += (endPosition - startPosition) * noise * rcpSampleCount;

		surfaceShadow = ShadowSampling::GetWorldShadow(startPosition, FrameBuffer::CameraPosAdjust[eyeIndex].xyz, eyeIndex);
		float worldShadow = surfaceShadow;
		for(uint i = 1; i < sampleCount; i++){
			float t = float(i) * rcpSampleCount;
			float3 sampledPositionWS = lerp(startPosition, endPosition, t);
			worldShadow += ShadowSampling::GetWorldShadow(sampledPositionWS, FrameBuffer::CameraPosAdjust[eyeIndex].xyz, eyeIndex);
		}

		if (worldShadow == 0.0)
			return 0.0;

		worldShadow *= rcpSampleCount;

#if defined(EFFECT_SHADOWS)
		float vsmSurfaceShadow;
		float shadow = EffectShadows::GetVSMShadow(startPosition, endPosition, sampleCount, eyeIndex, vsmSurfaceShadow);
		surfaceShadow *= vsmSurfaceShadow;
		return worldShadow * shadow;
#else
		return worldShadow;
#endif
	}

	float GetLightingShadow(float noise, float3 worldPosition, uint eyeIndex)
	{
		return 1;
	}

#if defined(SKYLIGHTING) && !defined(INTERIOR)
	void ExtractLighting(float3 inputColor, out float3 dirColor, out float3 ambientColor, float skylightingDiffuse)
#else
	void ExtractLighting(float3 inputColor, out float3 dirColor, out float3 ambientColor)
#endif
	{
		float3 ambientColorAmb = max(0, mul(SharedData::DirectionalAmbient, float4(0, 0, 1, 1)));

#		if defined(IBL)
	if (SharedData::iblSettings.EnableDiffuseIBL && (!SharedData::InInterior || SharedData::iblSettings.EnableInterior)) {
		ambientColorAmb *= SharedData::iblSettings.DALCAmount;
#			if defined(SKYLIGHTING) && !defined(INTERIOR)
		float3 iblColor = Color::Saturation(ImageBasedLighting::GetIBLColor(float3(0, 0, -1), skylightingDiffuse), SharedData::iblSettings.IBLSaturation) * SharedData::iblSettings.DiffuseIBLScale;
#			else
		float3 iblColor = Color::Saturation(ImageBasedLighting::GetIBLColor(float3(0, 0, -1)), SharedData::iblSettings.IBLSaturation) * SharedData::iblSettings.DiffuseIBLScale;
#			endif
		ambientColorAmb += Color::IrradianceToGamma(iblColor);
	}
#		endif

		float llDirLightMult = (SharedData::linearLightingSettings.enableLinearLighting && !SharedData::linearLightingSettings.isDirLightLinear) ? SharedData::linearLightingSettings.dirLightMult : 1.0f;
		float3 dirLightColorDir = Color::DirectionalLight(SharedData::DirLightColor.xyz / max(llDirLightMult, 1e-5), SharedData::linearLightingSettings.isDirLightLinear) * llDirLightMult;

		{
			float maxScale = 1.0;
			if (ambientColorAmb.x > 0.0)
				maxScale = min(maxScale, inputColor.x / ambientColorAmb.x);
			if (ambientColorAmb.y > 0.0)
				maxScale = min(maxScale, inputColor.y / ambientColorAmb.y);
			if (ambientColorAmb.z > 0.0)
				maxScale = min(maxScale, inputColor.z / ambientColorAmb.z);
			ambientColorAmb *= maxScale;
		}

		float3 dirLightColorAmb = max(0.0, inputColor - ambientColorAmb);

		dirColor = dirLightColorAmb;
		ambientColor = ambientColorAmb;
	}
}

#endif  // __SHADOW_SAMPLING_DEPENDENCY_HLSL__