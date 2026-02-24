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

// Always include: provides DirectionalShadowData struct, SharedShadowData (t19), and VSM functions.
// VSM texture (t18) and sampling functions are only compiled when VOLUMETRIC_SHADOWS is defined.
#include "VolumetricShadows/VolumetricShadows.hlsli"

// Directional cascade raw depth maps (t20, t21) — used for PCF when VSM is not active.
Texture2D<float> DirectionalShadowCascade[2] : register(t20);

// Frustum (spot) shadow lights.
// t22: projection data (one element per active frustum light, max 4).
// t23-t26: depth maps indexed by per-type slot.
struct FrustumShadowData { float4x4 Proj; };
StructuredBuffer<FrustumShadowData> FrustumShadows    : register(t22);
Texture2D<float>                    FrustumShadowMap[4] : register(t23);

// Paraboloid (point) shadow lights: dual-hemisphere depth maps.
// t27: projection data (one element per active paraboloid light, max 4).
// t28-t31: front hemisphere depth maps, t32-t35: back hemisphere depth maps.
struct ParaboloidShadowData
{
	float4x4 FrontProj;  // world-to-shadow for front hemisphere
	float4x4 BackProj;   // world-to-shadow for back hemisphere
	uint     HasBack;    // 1 if back hemisphere map is bound
	float3   _pad;
};
StructuredBuffer<ParaboloidShadowData> ParaboloidShadows      : register(t27);
Texture2D<float>                       ParaboloidFrontMap[4]  : register(t28);
Texture2D<float>                       ParaboloidBackMap[4]   : register(t32);

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
		float shadow = VolumetricShadows::GetVSMShadow3D(startPosition, endPosition, noise, sampleCount, eyeIndex, vsmSurfaceShadow);
		surfaceShadow *= vsmSurfaceShadow;
		return worldShadow * shadow;
#else
		return worldShadow;
#endif
	}

	float GetLightingShadow(float3 worldPosition, uint eyeIndex, out float detailedShadow)
	{
#if defined(VOLUMETRIC_SHADOWS)
		// High-quality variance shadow map path (requires VolumetricShadows feature).
		return VolumetricShadows::GetVSMShadow2D(worldPosition, eyeIndex, detailedShadow);
#else
		// PCF fallback using directional cascade depth maps bound to t20/t21.
		VolumetricShadows::DirectionalShadowData sD = VolumetricShadows::SharedShadowData[0];

		float shadowMapDepth = VolumetricShadows::GetShadowDepth(worldPosition, eyeIndex);
		if (shadowMapDepth >= sD.EndSplitDistances.y) {
			detailedShadow = 1.0;
			return 1.0;
		}

		float fade = saturate(shadowMapDepth / sD.EndSplitDistances.y);
		float cascadeSelect = saturate((shadowMapDepth - sD.StartSplitDistances.y) /
		                               (sD.EndSplitDistances.x - sD.StartSplitDistances.y));
		uint primaryCascade = uint(cascadeSelect);

		// ShadowMapProj is in DirectX row-major convention; transpose for column-vector mul.
		float4x4 shadowProj = sD.ShadowMapProj[primaryCascade];
		float3   posLS      = mul(transpose(shadowProj), float4(worldPosition, 1)).xyz;
		posLS.xy            = saturate(posLS.xy);

		float shadowDepth = DirectionalShadowCascade[primaryCascade].SampleLevel(LinearSampler, posLS.xy, 0);
		float shadow      = (posLS.z <= shadowDepth + 0.0002) ? 1.0 : 0.0;

		float fadeFactor  = 1.0 - pow(fade, 8);
		detailedShadow    = lerp(1.0, shadow, fadeFactor);
		return lerp(1.0, shadow, fadeFactor);
#endif
	}

	// Sample a frustum (spot) shadow light's depth map.
	// frustumIdx: typed slot index within FrustumShadows / FrustumShadowMap.
	// Returns 1.0 (lit) or 0.0 (shadowed); 1.0 for out-of-frustum samples.
	float GetFrustumShadow(uint frustumIdx, float3 worldPosition)
	{
		FrustumShadowData light = FrustumShadows[frustumIdx];

		// Row-major convention: transpose for column-vector multiply.
		float4 posLS4 = mul(transpose(light.Proj), float4(worldPosition, 1));
		// Perspective divide — safe for spot (w != 1) and orthographic (w == 1).
		float3 posLS = posLS4.xyz / posLS4.w;

		[branch]
		if (any(posLS.xy < 0.0) || any(posLS.xy > 1.0) || posLS.z < 0.0 || posLS.z > 1.0)
			return 1.0;

		float shadowDepth = FrustumShadowMap[frustumIdx].SampleLevel(LinearSampler, posLS.xy, 0);
		return (posLS.z <= shadowDepth + 0.002) ? 1.0 : 0.0;
	}

	// Sample a paraboloid (point) shadow light's dual-hemisphere depth maps.
	// paraboloidIdx: typed slot index within ParaboloidShadows / ParaboloidFrontMap / ParaboloidBackMap.
	// Returns 1.0 (lit) or 0.0 (shadowed); 1.0 if outside both hemispheres.
	float GetParaboloidShadow(uint paraboloidIdx, float3 worldPosition)
	{
		ParaboloidShadowData light = ParaboloidShadows[paraboloidIdx];

		// Try front hemisphere.
		float4 posLS4 = mul(transpose(light.FrontProj), float4(worldPosition, 1));
		float3 posLS  = posLS4.xyz / posLS4.w;

		[branch]
		if (all(posLS.xy >= 0.0) && all(posLS.xy <= 1.0) && posLS.z >= 0.0 && posLS.z <= 1.0) {
			float shadowDepth = ParaboloidFrontMap[paraboloidIdx].SampleLevel(LinearSampler, posLS.xy, 0);
			return (posLS.z <= shadowDepth + 0.002) ? 1.0 : 0.0;
		}

		// Try back hemisphere if bound.
		[branch]
		if (light.HasBack) {
			posLS4 = mul(transpose(light.BackProj), float4(worldPosition, 1));
			posLS  = posLS4.xyz / posLS4.w;

			[branch]
			if (all(posLS.xy >= 0.0) && all(posLS.xy <= 1.0) && posLS.z >= 0.0 && posLS.z <= 1.0) {
				float shadowDepth = ParaboloidBackMap[paraboloidIdx].SampleLevel(LinearSampler, posLS.xy, 0);
				return (posLS.z <= shadowDepth + 0.002) ? 1.0 : 0.0;
			}
		}

		return 1.0;
	}

	// Dispatch to GetFrustumShadow or GetParaboloidShadow based on game slot type.
	// gameSlot: the game's shadow-light index (0-3, from activeShadowLights order).
	// worldPosition: camera-relative world position of the surface point.
	// Returns 1.0 (lit) or 0.0 (shadowed); 1.0 if the slot is inactive.
	float GetShadowLightShadow(uint gameSlot, float3 worldPosition)
	{
		VolumetricShadows::DirectionalShadowData sD = VolumetricShadows::SharedShadowData[0];
		if (gameSlot >= sD.TotalCount)
			return 1.0;

		uint typedIdx = sD.TypedIndex[gameSlot];

		[branch]
		if (sD.LightIsParaboloid[gameSlot])
			return GetParaboloidShadow(typedIdx, worldPosition);
		else
			return GetFrustumShadow(typedIdx, worldPosition);
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
