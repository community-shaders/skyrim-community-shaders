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

// Comparison sampler for PCF shadow filtering (less-equal depth test).
SamplerComparisonState ShadowSamplerCmp : register(s14);

#if defined(VOLUMETRIC_SHADOWS)
#	include "VolumetricShadows/VolumetricShadows.hlsli"
#endif

namespace ShadowSampling
{
	// -----------------------------------------------------------------------
	// Constants
	// -----------------------------------------------------------------------

	// PCF filter radii in UV space (base, before user KernelScale).
	static const float PCFKernelDirectional = 1.0 / 2048.0;  // directional cascade maps
	static const float PCFKernelShadowLight = 1.0 / 1024.0;  // frustum/spot shadow maps

	// Shadow depth bias — applied before depth comparisons to prevent self-shadowing acne.
	static const float ShadowBiasConst = 0.0005;    // constant component
	static const float ShadowBiasSlopeScale = 0.1;  // multiplier on ddx/ddy slope bias

	// Depth guard epsilons used to reject spotlight fragments at the near/far frustum planes.
	static const float ShadowDepthNearEps = 0.0001;
	static const float ShadowDepthFarEps = 0.9999;

	// Volumetric / 3D shadow ray-march parameters (world units).
	static const float ShadowRayLength = 128.0;   // default ray extent
	static const float ShadowRayStepSize = 32.0;  // fixed step size

	// -----------------------------------------------------------------------

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
		float viewRayLength = ShadowRayLength;
		float3 startPosition = positionWS;
		float3 endPosition = positionWS - viewDirection * viewRayLength;
#else
		float viewRayLength = ShadowRayLength;
		float3 startPosition = positionWS;
		float3 endPosition = positionWS + viewDirection * viewRayLength;
#endif

		float totalRayLength = distance(endPosition, startPosition);

		const float stepSize = ShadowRayStepSize;

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

	float GetLightingShadow(float3 worldPosition, uint eyeIndex, out float detailedShadow)
	{
		DirectionalShadowData shadow = DirectionalShadows[0];

		float shadowMapDepth = length(worldPosition);

		if (shadowMapDepth > shadow.EndSplitDistances.y) {
			detailedShadow = 1.0;
			return 1.0;
		}

		worldPosition.xyz += FrameBuffer::CameraPosAdjust[0].xyz;

		// Fade shadows out toward the cascade boundary so they dissolve cleanly.
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
		[branch] if (needsBlending)
		{
			uint secondaryCascade = 1 - primaryCascade;

			positionLS = mul(shadow.ShadowProj[secondaryCascade], float4(worldPosition, 1)).xyz;

			float visibilityBlend = dot(float4(DirectionalShadowCascades.GatherRed(LinearSampler, float3(positionLS.xy, secondaryCascade)) > positionLS.z), 0.25);
			visibility = lerp(visibility, visibilityBlend, cascadeSelect);
		}

		// detailedShadow exposes raw visibility for callers that need the un-faded value.
		detailedShadow = visibility;
		return lerp(visibility, 1.0, fade);
	}

	// --- PCF helpers ---

	// Gather-based shadow sample: fetches 4 texels and compares to receiver depth.
	// Used for all shadow filtering modes (single-tap and multi-tap PCF).
	// Avoids hardware PCF filtering artifacts while maintaining efficiency.
	// receiverDepth should be pre-biased by the caller (except for debug/constant-bias mode).
	float SampleShadowGather(uint shadowIndex, float2 uv, float receiverDepth)
	{
		float4 samples = ShadowMaps.GatherRed(LinearSampler, float3(uv, shadowIndex));
		return dot(float4(samples > receiverDepth), 0.25);
	}

	// Compute adaptive slope-scale bias from screen-space depth derivatives.
	// Call once per pixel; subtract the result from receiverDepth before any
	// shadow comparison to prevent self-shadowing acne on angled surfaces.
	// (pixel-shader only — uses ddx/ddy)
	float ComputeSlopeBias(float receiverDepth)
	{
		float2 deriv = float2(ddx(receiverDepth), ddy(receiverDepth));
		return ShadowBiasConst + max(abs(deriv.x), abs(deriv.y)) * ShadowBiasSlopeScale;
	}

	// 8-tap spiral PCF on a 2D shadow map slice (paraboloid UV space).
	// receiverDepth must be pre-biased by the caller.
	float PCFSpiral8(uint shadowIndex, float2 baseUV, float receiverDepth, float kernelRadius, float2x2 rotationMatrix)
	{
		float sum = 0.0;
		[unroll] for (int i = 0; i < 8; i++)
		{
			float2 offset = mul(rotationMatrix, Random::SpiralSampleOffsets8[i]) * kernelRadius;
			sum += SampleShadowGather(shadowIndex, baseUV + offset, receiverDepth);
		}
		return sum * rcp(8.0);
	}

	// 16-tap Poisson disc PCF on a 2D shadow map slice.
	float PCFPoisson16(uint shadowIndex, float2 baseUV, float receiverDepth, float kernelRadius, float2x2 rotationMatrix)
	{
		float sum = 0.0;
		[unroll] for (int i = 0; i < 16; i++)
		{
			float2 offset = mul(rotationMatrix, Random::PoissonSampleOffsets16[i]) * kernelRadius;
			sum += SampleShadowGather(shadowIndex, baseUV + offset, receiverDepth);
		}
		return sum * rcp(16.0);
	}

	// PCSS: blocker search → penumbra estimation → variable-width PCF.
	// Optimized PCSS Spotlight with Stabilized Blocker Search
	float PCSSSpotlight(uint shadowIndex, float2 baseUV, float receiverDepth, float2x2 rotationMatrix)
	{
		float lightSize = SharedData::lightLimitFixSettings.LightSize;
		float kernelScale = SharedData::lightLimitFixSettings.KernelScale;

		// Step 1: DPCF blocker search — single GatherRed (4 bilinear samples, 1 tex instruction).
		// Treyarch "Shadows of Cold War": replaces the separate 8-sample spiral pass.
		// The center-pixel gather naturally reduces acne without a wide search radius.
		// https://research.activision.com/publications/2021/10/shadows-of-cold-war--a-scalable-approach-to-shadowing
		float4 gathered = ShadowMaps.GatherRed(LinearSampler, float3(baseUV, shadowIndex));
		float4 isBlocker = float4(gathered < receiverDepth);
		float blockerCount = dot(isBlocker, 1.0);
		if (blockerCount == 0.0)
			return 1.0;  // fully lit — no occluders found

		// Step 2: penumbra width from receiver–blocker distance.
		// Guard avgBlockerDepth against zero (can occur when the shadow map stores 0 at the near plane).
		// Saturate penumbra to [0,1] so kernelRadius stays bounded regardless of lightSize/depth ratio.
		float avgBlockerDepth = dot(gathered * isBlocker, 1.0) / blockerCount;
		float penumbra = saturate((receiverDepth - avgBlockerDepth) / max(avgBlockerDepth, 1e-5) * lightSize);
		float kernelRadius = penumbra * PCFKernelShadowLight * kernelScale;

		// Step 3: PCF with contact-hardened radius.
		return PCFPoisson16(shadowIndex, baseUV, receiverDepth, kernelRadius, rotationMatrix);
	}

	// Dispatch the active filter mode for omnidirectional/paraboloid shadow maps.
	// Mode 0: single-tap gather-based PCF with slope bias
	// Mode 1: 8-tap spiral PCF with temporal rotation
	// Mode 2: PCSS contact-hardened soft shadows
	float SampleParaboloidShadow(uint shadowIndex, float2 sampleUV, float depth, float2x2 rotationMatrix)
	{
		uint mode = SharedData::lightLimitFixSettings.FilterMode;
		float kernelRadius = PCFKernelShadowLight * SharedData::lightLimitFixSettings.KernelScale;

		// Compute slope bias before branching to maintain quad coherence for gradient operations.
		float slopeBias = ComputeSlopeBias(depth);

		[branch] if (mode >= 1)
		{
			return PCFSpiral8(shadowIndex, sampleUV, depth - slopeBias, kernelRadius, rotationMatrix);
		}
		// Mode 0: single-tap with pre-computed bias
		return SampleShadowGather(shadowIndex, sampleUV, depth - slopeBias);
	}

	// --- Per-light shadow sampling ---

	float GetSpotlightShadow(ShadowData shadow, uint shadowIndex, float4 positionLS, float2x2 rotationMatrix)
	{
		// 1. Perspective Divide
		if (positionLS.w <= 0)
			return 0.0;
		positionLS.xyz /= positionLS.w;

		// 2. Standard Depth Guard (with a small safety epsilon)
		if (positionLS.z < ShadowDepthNearEps || positionLS.z > ShadowDepthFarEps)
			return 0.0;

		// 3. Simple UV Mapping (No flips yet, to test alignment)
		float2 baseUV = positionLS.xy * 0.5 + 0.5;

		// 4. Border Guard
		if (any(baseUV < 0.0) || any(baseUV > 1.0))
			return 0.0;

		// 5. Cone mask (circular spot area inside the frustum)
		float radialDistSq = dot(positionLS.xy, positionLS.xy);
		if (radialDistSq >= 1.0)
			return 0.0;

		float spotFalloff = saturate(1.0 - radialDistSq);
		spotFalloff = spotFalloff * spotFalloff;

		// Compute slope bias once for all modes.
		uint mode = SharedData::lightLimitFixSettings.FilterMode;
		float biasedDepth = positionLS.z - ComputeSlopeBias(positionLS.z);

		float shadowSample;
		[branch] if (mode == 2) shadowSample = PCSSSpotlight(shadowIndex, baseUV, biasedDepth, rotationMatrix);
		else if (mode == 1) shadowSample = PCFPoisson16(shadowIndex, baseUV, biasedDepth,
			PCFKernelShadowLight * SharedData::lightLimitFixSettings.KernelScale, rotationMatrix);
		else shadowSample = SampleShadowGather(shadowIndex, baseUV, biasedDepth);  // mode 0

		return shadowSample * spotFalloff;
	}

	float GetHemisphereShadow(ShadowData shadow, uint shadowIndex, float4 positionLS, float2x2 rotationMatrix)
	{
		// positionLS.z > 0 means the point is in the forward hemisphere the paraboloid covers.
		if (positionLS.z > 0) {
			positionLS.xyz /= positionLS.w;
			float3 lightDirection = normalize(normalize(positionLS.xyz) + float3(0, 0, 1));
			float2 sampleUV = lightDirection.xy / lightDirection.z * 0.5 + 0.5;
			positionLS.z = saturate(length(positionLS.xyz) / shadow.ShadowParam.y);

			return SampleParaboloidShadow(shadowIndex, sampleUV, positionLS.z, rotationMatrix);
		}

		// Geometry outside the paraboloid's coverage hemisphere is unshadowed.
		return 1.0;
	}

	float GetOmnidirectionalShadow(ShadowData shadow, uint shadowIndex, float3 positionLS, float2x2 rotationMatrix)
	{
		bool lowerHalf = positionLS.z < 0;
		float3 normalizedPositionLS = normalize(positionLS);

		float depth = saturate(length(positionLS) / shadow.ShadowParam.y);

		float3 positionOffset = lowerHalf ? float3(0, 0, -1) : float3(0, 0, 1);
		float3 lightDirection = normalize(normalizedPositionLS + positionOffset);
		float2 sampleUV = lightDirection.xy / lightDirection.z * 0.5 + 0.5;
		sampleUV.y = lowerHalf ? 1.0 - 0.5 * sampleUV.y : 0.5 * sampleUV.y;

		return SampleParaboloidShadow(shadowIndex, sampleUV, depth, rotationMatrix);
	}

	float GetShadowLightShadow(uint shadowIndex, float3 worldPosition, float2x2 rotationMatrix, uint eyeIndex = 0)
	{
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

		[branch] if (shadow.ShadowParam.x == 0) return GetSpotlightShadow(shadow, shadowIndex, positionLS, rotationMatrix);
		else if (shadow.ShadowParam.x == 1) return GetHemisphereShadow(shadow, shadowIndex, positionLS, rotationMatrix);
		else if (shadow.ShadowParam.x == 2) return GetOmnidirectionalShadow(shadow, shadowIndex, positionLS.xyz, rotationMatrix);

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
