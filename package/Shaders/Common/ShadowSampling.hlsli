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

// Only declared when volumetric shadows are active; avoids register(t80) conflict with
// TRUE_PBR LANDSCAPE TexLandDisplacement0Sampler in Lighting.hlsl.
#if defined(VOLUMETRIC_SHADOWS)
Texture2D<float2> SharedShadowMap : register(t80);
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

	float4 ShadowParam;
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
		// Shadow depth bias — applied before depth comparisons to prevent self-shadowing acne.
		static const float ShadowBiasConst = 0.0005;  // constant component
		// Depth guard epsilons used to reject spotlight fragments at the near/far frustum planes.
		static const float ShadowDepthNearEps = 0.0001;
		static const float ShadowDepthFarEps = 0.9999;

		// Volumetric / 3D shadow ray-march parameters (world units).
		static const float ShadowRayLength = 128.0;   // default ray extent
		static const float ShadowRayStepSize = 32.0;  // fixed step size
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
		DirectionalShadowData shadow = DirectionalShadows[0];

		float shadowMapDepth = length(worldPosition);

		if (shadowMapDepth > shadow.EndSplitDistances.y)
			return 1.0;

		float fadeFactor = 1.0 - pow(saturate(dot(worldPosition.xyz, worldPosition.xyz) / shadow.EndSplitDistances.y), 8);

		worldPosition.xyz += FrameBuffer::CameraPosAdjust[eyeIndex].xyz;

		// Compute cascade blend factor
		float cascadeSelect = smoothstep(shadow.StartSplitDistances.y, shadow.EndSplitDistances.x, shadowMapDepth);

		// Determine which cascade(s) to sample
		uint primaryCascade = cascadeSelect;
		bool needsBlending = (cascadeSelect > 0.0) && (cascadeSelect < 1.0);

		// Transform ray to light space for primary cascade
		float3 positionLS = mul(shadow.ShadowProj[primaryCascade], float4(worldPosition, 1)).xyz;
		positionLS.z -= Constants::ShadowBiasConst;

		// Sample primary cascade
		uint onePlusLayerIndex = 1.0 + primaryCascade;
		float layerIndexRcp = rcp(onePlusLayerIndex);

		float ShadowSampleParamZ = 0.001; // // fPoissonRadiusScale / iShadowMapResolution in z and w

		float visibility = 0;

		for (int i = 0; i < 16; i++) {
			float2 sampleOffset = mul(Random::PoissonSampleOffsets16[i], rotationMatrix);
			float2 sampleUV = positionLS.xy + layerIndexRcp * sampleOffset * ShadowSampleParamZ;
			visibility += dot(float4(DirectionalShadowCascades.GatherRed(LinearSampler, float3(saturate(sampleUV), primaryCascade)) > positionLS.z), 0.25);
		}

		visibility /= 16.0;
	
		// Blend with secondary cascade if needed
		[branch] if (needsBlending)
		{
			uint secondaryCascade = 1 - primaryCascade;

			onePlusLayerIndex = 1.0 + secondaryCascade;
			layerIndexRcp = rcp(onePlusLayerIndex);

			positionLS = mul(shadow.ShadowProj[secondaryCascade], float4(worldPosition, 1)).xyz;
			positionLS.z -= Constants::ShadowBiasConst;

			float visibilityBlend = 0.0;
			
			for (int i = 0; i < 16; i++) {
				float2 sampleOffset = mul(Random::PoissonSampleOffsets16[i], rotationMatrix);
				float2 sampleUV = positionLS.xy + layerIndexRcp * sampleOffset * ShadowSampleParamZ;
				visibilityBlend += dot(float4(DirectionalShadowCascades.GatherRed(LinearSampler, float3(saturate(sampleUV), secondaryCascade)) > positionLS.z), 0.25);
			}

			visibilityBlend /= 16.0;

			visibility = lerp(visibility, visibilityBlend, cascadeSelect);
		}

		return lerp(visibility, 1.0, fadeFactor);
	}

	// --- Shadow helpers ---

	// Gather-based shadow sample: fetches 4 texels and compares to receiver depth.
	// receiverDepth should be pre-biased by the caller.
	float SampleShadowGather(uint shadowIndex, float2 uv, float receiverDepth)
	{
		float4 samples = ShadowMaps.GatherRed(LinearSampler, float3(uv, shadowIndex));
		return dot(float4(samples > receiverDepth), 0.25);
	}

	float SampleParaboloidShadow(uint shadowIndex, float2 sampleUV, float depth)
	{
		return SampleShadowGather(shadowIndex, sampleUV, depth - Constants::ShadowBiasConst);
	}

	// --- Per-light shadow sampling ---

	// hasCoverage: set to true when a PCF/shadow sample was actually taken; false on
	// early-exit boundary guards (behind frustum, outside cone, etc.).  Used by the
	// debug visualizer (LLFDEBUG) to distinguish "no coverage → dark" from
	// "in shadow → dark" so mode-5 doesn't show black everywhere under spot lights.
	float GetSpotlightShadow(ShadowData shadow, uint shadowIndex, float4 positionLS, out bool hasCoverage)
	{
		hasCoverage = false;

		// 1. Perspective Divide.
		if (positionLS.w <= 0)
			return 0.0;
		positionLS.xyz /= positionLS.w;

		// 2. Standard Depth Guard (with a small safety epsilon).
		if (positionLS.z < Constants::ShadowDepthNearEps || positionLS.z > Constants::ShadowDepthFarEps)
			return 0.0;

		// 3. Simple UV Mapping.
		float2 baseUV = positionLS.xy * 0.5 + 0.5;

		// 4. Border Guard.
		if (any(baseUV < 0.0) || any(baseUV > 1.0))
			return 0.0;

		// 5. Cone mask (circular spot area inside the frustum).
		float radialDistSq = dot(positionLS.xy, positionLS.xy);
		if (radialDistSq >= 1.0)
			return 0.0;

		hasCoverage = true;

		return SampleShadowGather(shadowIndex, baseUV, positionLS.z - Constants::ShadowBiasConst);
	}

	float GetHemisphereShadow(ShadowData shadow, uint shadowIndex, float4 positionLS)
	{
		// Initialize to unshadowed; geometry outside the paraboloid's coverage hemisphere is unshadowed.
		// Explicit initialization avoids X4000 "potentially uninitialized variable" from FXC.
		float result = 1.0;

		// positionLS.z > 0 means the point is in the forward hemisphere the paraboloid covers.
		if (positionLS.z > 0) {
			positionLS.xyz /= positionLS.w;
			float3 lightDirection = normalize(normalize(positionLS.xyz) + float3(0, 0, 1));
			float2 sampleUV = lightDirection.xy / lightDirection.z * 0.5 + 0.5;
			positionLS.z = saturate(length(positionLS.xyz) / shadow.ShadowParam.y);

			result = SampleParaboloidShadow(shadowIndex, sampleUV, positionLS.z);
		}

		return result;
	}

	float GetOmnidirectionalShadow(ShadowData shadow, uint shadowIndex, float3 positionLS)
	{
		bool lowerHalf = positionLS.z < 0;
		float3 normalizedPositionLS = normalize(positionLS);

		float depth = saturate(length(positionLS) / shadow.ShadowParam.y);

		float3 positionOffset = lowerHalf ? float3(0, 0, -1) : float3(0, 0, 1);
		float3 lightDirection = normalize(normalizedPositionLS + positionOffset);
		float2 sampleUV = lightDirection.xy / lightDirection.z * 0.5 + 0.5;
		sampleUV.y = lowerHalf ? 1.0 - 0.5 * sampleUV.y : 0.5 * sampleUV.y;

		return SampleParaboloidShadow(shadowIndex, sampleUV, depth);
	}

	// Returns the shadow factor for a point-light shadow slot.
	// hasCoverage is set to false when the pixel falls outside the spotlight frustum /
	// cone (early-exit with 0.0 return) so the debug visualizer can skip those pixels
	// in its min-shadow accumulation.  For hemi / omni lights hasCoverage is always true.
	float GetShadowLightShadow(uint shadowIndex, float3 worldPosition, uint eyeIndex, out bool hasCoverage)
	{
		hasCoverage = true;  // default: paraboloid lights always sample

		ShadowData shadow = Shadows[shadowIndex];

		// ShadowParam.y encodes slot state:
		//   == 0  : slot not written (capacity exceeded) → unshadowed (fully lit)
		//    < 0  : slot suppressed via debug overlay    → fully dark (light hidden)
		//    > 0  : valid radius                         → normal shadow test
		if (shadow.ShadowParam.y == 0)
			return 1.0;
		if (shadow.ShadowParam.y < 0)
			return 0.0;

		worldPosition.xyz += FrameBuffer::CameraPosAdjust[eyeIndex].xyz;

		float4 positionLS = mul(shadow.ShadowProj, float4(worldPosition, 1));

		[branch] if (shadow.ShadowParam.x == 0) return GetSpotlightShadow(shadow, shadowIndex, positionLS, hasCoverage);
		else if (shadow.ShadowParam.x == 1) return GetHemisphereShadow(shadow, shadowIndex, positionLS);
		else if (shadow.ShadowParam.x == 2) return GetOmnidirectionalShadow(shadow, shadowIndex, positionLS.xyz);

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
