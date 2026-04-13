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

// Only declared when volumetric shadows are active; the #if guard already prevents
// any conflict with TRUE_PBR LANDSCAPE shaders since VOLUMETRIC_SHADOWS is never
// defined in Lighting.hlsl permutations.
#if defined(VOLUMETRIC_SHADOWS)
Texture2D<float2> SharedShadowMap : register(t18);
#endif

// Directional (sun) shadow data: cascade split distances and projection matrices.
struct DirectionalShadowData
{
	column_major float4x4 ShadowProj[2];
	column_major float4x4 InvShadowProj[2];
	float2 EndSplitDistances;
	float2 StartSplitDistances;
};

StructuredBuffer<DirectionalShadowData> DirectionalShadows : register(t98);

Texture2DArray<float> DirectionalShadowCascades : register(t99);

struct ShadowData
{
	column_major float4x4 ShadowProj;
	column_major float4x4 InvShadowProj;
	float4 ShadowLightParam;
};

StructuredBuffer<ShadowData> Shadows : register(t100);
Texture2DArray<float> ShadowMaps : register(t101);

#if defined(VOLUMETRIC_SHADOWS)
#	include "VolumetricShadows/VolumetricShadows.hlsli"
#endif

namespace ShadowSampling
{
	namespace Constants
	{
		static const float DirectionalBias = (0.00025f) / 3.0f;

		// Shadow Radius for PCF
		static const float PCFRadius2D = 0.002;

		// Volumetric / 3D shadow ray-march parameters (world units).
		static const float ShadowRayLength = 128.0;
		static const float ShadowRayStepSize = 32.0;
	}

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
		float viewRayLength = Constants::ShadowRayLength;
		float3 startPosition = positionWS;
		float3 endPosition = positionWS - viewDirection * viewRayLength;
#else
		float viewRayLength = Constants::ShadowRayLength;
		float3 startPosition = positionWS;
		float3 endPosition = positionWS + viewDirection * viewRayLength;
#endif

		float totalRayLength = distance(endPosition, startPosition);

		const float stepSize = Constants::ShadowRayStepSize;

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

	float GetDirectionalShadow(float3 worldPosition, float2x2 rotationMatrix, uint eyeIndex)
	{
		DirectionalShadowData shadowData = DirectionalShadows[0];

		float shadowMapDepth = length(worldPosition);

		if (shadowMapDepth > shadowData.EndSplitDistances.y)
			return 1.0;

		float fadeFactor = 1.0 - pow(saturate(dot(worldPosition.xyz, worldPosition.xyz) / shadowData.EndSplitDistances.y), 8);

		worldPosition.xyz += FrameBuffer::CameraPosAdjust[eyeIndex].xyz;

		// Compute cascade blend factor
		float cascadeSelect = smoothstep(shadowData.StartSplitDistances.y, shadowData.EndSplitDistances.x, shadowMapDepth);

		// Determine which cascade(s) to sample
		uint primaryCascade = cascadeSelect;
		bool needsBlending = (cascadeSelect > 0.0) && (cascadeSelect < 1.0);

		// Transform ray to light space for primary cascade
		float3 positionLS = mul(shadowData.ShadowProj[primaryCascade], float4(worldPosition, 1)).xyz;
		positionLS.z -= Constants::DirectionalBias;

		// Sample primary cascade
		uint onePlusLayerIndex = 1.0 + primaryCascade;
		float layerIndexRcp = rcp(onePlusLayerIndex);

		float shadow = 0.0;

		[unroll] for (int i = 0; i < 8; i++)
		{
			float2 sampleOffset = mul(Random::SpiralSampleOffsets8[i], rotationMatrix);
			float2 sampleUV = positionLS.xy + layerIndexRcp * sampleOffset * Constants::PCFRadius2D;
			shadow += dot(float4(DirectionalShadowCascades.GatherRed(LinearSampler, float3(saturate(sampleUV), primaryCascade)) > positionLS.z), 0.25);
		}

		shadow /= 8.0;

		// Blend with secondary cascade if needed
		[branch] if (needsBlending)
		{
			uint secondaryCascade = 1 - primaryCascade;

			onePlusLayerIndex = 1.0 + secondaryCascade;
			layerIndexRcp = rcp(onePlusLayerIndex);

			positionLS = mul(shadowData.ShadowProj[secondaryCascade], float4(worldPosition, 1)).xyz;
			positionLS.z -= Constants::DirectionalBias;

			float shadowBlend = 0.0;

			[unroll] for (int i = 0; i < 8; i++)
			{
				float2 sampleOffset = mul(Random::SpiralSampleOffsets8[i], rotationMatrix);
				float2 sampleUV = positionLS.xy + layerIndexRcp * sampleOffset * Constants::PCFRadius2D;
				shadowBlend += dot(float4(DirectionalShadowCascades.GatherRed(LinearSampler, float3(saturate(sampleUV), secondaryCascade)) > positionLS.z), 0.25);
			}

			shadowBlend /= 8.0;

			shadow = lerp(shadow, shadowBlend, cascadeSelect);
		}

		return lerp(shadow, 1.0, fadeFactor);
	}

	float SampleShadowGather(uint shadowIndex, float2 uv, float receiverDepth)
	{
		float4 samples = ShadowMaps.GatherRed(LinearSampler, float3(uv, shadowIndex));
		return dot(float4(samples > receiverDepth), 0.25);
	}

	float GetSpotlightShadow(ShadowData shadowData, uint shadowIndex, float4 positionLS)
	{
		positionLS.xyz /= positionLS.w;

		positionLS.xy = positionLS.xy * 0.5 + 0.5;

		float shadow = 0.0;

		[unroll] for (int i = 0; i < 8; i++)
		{
			float2 sampleOffset = Random::SpiralSampleOffsets8[i];
			float2 sampleUV = positionLS.xy + sampleOffset * Constants::PCFRadius2D;
			shadow += SampleShadowGather(shadowIndex, sampleUV, positionLS.z);
		}

		return shadow / 8.0;
	}

	float SampleParaboloidShadow(uint shadowIndex, float2 sampleUV, float depth)
	{
		float shadow = 0.0;

		[unroll] for (int i = 0; i < 8; i++)
		{
			float2 offset = Random::SpiralSampleOffsets8[i] * Constants::PCFRadius2D;
			float2 uv = sampleUV + offset;

			// Clamp to the correct paraboloid half
			uv.y = (sampleUV.y >= 0.5) ? max(uv.y, 0.5) : min(uv.y, 0.5);

			shadow += SampleShadowGather(shadowIndex, uv, depth);
		}

		return shadow / 8.0;
	}

	float GetOmnidirectionalShadow(ShadowData shadowData, uint shadowIndex, float4 positionLS)
	{
		bool lowerHalf = positionLS.z < 0;

		// Hemisphere-only early out
		if (!lowerHalf && positionLS.z <= 0)
			return 1.0;

		positionLS.xyz /= positionLS.w;

		float3 posOffset = lowerHalf ? float3(0, 0, -1) : float3(0, 0, 1);
		float3 lightDirection = normalize(normalize(positionLS.xyz) + posOffset);
		float2 sampleUV = lightDirection.xy / lightDirection.z * 0.5 + 0.5;
		sampleUV.y = lowerHalf ? 1.0 - 0.5 * sampleUV.y : 0.5 * sampleUV.y;

		float depth = saturate(length(positionLS.xyz) / shadowData.ShadowLightParam.y);
		depth -= shadowData.ShadowLightParam.z;

		return SampleParaboloidShadow(shadowIndex, sampleUV, depth);
	}

	// Returns the shadow factor for a point-light shadow slot.
	// hasCoverage is set to false when the pixel falls outside the spotlight frustum /
	// cone (early-exit with 0.0 return) so the debug visualizer can skip those pixels
	// in its min-shadow accumulation.  For hemi / omni lights hasCoverage is always true.
	float GetShadowLightShadow(uint shadowIndex, float3 worldPosition, uint eyeIndex, out bool hasCoverage)
	{
		hasCoverage = true;  // default: paraboloid lights always sample

		ShadowData shadowData = Shadows[shadowIndex];

		// ShadowLightParam.y encodes slot state:
		//   == 0  : slot not written (capacity exceeded) → unshadowed (fully lit)
		//    < 0  : slot suppressed via debug overlay    → fully dark (light hidden)
		//    > 0  : valid radius                         → normal shadow test
		[flatten] if (shadowData.ShadowLightParam.y == 0) return 1.0;
		[flatten] if (shadowData.ShadowLightParam.y < 0) return 0.0;

		worldPosition.xyz += FrameBuffer::CameraPosAdjust[eyeIndex].xyz;

		float4 positionLS = mul(shadowData.ShadowProj, float4(worldPosition, 1));

		[branch] if (shadowData.ShadowLightParam.x == 0)
		{
			float shadowBaseVisibility = GetSpotlightShadow(shadowData, shadowIndex, positionLS);
			positionLS.xyz /= positionLS.w;

			float spotFalloff = saturate(1.0 - dot(positionLS.xy, positionLS.xy));
			spotFalloff = spotFalloff * spotFalloff;

			return shadowBaseVisibility * spotFalloff;
		}

		return GetOmnidirectionalShadow(shadowData, shadowIndex, positionLS);
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
