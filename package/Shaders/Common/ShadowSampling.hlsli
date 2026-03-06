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

Texture2D<float2> SharedShadowMap : register(t80);

// Directional (sun) shadow data: cascade split distances, projection matrices,
struct DirectionalShadowData
{
	column_major float4x4 ShadowProj[2];
	column_major float4x4 InvShadowProj[2];

	float2 EndSplitDistances;
	float2 StartSplitDistances;
};

StructuredBuffer<DirectionalShadowData> DirectionalShadows : register(t81);

Texture2DArray<float> DirectionalShadowCascades : register(t82);

struct ShadowData
{
	column_major float4x4 ShadowProj;
	column_major float4x4 InvShadowProj[2];

	float4 ShadowParam;
};

StructuredBuffer<ShadowData> Shadows    : register(t83);
Texture2DArray<float>        ShadowMaps : register(t84);

// Comparison sampler for PCF shadow filtering (less-equal depth test).
SamplerComparisonState ShadowSamplerCmp : register(s14);

#if defined(VOLUMETRIC_SHADOWS)
#	include "VolumetricShadows/VolumetricShadows.hlsli"
#endif

namespace ShadowSampling
{
	// PCF filter radii in UV space for Poisson disc samples.
	static const float PCFKernelDirectional = 1.0 / 2048.0;  // directional cascade maps
	static const float PCFKernelShadowLight = 1.0 / 1024.0;  // frustum/spot shadow maps
	static const float PCFParaboloidRadius  = 2.0;            // world-space jitter radius for paraboloid PCF

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
		float shadow = VolumetricShadows::GetVSMShadow3D(DirectionalShadows[0], startPosition, endPosition, noise, sampleCount, eyeIndex, vsmSurfaceShadow);
		surfaceShadow *= vsmSurfaceShadow;
		return worldShadow * shadow;
#else
		return worldShadow;
#endif
	}

	float GetLightingShadow(float3 worldPosition, uint eyeIndex, float2x2 rotationMatrix, out float detailedShadow)
	{
		DirectionalShadowData shadow = DirectionalShadows[0];

		float shadowMapDepth = length(worldPosition);

		if (shadowMapDepth > shadow.EndSplitDistances.y) {
			detailedShadow = 1.0;
			return 1.0;
		}

		worldPosition.xyz += FrameBuffer::CameraPosAdjust[0].xyz;

		// Reduce over distance
		float fade = saturate(shadowMapDepth / shadow.EndSplitDistances.y);

		// Compute cascade blend factor
		float cascadeSelect = smoothstep(shadow.StartSplitDistances.y, shadow.EndSplitDistances.x, shadowMapDepth);

		// Determine which cascade(s) to sample
		uint primaryCascade = cascadeSelect;
		bool needsBlending = (cascadeSelect > 0.0) && (cascadeSelect < 1.0);

		// Transform ray to light space for primary cascade
		float3 positionLS = mul(shadow.ShadowProj[primaryCascade], float4(worldPosition, 1)).xyz;

		// Sample primary cascade
		float visibility = dot(float4(DirectionalShadowCascades.GatherRed(LinearSampler, float3(positionLS.xy, primaryCascade)) > positionLS.z), 0.25);

		// Blend with secondary cascade if needed
		[branch]
		if (needsBlending) {
			uint secondaryCascade = 1 - primaryCascade;

			positionLS = mul(shadow.ShadowProj[secondaryCascade], float4(worldPosition, 1)).xyz;

			float visibilityBlend = dot(float4(DirectionalShadowCascades.GatherRed(LinearSampler, float3(positionLS.xy, secondaryCascade)) > positionLS.z), 0.25);
			visibility = lerp(visibility, visibilityBlend, cascadeSelect);
		}

		float fadeFactor = 1.0 - pow(fade, 8);
		detailedShadow = lerp(1.0, visibility, 1);
		return lerp(1.0, visibility, 1);
	}

	float GetSpotlightShadow(ShadowData shadow, uint shadowIndex, float4 positionLS, float2x2 rotationMatrix)
	{
		positionLS.xyz /= positionLS.w;

		float2 sampleUV = positionLS.xy * 0.5 + 0.5;
		float visibility = dot(float4(ShadowMaps.GatherRed(LinearSampler, float3(sampleUV.xy, shadowIndex)) > positionLS.z), 0.25);

		return visibility;
	}

	float GetHemisphereShadow(ShadowData shadow, uint shadowIndex, float4 positionLS)
	{
		if (positionLS.z * 0.5 + 0.5 >= 0) {
			positionLS.xyz /= positionLS.w;
			float3 lightDirection = normalize(normalize(positionLS.xyz) + float3(0, 0, 1));
			float2 sampleUV = lightDirection.xy / lightDirection.z * 0.5 + 0.5;
			positionLS.z = saturate(length(positionLS.xyz) / shadow.ShadowParam.y);
			
			float visibility = dot(float4(ShadowMaps.GatherRed(LinearSampler, float3(sampleUV.xy, shadowIndex)) > positionLS.z), 0.25);
			return visibility;
		}

		return 0.0;
	}

	float GetOmnidirectionalShadow(ShadowData shadow, uint shadowIndex, float3 positionLS)
	{
		bool lowerHalf = positionLS.z < 0;
		float3 normalizedPositionLS = normalize(positionLS);

		positionLS.z = saturate(length(positionLS) / shadow.ShadowParam.y);

		float3 positionOffset = lowerHalf ? float3(0, 0, -1) : float3(0, 0, 1);
		float3 lightDirection = normalize(normalizedPositionLS + positionOffset);
		float2 sampleUV = lightDirection.xy / lightDirection.z * 0.5 + 0.5;
		sampleUV.y = lowerHalf ? 1.0 - 0.5 * sampleUV.y : 0.5 * sampleUV.y;

		float visibility = dot(float4(ShadowMaps.GatherRed(LinearSampler, float3(sampleUV.xy, shadowIndex)) > positionLS.z), 0.25);

		return visibility;
	}

	float GetShadowLightShadow(uint shadowIndex, float3 worldPosition, float2x2 rotationMatrix)
	{
		ShadowData shadow = Shadows[shadowIndex];

		worldPosition.xyz += FrameBuffer::CameraPosAdjust[0].xyz;

		float4 positionLS = mul(shadow.ShadowProj, float4(worldPosition, 1));

		[branch]
		if (shadow.ShadowParam.x == 0)
			return GetSpotlightShadow(shadow, shadowIndex, positionLS, rotationMatrix);
		else if (shadow.ShadowParam.x  == 1)
			return GetHemisphereShadow(shadow, shadowIndex, positionLS);
		else if (shadow.ShadowParam.x  == 2)
			return GetOmnidirectionalShadow(shadow, shadowIndex, positionLS.xyz);

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
