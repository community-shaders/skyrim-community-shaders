#ifndef __SHADOW_SAMPLING_DEPENDENCY_HLSL__
#define __SHADOW_SAMPLING_DEPENDENCY_HLSL__

#include "Common/Color.hlsli"
#include "Common/Math.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"

#if defined(TERRAIN_SHADOWS)
#	include "TerrainShadows/TerrainShadows.hlsli"
#endif

#if defined(CLOUD_SHADOWS)
#	include "CloudShadows/CloudShadows.hlsli"
#endif

#if defined(IBL)
#	include "IBL/IBL.hlsli"
#elif defined(SKYLIGHTING)
// sh2 type is needed for the ExtractLighting overload that accepts a visibility SH
#	include "Common/Spherical Harmonics/SphericalHarmonics.hlsli"
#endif

// Populated once per frame by Deferred::CopyShadowLightData from BSShadowDirectionalLight.
// Column-major float4x4 projections so HLSL `mul(proj, float4(pos, 1))` matches the
// XMMATRIX layout written by XMStoreFloat4x4 on the C++ side.
struct DirectionalShadowLightData
{
	column_major float4x4 ShadowProj[2];
	column_major float4x4 InvShadowProj[2];
	float2 EndSplitDistances;
	float2 StartSplitDistances;
};

StructuredBuffer<DirectionalShadowLightData> DirectionalShadowLights : register(t98);

#if defined(VOLUMETRIC_SHADOWS)
#	include "VolumetricShadows/VolumetricShadows.hlsli"
#endif

namespace ShadowSampling
{
	static const float MinDirectionalLightMultiplier = 1e-5;
	static const float3 LightingSampleNormal = float3(0, 0, 1);
	static const float3 ImageBasedLightingNormal = float3(0, 0, -1);

	bool HasDirectionalShadows()
	{
		return SharedData::HasDirectionalShadows;
	}

	float GetWorldShadow(float3 positionWS, float3 offset)
	{
		if (SharedData::InInterior || SharedData::HideSky || SharedData::InMapMenu)
			return 1.0;

		float worldShadow = 1.0;
#if defined(TERRAIN_SHADOWS)
		worldShadow = TerrainShadows::GetTerrainShadow(positionWS + offset, LinearSampler);
#endif

#if defined(CLOUD_SHADOWS)
		worldShadow *= CloudShadows::GetCloudShadowMult(positionWS, LinearSampler);
#endif

		return worldShadow;
	}

	float Get3DFilteredShadow(float3 positionWS, float3 viewDirection, float2 screenPosition, out float surfaceShadow)
	{
#if defined(EFFECT)
		float viewRayLength = min(Permutation::EffectRadius * 0.2, 256);
		float3 startPosition = positionWS - viewDirection * viewRayLength;
		float3 endPosition = positionWS + viewDirection * viewRayLength;
#elif defined(UNDERWATER)
		float viewRayLength = 128.0;
		float3 startPosition = positionWS;
		float3 endPosition = positionWS - viewDirection * viewRayLength;
#else
		float viewRayLength = 128.0;
		float3 startPosition = positionWS;
		float3 endPosition = positionWS + viewDirection * viewRayLength;
#endif

		float totalRayLength = distance(endPosition, startPosition);

		const float stepSize = 32.0;  // Fixed step size in world units

		uint sampleCount = clamp(uint(totalRayLength / stepSize + 0.5), 1, 4);
		float rcpSampleCount = rcp(sampleCount);

		float noise = Random::InterleavedGradientNoise(screenPosition, SharedData::FrameCount);

		float worldShadow = 0.0;
		for (uint i = 0; i < sampleCount; i++) {
			float t = (float(i) + noise) * rcpSampleCount;
			float3 sampledPositionWS = lerp(endPosition, startPosition, t);
			float worldShadowSample = ShadowSampling::GetWorldShadow(sampledPositionWS, FrameBuffer::CameraPosAdjust.xyz);
			surfaceShadow = worldShadowSample;
			worldShadow += worldShadowSample;
		}

		if (worldShadow == 0.0 && surfaceShadow == 0.0)
			return 0.0;

		worldShadow *= rcpSampleCount;

#if defined(VOLUMETRIC_SHADOWS)
		if (HasDirectionalShadows()) {
			float vsmSurfaceShadow;
			float shadow = VolumetricShadows::GetVSMShadow3D(startPosition, endPosition, noise, sampleCount, vsmSurfaceShadow);
			surfaceShadow *= vsmSurfaceShadow;
			return worldShadow * shadow;
		}
#else
		return worldShadow;
#endif

		return worldShadow;
	}

	float GetLightingShadow(float3 worldPosition, out float detailedShadow)
	{
		if (!HasDirectionalShadows()) {
			detailedShadow = 1.0;
			return 1.0;
		}

#if defined(VOLUMETRIC_SHADOWS)
		float shadow = VolumetricShadows::GetVSMShadow2D(worldPosition, detailedShadow);
		return shadow;
#else
		detailedShadow = 1.0;
		return 1.0;
#endif
	}

	float3 GetRawAmbientLighting(float3 normal)
	{
		return max(0, SharedData::GetAmbient(normal));
	}

	float3 GetAmbientLighting(float3 normal)
	{
		float3 ambientColor = GetRawAmbientLighting(normal);

#if defined(IBL)
		if (SharedData::iblSettings.EnableIBL) {
			ambientColor = ImageBasedLighting::GetDiffuseIBL(ambientColor, ImageBasedLightingNormal);
		}
#endif

		return ambientColor;
	}

#if defined(SKYLIGHTING) && !defined(INTERIOR)
	float3 GetAmbientLighting(float3 normal, float skylightingDiffuse)
	{
		float3 ambientColor = GetRawAmbientLighting(normal);

#	if defined(IBL)
		if (SharedData::iblSettings.EnableIBL) {
			ambientColor = ImageBasedLighting::GetDiffuseIBLOccluded(ambientColor, ImageBasedLightingNormal, skylightingDiffuse);
		}
#	endif

		return ambientColor;
	}
#endif

	float3 GetDirectionalLighting()
	{
		float llDirLightMult = (SharedData::linearLightingSettings.enableLinearLighting && !SharedData::linearLightingSettings.isDirLightLinear) ? SharedData::linearLightingSettings.dirLightMult : 1.0f;
		return Color::DirectionalLight(SharedData::DirLightColor.xyz / max(llDirLightMult, MinDirectionalLightMultiplier), SharedData::linearLightingSettings.isDirLightLinear) * llDirLightMult;
	}

	float3 GetSceneLightingColor()
	{
		return GetAmbientLighting(LightingSampleNormal) + GetDirectionalLighting();
	}

#if defined(SKYLIGHTING) && !defined(INTERIOR)
	void ExtractLighting(float3 inputColor, out float3 dirColor, out float3 ambientColor, float skylightingDiffuse)
#else
	void ExtractLighting(float3 inputColor, out float3 dirColor, out float3 ambientColor)
#endif
	{
#if defined(SKYLIGHTING) && !defined(INTERIOR)
		float3 ambientColorAmb = GetAmbientLighting(LightingSampleNormal, skylightingDiffuse);
#else
		float3 ambientColorAmb = GetAmbientLighting(LightingSampleNormal);
#endif

		float3 dirLightColorDir = GetDirectionalLighting();

		float inputLuma = Color::RGBToLuminance(inputColor);
		float ambientLuma = Color::RGBToLuminance(ambientColorAmb);
		float dirLightLuma = Color::RGBToLuminance(dirLightColorDir);

		float totalLuma = ambientLuma + dirLightLuma;

		// Scale ambientColorAmb so total luma matches input luma
		if (totalLuma > 0.0 && ambientLuma > 0.0)
			ambientColorAmb *= inputLuma / totalLuma;

		float3 dirLightColorAmb = max(0.0, inputColor - ambientColorAmb);

		dirColor = dirLightColorAmb;
		ambientColor = ambientColorAmb;
	}
}

#endif  // __SHADOW_SAMPLING_DEPENDENCY_HLSL__
