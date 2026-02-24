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
#endif

Texture2D<float2> SharedShadowMap : register(t18);

// Directional (sun) shadow data: cascade split distances, projection matrices,
struct DirectionalShadowData
{
	float2   EndSplitDistances;    // cascade end distances: x = cascade 0, y = cascade 1
	float2   StartSplitDistances;  // cascade start distances: x = cascade 0, y = cascade 1
	float4x4 ShadowMapProj[2];     // world-to-shadow projection for each directional cascade
};

StructuredBuffer<DirectionalShadowData> DirectionalShadows : register(t19);

Texture2DArray<float> DirectionalShadowCascades : register(t20);

struct ShadowData 
{ 
	float4x4 ShadowProj; 
	uint ShadowType;
	uint3 ShadowLightParam;
};

StructuredBuffer<ShadowData> Shadows    : register(t22);
Texture2DArray<float>        ShadowMaps : register(t23);

namespace ShadowSampling
{
	float GetWorldShadow(float3 positionWS, float3 offset, uint eyeIndex)
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

	float Get3DFilteredShadow(float3 positionWS, float3 viewDirection, float2 screenPosition, uint eyeIndex, out float surfaceShadow)
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
			float worldShadowSample = ShadowSampling::GetWorldShadow(sampledPositionWS, FrameBuffer::CameraPosAdjust[eyeIndex].xyz, eyeIndex);
			surfaceShadow = worldShadowSample;
			worldShadow += worldShadowSample;
		}

		if (worldShadow == 0.0 && surfaceShadow == 0.0)
			return 0.0;

		worldShadow *= rcpSampleCount;

#if defined(VOLUMETRIC_SHADOWS)
		float vsmSurfaceShadow;
		float shadow = VolumetricShadows::GetVSMShadow3D(SharedShadowData[0], startPosition, endPosition, noise, sampleCount, eyeIndex, vsmSurfaceShadow);
		surfaceShadow *= vsmSurfaceShadow;
		return worldShadow * shadow;
#else
		return worldShadow;
#endif
	}

	float GetLightingShadow(float3 worldPosition, uint eyeIndex, out float detailedShadow)
	{
		DirectionalShadowData shadow = DirectionalShadows[0];

		float shadowMapDepth = length(worldPosition);
		if (shadowMapDepth >= shadow.EndSplitDistances.y) {
			detailedShadow = 1.0;
			return 1.0;
		}

		float fade = saturate(shadowMapDepth / shadow.EndSplitDistances.y);
		float cascadeSelect = saturate((shadowMapDepth - shadow.StartSplitDistances.y) / (shadow.EndSplitDistances.x - shadow.StartSplitDistances.y));
		uint primaryCascade = uint(cascadeSelect);

		const uint sampleCount = 16;
		const uint rcpSampleCount = 1.0 / float(sampleCount);

		float3 positionLS = mul(shadow.ShadowProj[cascadeSelect], float4(worldPosition, 1)).xyz;

		float visibility = 0;
		for (uint i = 0; i < sampleCount; i++) {
			float2 sampleOffset = mul(Random::PoissonSampleOffsets16[sampleIndex], rotationMatrix);
			float2 sampleUV = positionLS.xy + sampleOffset * shadow.ShadowSampleParam.z;
			visibility += DirectionalShadowCascades.SampleCmpLevelZero(samp, float3(sampleUV, primaryCascade), positionLS.z).x;
		}

		float fadeFactor = 1.0 - pow(fade, 8);
		detailedShadow = lerp(1.0, shadow, fadeFactor);
		return lerp(1.0, shadow, fadeFactor);
	}

	float GetFrustumShadow(ShadowData shadow, uint shadowIndex, float3 worldPosition, float2x2 rotationMatrix)
	{
		const uint sampleCount = 16;
		const uint rcpSampleCount = 1.0 / float(sampleCount);

		float3 positionLS = mul(shadow.ShadowProj, float4(worldPosition, 1)).xyz;

		float visibility = 0;
		for (uint i = 0; i < sampleCount; i++) {
			float2 sampleOffset = mul(Random::PoissonSampleOffsets16[sampleIndex], rotationMatrix);
			float2 sampleUV = positionLS.xy + sampleOffset * shadow.ShadowSampleParam.z;
			visibility += ShadowMaps.SampleCmpLevelZero(samp, float3(sampleUV, shadowIndex), positionLS.z).x;
		}

		return visibility * rcpSampleCount;
	}

	float GetParaboloidShadow(ShadowData shadow, uint shadowIndex, float3 worldPosition)
	{
		const uint sampleCount = 16;
		const uint rcpSampleCount = 1.0 / float(sampleCount);

		float visibility = 0;
		for (uint i = 0; i < sampleCount; i++) {
			// Optimized random generation using simplified hash
			float3 sampleOffset = (Random::R3Modified(sampleIndex + SharedData::FrameCount * sampleCount, seed / 4294967295) * 2.0 - 1.0) * shadow.ShadowSampleParam.z * 2048;

			float3 positionLS = mul(shadow.ShadowMapProj, float4(worldPosition + sampleOffset, 1)).xyz;

			bool lowerHalf = positionLS.z * 0.5 + 0.5 < 0;
			float3 normalizedPositionLS = normalize(positionLS);

			float compareValue = saturate(length(positionLS) / shadow.ShadowLightParam.x);

			float3 positionOffset = lowerHalf ? float3(0, 0, -1) : float3(0, 0, 1);
			float3 lightDirection = normalize(normalizedPositionLS + positionOffset);
			float2 shadowMapUV = lightDirection.xy / lightDirection.z * 0.5 + 0.5;
			shadowMapUV.y = lowerHalf ? 1.0 - 0.5 * shadowMapUV.y : 0.5 * shadowMapUV.y;

			visibility += ShadowMaps.SampleCmpLevelZero(samp, float3(shadowMapUV, shadowIndex), compareValue).x;
		}

		return visibility * rcpSampleCount;
	}

	float GetShadowLightShadow(uint shadowIndex, float3 worldPosition, float2x2 rotationMatrix)
	{
		ShadowData shadow = Shadows[shadowIndex];

		[branch]
		if (shadow.ShadowType == 0)
			return GetParaboloidShadow(shadow, shadowIndex, worldPosition);
		else if (shadow.ShadowType == 1)
			return GetFrustumShadow(shadow, shadowIndex, worldPosition, rotationMatrix);
		
		return 1.0;
	}

#if defined(SKYLIGHTING) && !defined(INTERIOR)
	void ExtractLighting(float3 inputColor, out float3 dirColor, out float3 ambientColor, float skylightingDiffuse)
#else
	void ExtractLighting(float3 inputColor, out float3 dirColor, out float3 ambientColor)
#endif
	{
		float3 ambientColorAmb = max(0, SharedData::GetAmbient(float3(0, 0, 1)));

#if defined(IBL)
		if (SharedData::iblSettings.EnableIBL) {
			if (SharedData::iblSettings.DALCMode == 2) {
				// Mode 2: keep vanilla DALC scaled by DALCAmount, add sky IBL overlay
				ambientColorAmb = ambientColorAmb * SharedData::iblSettings.DALCAmount + Color::IrradianceToGamma(ImageBasedLighting::GetSkyIBLColor(float3(0, 0, -1)));
			} else {
				float3 envIBLColor = Color::IrradianceToGamma(ImageBasedLighting::GetEnvIBLColor(float3(0, 0, -1)));
				float3 skyIBLColor = Color::IrradianceToGamma(ImageBasedLighting::GetSkyIBLColor(float3(0, 0, -1)));
				ambientColorAmb = envIBLColor + skyIBLColor;
			}
		}
#endif

		float llDirLightMult = (SharedData::linearLightingSettings.enableLinearLighting && !SharedData::linearLightingSettings.isDirLightLinear) ? SharedData::linearLightingSettings.dirLightMult : 1.0f;
		float3 dirLightColorDir = Color::DirectionalLight(SharedData::DirLightColor.xyz / max(llDirLightMult, 1e-5), SharedData::linearLightingSettings.isDirLightLinear) * llDirLightMult;

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
