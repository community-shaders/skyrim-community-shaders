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

StructuredBuffer<ShadowData> Shadows    : register(t100);
Texture2DArray<float>        ShadowMaps : register(t101);

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
		for(uint i = 0; i < sampleCount; i++){
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


	float GetDirectionalShadowVisibility(float3 positionLS, float3 lightPosition, uint cascade, float searchRadius, float kernelScale, float2x2 rotationMatrix, float receiverDepth)
	{
		float blockerSum   = 0.0;
		uint  blockerCount = 0;
		[unroll] for (uint i = 0; i < 8; i++) {
			float2 offset = mul(rotationMatrix, Random::SpiralSampleOffsets8[i]) * searchRadius;
			float2 uv = saturate(positionLS.xy + offset);
			float4 blockerDepths = DirectionalShadowCascades.GatherRed(LinearSampler, float3(uv, cascade), 0);
			[unroll] for(uint k = 0; k < 4; k++)
			{
				float4 blockedPosition = float4(2 * float2(uv.x, -uv.y + 1) - 1, blockerDepths[k], 1);
				blockedPosition = mul(shadow.InvShadowProj[cascade], blockedPosition);
				blockedPosition.xyz = blockedPosition.xyz / blockedPosition.w;		
				float blockerDepth = distance(blockedPosition, lightPosition);
				if (blockerDepth < receiverDepth) {
					blockerSum += blockerDepth;
					blockerCount++;
				}
			}
		}

		if (blockerCount == 0) return 1.0;  // fully lit — no occluders found

		float avgBlockerDepth = blockerSum / float(blockerCount);
		float penumbra        = (receiverDepth - avgBlockerDepth) / avgBlockerDepth;
		float kernelRadius    = penumbra * kernelScale;

		float sum = 0.0;
		[unroll] for (int i = 0; i < 16; i++) {
			float2 offset = mul(rotationMatrix, Random::PoissonSampleOffsets16[i]) * kernelRadius;
			sum += dot(float4(DirectionalShadowCascades.GatherRed(LinearSampler, float3(positionLS.xy, cascade)) > positionLS.z), 0.25);
		}

		return sum / 16.0;
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
		float kernelScale = 0.001;

		// Step 1: blocker search with a small spiral kernel.
		float3 lightPosition = float3(0, 0, 100000000);
		float recieverDepth = distance(worldPosition, lightPosition);
		float searchRadius = 0.001;
		float kernelScale = 0.01;

		float visibility = GetDirectionalShadowVisibility(positionLS, lightPosition, primaryCascade, searchRadius, kernelScale, rotationMatrix, receiverDepth);

		// Blend with secondary cascade if needed
		[branch]
		if (needsBlending) {
			uint secondaryCascade = 1 - primaryCascade;

			positionLS = mul(shadow.ShadowProj[secondaryCascade], float4(worldPosition, 1)).xyz;

			float visibilityBlend = GetDirectionalShadowVisibility(positionLS, lightPosition, secondaryCascade, searchRadius, kernelScale, rotationMatrix, receiverDepth);
			visibility = lerp(visibility, visibilityBlend, cascadeSelect);
		}

		detailedShadow = lerp(1.0, visibility, 1);
		return lerp(1.0, visibility, 1);
	}

	// --- PCF helpers ---

	// Single-tap hardware-PCF sample (bilinear blend of 4 depth comparisons).
	float SampleShadowPCF(uint shadowIndex, float2 uv, float receiverDepth)
	{
		return ShadowMaps.SampleCmpLevelZero(ShadowSamplerCmp, float3(uv, shadowIndex), receiverDepth);
	}

	// Fast 2x2 gather-based sample (4 depth comparisons, averaged).
	float SampleShadowGather(uint shadowIndex, float2 uv, float receiverDepth)
	{
		return dot(float4(ShadowMaps.GatherRed(LinearSampler, float3(uv, shadowIndex)) > receiverDepth), 0.25);
	}

	// 8-tap spiral PCF on a 2D shadow map slice (paraboloid UV space).
	float PCFSpiral8(uint shadowIndex, float2 baseUV, float receiverDepth, float kernelRadius)
	{
		float sum = 0.0;
		[unroll] for (int i = 0; i < 8; i++) {
			float2 offset = Random::SpiralSampleOffsets8[i] * kernelRadius;
			sum += SampleShadowPCF(shadowIndex, baseUV + offset, receiverDepth);
		}
		return sum * rcp(8.0);
	}

	// 16-tap Poisson disc PCF on a 2D shadow map slice.
	float PCFPoisson16(uint shadowIndex, float2 baseUV, float receiverDepth, float kernelRadius, float2x2 rotationMatrix)
	{
		float sum = 0.0;
		[unroll] for (int i = 0; i < 16; i++) {
			float2 offset = mul(rotationMatrix, Random::PoissonSampleOffsets16[i]) * kernelRadius;
			sum += SampleShadowPCF(shadowIndex, baseUV + offset, receiverDepth);
		}
		return sum * rcp(16.0);
	}

	// PCSS: blocker search → penumbra estimation → variable-width PCF.
	float PCSSSpotlight(uint shadowIndex, float2 baseUV, float receiverDepth, float2x2 rotationMatrix)
	{
		float lightSize   = SharedData::lightLimitFixSettings.LightSize;
		float kernelScale = SharedData::lightLimitFixSettings.KernelScale;

		// Step 1: blocker search with a small spiral kernel.
		float searchRadius = lightSize * PCFKernelShadowLight;
		float blockerSum   = 0.0;
		int   blockerCount = 0;
		[unroll] for (int i = 0; i < 8; i++) {
			float2 offset        = mul(rotationMatrix, Random::SpiralSampleOffsets8[i]) * searchRadius;
			float  blockerDepth  = ShadowMaps.SampleLevel(LinearSampler, float3(baseUV + offset, shadowIndex), 0).r;
			if (blockerDepth < receiverDepth) {
				blockerSum += blockerDepth;
				blockerCount++;
			}
		}

		if (blockerCount == 0) return 1.0;  // fully lit — no occluders found

		// Step 2: penumbra width from receiver–blocker distance.
		float avgBlockerDepth = blockerSum / float(blockerCount);
		float penumbra        = (receiverDepth - avgBlockerDepth) / avgBlockerDepth * lightSize;
		float kernelRadius    = penumbra * PCFKernelShadowLight * kernelScale;

		// Step 3: PCF with contact-hardened radius.
		return PCFPoisson16(shadowIndex, baseUV, receiverDepth, kernelRadius, rotationMatrix);
	}

	// --- Per-light shadow sampling ---

	float GetSpotlightShadow(ShadowData shadow, uint shadowIndex, float4 positionLS, float2x2 rotationMatrix)
	{
		// Geometry behind the shadow frustum (w <= 0) would produce a sign-flipped
		// projection, yielding garbage UVs and a false visibility of 0 which makes
		// the light appear completely off.  Treat it as unshadowed instead.
		if (positionLS.w <= 0)
			return 1.0;

		positionLS.xyz /= positionLS.w;

		// Geometry beyond the shadow near/far planes is outside the depth range [0,1];
		// treat it as unshadowed rather than fully dark.
		if (positionLS.z < 0 || positionLS.z > 1)
			return 1.0;

		float2 baseUV = positionLS.xy * 0.5 + 0.5;
		uint   mode   = SharedData::lightLimitFixSettings.FilterMode;

		[branch]
		if (mode == 2)
			return PCSSSpotlight(shadowIndex, baseUV, positionLS.z, rotationMatrix);
		else if (mode == 1)
			return PCFPoisson16(shadowIndex, baseUV, positionLS.z,
				PCFKernelShadowLight * SharedData::lightLimitFixSettings.KernelScale, rotationMatrix);
		else
			return SampleShadowGather(shadowIndex, baseUV, positionLS.z);
	}

	float GetHemisphereShadow(ShadowData shadow, uint shadowIndex, float4 positionLS)
	{
		if (positionLS.z * 0.5 + 0.5 >= 0) {
			positionLS.xyz /= positionLS.w;
			float3 lightDirection = normalize(normalize(positionLS.xyz) + float3(0, 0, 1));
			float2 sampleUV = lightDirection.xy / lightDirection.z * 0.5 + 0.5;
			positionLS.z = saturate(length(positionLS.xyz) / shadow.ShadowParam.y);

			uint mode = SharedData::lightLimitFixSettings.FilterMode;
			float kernelRadius = PCFKernelShadowLight * SharedData::lightLimitFixSettings.KernelScale;
			[branch]
			if (mode >= 1)
				return PCFSpiral8(shadowIndex, sampleUV, positionLS.z, kernelRadius);
			else
				return SampleShadowGather(shadowIndex, sampleUV, positionLS.z);
		}

		// Geometry outside the paraboloid's coverage hemisphere is unshadowed.
		return 1.0;
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

		uint mode = SharedData::lightLimitFixSettings.FilterMode;
		float kernelRadius = PCFKernelShadowLight * SharedData::lightLimitFixSettings.KernelScale;
		[branch]
		if (mode >= 1)
			return PCFSpiral8(shadowIndex, sampleUV, depth, kernelRadius);
		else
			return SampleShadowGather(shadowIndex, sampleUV, depth);
	}

	float GetShadowLightShadow(uint shadowIndex, float3 worldPosition, float2x2 rotationMatrix, uint eyeIndex = 0)
	{
		ShadowData shadow = Shadows[shadowIndex];

		// ShadowParam.y holds the light radius, always > 0 for valid entries.
		// A zero radius means the slot was never written (e.g. shadowmapIndex exceeded
		// the buffer capacity), so treat it as unshadowed rather than fully dark.
		if (shadow.ShadowParam.y == 0)
			return 1.0;

		worldPosition.xyz += FrameBuffer::CameraPosAdjust[eyeIndex].xyz;

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
