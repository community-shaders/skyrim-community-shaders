SamplerState LinearSampler : register(s0);
SamplerComparisonState ShadowSampler : register(s1);
Texture3D<float4> VBufferA : register(t0);
Texture2DArray<float4> DirectionalShadowMap : register(t1);
Texture3D<float4> LightScatteringHistory : register(t2);
Texture2D<float> ConservativeDepthTexture : register(t3);
Texture2D<float> PrevConservativeDepthTexture : register(t4);
RWTexture3D<float4> LightScattering : register(u0);

#include "Common/Random.hlsli"
#include "ExponentialHeightFog/VolumetricFogCSCommon.hlsli"
#include "IBL/IBL.hlsli"
#if defined(TERRAIN_SHADOWS)
#	include "TerrainShadows/TerrainShadows.hlsli"
#endif
#if defined(CLOUD_SHADOWS)
#	include "CloudShadows/CloudShadows.hlsli"
#endif
#if defined(LIGHT_LIMIT_FIX)
#	include "LightLimitFix/LightLimitFix.hlsli"
#	include "InverseSquareLighting/InverseSquareLighting.hlsli"
#endif
#define SKYLIGHTING_PROBE_REGISTER t50
#include "Skylighting/Skylighting.hlsli"

struct DirectionalShadowLightData
{
	column_major float4x4 ShadowProj[2];
	column_major float4x4 InvShadowProj[2];
	float2 EndSplitDistances;
	float2 StartSplitDistances;
};

StructuredBuffer<DirectionalShadowLightData> DirectionalShadowLights : register(t98);

// 4D PCG hash matching UE's Rand4DPCG32 (jcgt.org/published/0009/03/02/)
uint4 Rand4DPCG32(int4 p)
{
	uint4 v = uint4(p);
	v = v * 1664525u + 1013904223u;
	v.x += v.y * v.w;
	v.y += v.z * v.x;
	v.z += v.x * v.y;
	v.w += v.y * v.z;
	v ^= (v >> 16u);
	v.x += v.y * v.w;
	v.y += v.z * v.x;
	v.z += v.x * v.y;
	v.w += v.y * v.z;
	return v;
}

// Matches UE's MakePositiveFinite - ensures no NaN/Inf propagates into history chain
float4 MakePositiveFinite(float4 v)
{
	v = max(v, 0.0f.xxxx);
	v.x = isfinite(v.x) ? v.x : 0.0f;
	v.y = isfinite(v.y) ? v.y : 0.0f;
	v.z = isfinite(v.z) ? v.z : 0.0f;
	v.w = isfinite(v.w) ? v.w : 0.0f;
	return v;
}

bool IsFroxelBehindSceneDepth(uint3 coord)
{
	float frontDepth = ExponentialHeightFog::ComputeVolumetricSliceDepth(max(float(coord.z) - 0.5f, 0.0f));
	float sceneDepth = ConservativeDepthTexture[coord.xy];
	return sceneDepth < frontDepth;
}

float3 ComputeHistoryVolumeUVAndDepth(float3 positionWS, uint eyeIndex, out bool validHistory, out float previousViewDepth)
{
	float3 previousPositionWS = positionWS + FrameBuffer::CameraPosAdjust[eyeIndex].xyz - FrameBuffer::CameraPreviousPosAdjust[eyeIndex].xyz;
	float4 previousClip = mul(FrameBuffer::CameraPreviousViewProjUnjittered[eyeIndex], float4(previousPositionWS, 1.0f));

	previousViewDepth = abs(previousClip.w);
	validHistory = previousClip.w > 0.0f;
	if (!validHistory)
		return 0.0f.xxx;

	float2 historyUV = previousClip.xy / previousClip.w * float2(0.5f, -0.5f) + 0.5f;
#if defined(VR)
	historyUV = Stereo::ConvertToStereoUV(historyUV, eyeIndex);
#endif

	float historyZ = ExponentialHeightFog::ComputeVolumetricNormalizedSlice(previousViewDepth);
	float3 volumeUV = float3(historyUV, historyZ);
	validHistory = !any(volumeUV < 0.0f) && !any(volumeUV >= 1.0f);
	return saturate(volumeUV);
}

float3 ComputeHistoryVolumeUV(float3 positionWS, uint eyeIndex, out bool validHistory)
{
	float previousViewDepth;
	return ComputeHistoryVolumeUVAndDepth(positionWS, eyeIndex, validHistory, previousViewDepth);
}

float2 FixupHistoryUV(float2 uv, float previousCellDepth, out bool validHistory)
{
	float2 size = float2(VolumetricFogGridSize.xy);
	float2 fullResUV = uv * size;
	float2 screenCoord = floor(fullResUV - 0.5f);
	float2 fullResOffset = fullResUV - screenCoord;
	float2 gatherUV = (screenCoord + 1.0f) / size;

	float4 previousSceneDepths = PrevConservativeDepthTexture.Gather(LinearSampler, gatherUV);
	bool4 validSamples = previousSceneDepths >= previousCellDepth;

	validHistory = true;
	if (all(validSamples))
		return uv;

	if (all(validSamples.wz))
		return (screenCoord + float2(fullResOffset.x, 0.5f)) / size;
	if (all(validSamples.xy))
		return (screenCoord + float2(fullResOffset.x, 1.5f)) / size;
	if (all(validSamples.wx))
		return (screenCoord + float2(0.5f, fullResOffset.y)) / size;
	if (all(validSamples.zy))
		return (screenCoord + float2(1.5f, fullResOffset.y)) / size;

	if (validSamples.x)
		return (screenCoord + float2(0.5f, 1.5f)) / size;
	if (validSamples.y)
		return (screenCoord + float2(1.5f, 1.5f)) / size;
	if (validSamples.w)
		return (screenCoord + float2(0.5f, 0.5f)) / size;
	if (validSamples.z)
		return (screenCoord + float2(1.5f, 0.5f)) / size;

	validHistory = false;
	return uv;
}

float SampleDirectionalShadowPCF(float3 positionLS, uint cascadeIndex)
{
	uint shadowWidth;
	uint shadowHeight;
	uint shadowSlices;
	DirectionalShadowMap.GetDimensions(shadowWidth, shadowHeight, shadowSlices);
	if (cascadeIndex >= shadowSlices)
		return 1.0f;

	float2 texelSize = rcp(float2(max(shadowWidth, 1), max(shadowHeight, 1)));
	float compareDepth = positionLS.z - SharedData::exponentialHeightFogSettings.volumetricShadowBias;

	float2 uvMin = texelSize * 1.5f;
	float2 uvMax = 1.0f.xx - uvMin;
	if (any(positionLS.xy < uvMin) || any(positionLS.xy > uvMax))
		return DirectionalShadowMap.SampleCmpLevelZero(ShadowSampler, float3(saturate(positionLS.xy), cascadeIndex), compareDepth).x;

	float center = DirectionalShadowMap.SampleCmpLevelZero(ShadowSampler, float3(positionLS.xy, cascadeIndex), compareDepth).x;
	float cross =
		DirectionalShadowMap.SampleCmpLevelZero(ShadowSampler, float3(positionLS.xy + float2(texelSize.x, 0.0f), cascadeIndex), compareDepth).x +
		DirectionalShadowMap.SampleCmpLevelZero(ShadowSampler, float3(positionLS.xy - float2(texelSize.x, 0.0f), cascadeIndex), compareDepth).x +
		DirectionalShadowMap.SampleCmpLevelZero(ShadowSampler, float3(positionLS.xy + float2(0.0f, texelSize.y), cascadeIndex), compareDepth).x +
		DirectionalShadowMap.SampleCmpLevelZero(ShadowSampler, float3(positionLS.xy - float2(0.0f, texelSize.y), cascadeIndex), compareDepth).x;

	return (center * 4.0f + cross) * rcp(8.0f);
}

float SampleDirectionalShadow(float3 positionWS, uint eyeIndex)
{
	if (SharedData::InInterior || SharedData::HideSky || SharedData::InMapMenu)
		return 1.0f;
	if (!VolumetricFogHasDirectionalShadowMap)
		return 1.0f;

	DirectionalShadowLightData directionalShadowLightData = DirectionalShadowLights[0];
	float shadowMapDepth = SharedData::GetScreenDepth(FrameBuffer::GetShadowDepth(positionWS, eyeIndex));
	if (shadowMapDepth >= directionalShadowLightData.EndSplitDistances.y)
		return 1.0f;

	float splitDenom = max(directionalShadowLightData.EndSplitDistances.x - directionalShadowLightData.StartSplitDistances.y, 1e-4f);
	float cascadeSelect = smoothstep(0.0f, 1.0f, saturate((shadowMapDepth - directionalShadowLightData.StartSplitDistances.y) / splitDenom));
	uint primaryCascade = (uint)cascadeSelect;

	float3 absolutePositionWS = positionWS + FrameBuffer::CameraPosAdjust[eyeIndex].xyz;
	float3 positionLS = mul(directionalShadowLightData.ShadowProj[primaryCascade], float4(absolutePositionWS, 1.0f)).xyz;
	if (any(positionLS.xy < 0.0f) || any(positionLS.xy > 1.0f))
		return 1.0f;

	float shadow = SampleDirectionalShadowPCF(positionLS, primaryCascade);

	[branch] if (cascadeSelect > 0.0f && cascadeSelect < 1.0f)
	{
		uint secondaryCascade = 1u - primaryCascade;
		float3 secondaryLS = mul(directionalShadowLightData.ShadowProj[secondaryCascade], float4(absolutePositionWS, 1.0f)).xyz;
		if (!any(secondaryLS.xy < 0.0f) && !any(secondaryLS.xy > 1.0f)) {
			float secondaryShadow = SampleDirectionalShadowPCF(secondaryLS, secondaryCascade);
			shadow = lerp(shadow, secondaryShadow, cascadeSelect);
		}
	}

	float fade = saturate(shadowMapDepth / max(directionalShadowLightData.EndSplitDistances.y, 1.0f));
	float fadeFactor = 1.0f - pow(fade * fade, 8.0f);
	return lerp(1.0f, shadow, fadeFactor);
}

float SampleDirectionalWorldShadow(float3 positionWS, uint eyeIndex)
{
	if (SharedData::InInterior || SharedData::HideSky || SharedData::InMapMenu)
		return 1.0f;

	float worldShadow = 1.0f;
#if defined(TERRAIN_SHADOWS)
	worldShadow *= TerrainShadows::GetTerrainShadow(positionWS + FrameBuffer::CameraPosAdjust[eyeIndex].xyz, LinearSampler);
#endif
#if defined(CLOUD_SHADOWS)
	worldShadow *= CloudShadows::GetCloudShadowMult(positionWS, LinearSampler);
#endif
	return worldShadow;
}

float3 ComputeSkyLightScattering(float3 positionWS, float3 viewDirection, uint eyeIndex)
{
	float phaseG = SharedData::exponentialHeightFogSettings.volumetricFogScatteringDistribution;
	float3 skyDirection = abs(phaseG) > 0.001f ? normalize(-viewDirection * phaseG) : 0.0f.xxx;
	float3 skyVisibilityDirection = abs(phaseG) > 0.001f ? skyDirection : float3(0.0f, 0.0f, 1.0f);
	float skyVisibility = 1.0f;
	if (VolumetricFogHasSkylighting && !SharedData::InInterior) {
#if defined(VR)
		float3 skylightingPosition = positionWS + FrameBuffer::CameraPosAdjust[eyeIndex].xyz - FrameBuffer::CameraPosAdjust[0].xyz;
#else
		float3 skylightingPosition = positionWS;
#endif
		sh2 skylightingSH = Skylighting::SampleNoBias(skylightingPosition);
		skyVisibility = Skylighting::EvaluateDiffuse(skylightingSH, skyVisibilityDirection, Skylighting::GetFadeOutFactor(skylightingPosition));
	}

	float3 skyLighting =
		SharedData::exponentialHeightFogSettings.fogInscatteringColor.rgb *
		SharedData::exponentialHeightFogSettings.fogInscatteringColor.a *
		skyVisibility;
	[branch] if (VolumetricFogHasIBL)
		skyLighting = ImageBasedLighting::GetIBLColorOccluded(skyDirection, skyVisibility);

	return skyLighting *
	       SharedData::exponentialHeightFogSettings.volumetricSkyLightingIntensity;
}

#if defined(LIGHT_LIMIT_FIX)
float ComputeLocalLightAttenuation(float distanceSqr, float cellRadius, LightLimitFix::Light light)
{
	float distance = sqrt(max(distanceSqr, 1e-6f));

	// UE biases local light integration by froxel size to avoid singular bright voxels close to the light.
	if (light.lightFlags & LightLimitFix::LightFlags::InverseSquare) {
		distance = sqrt(max(distanceSqr, cellRadius * cellRadius));
	}

	return InverseSquareLighting::GetAttenuation(distance, light);
}

float3 AccumulateLocalLightScattering(
	uint3 coord,
	float3 cellOffset,
	float3 positionWS,
	float viewDepth,
	float3 viewDirection,
	uint eyeIndex,
	float3 materialScattering)
{
	if (!VolumetricFogHasLocalLights)
		return 0.0f.xxx;

	float2 volumeUV = (float2(coord.xy) + cellOffset.xy) * VolumetricFogInvGridSize.xy;
	float2 screenUV = Stereo::ConvertFromStereoUV(volumeUV, eyeIndex);

	uint clusterIndex = 0;
	if (!LightLimitFix::GetClusterIndex(screenUV, viewDepth, clusterIndex))
		return 0.0f.xxx;

	LightLimitFix::LightGrid grid = LightLimitFix::lightGrid[clusterIndex];
	uint lightCount = min(grid.lightCount, (uint)MAX_CLUSTER_LIGHTS);

	uint cornerEyeIndex;
	float cornerViewDepth;
	float3 cellCornerWS = ExponentialHeightFog::ComputeCellWorldPosition(coord + uint3(1, 1, 1), cellOffset, cornerEyeIndex, cornerViewDepth);
	float cellRadius = max(length(cellCornerWS - positionWS), 1.0f);

	float phaseG = SharedData::exponentialHeightFogSettings.volumetricFogScatteringDistribution;
	float3 localScattering = 0.0f.xxx;
	[loop] for (uint lightIndex = 0; lightIndex < lightCount; lightIndex++)
	{
		uint clusteredLightIndex = LightLimitFix::lightList[grid.offset + lightIndex];
		LightLimitFix::Light light = LightLimitFix::lights[clusteredLightIndex];

		if (light.lightFlags & LightLimitFix::LightFlags::Disabled)
			continue;

		float3 toLight = light.positionWS[eyeIndex].xyz - positionWS;
		float distanceSqr = dot(toLight, toLight);
		if (distanceSqr < 1e-6f)
			continue;

		float attenuation = ComputeLocalLightAttenuation(distanceSqr, cellRadius, light);
		if (attenuation < 1e-5f)
			continue;

		float3 L = toLight * rsqrt(distanceSqr);
		float phase = ExponentialHeightFog::HenyeyGreenstein(dot(L, -viewDirection), phaseG);

		const bool isPointLightLinear = light.lightFlags & LightLimitFix::LightFlags::Linear;
		float3 lightColor = Color::PointLight(light.color.xyz, isPointLightLinear) * attenuation * light.fade;
		localScattering += lightColor * phase;
	}

	return localScattering *
	       SharedData::exponentialHeightFogSettings.volumetricLocalLightScatteringIntensity *
	       materialScattering;
}
#else
float3 AccumulateLocalLightScattering(
	uint3 coord,
	float3 cellOffset,
	float3 positionWS,
	float viewDepth,
	float3 viewDirection,
	uint eyeIndex,
	float3 materialScattering)
{
	return 0.0f.xxx;
}
#endif

float4 ComputeLightScattering(uint3 coord, float3 cellOffset)
{
	uint eyeIndex;
	float viewDepth;
	float3 positionWS = ExponentialHeightFog::ComputeCellWorldPosition(coord, cellOffset, eyeIndex, viewDepth);

	float4 materialScatteringAndExtinction = VBufferA[coord];
	float extinction = materialScatteringAndExtinction.w;

	float3 viewDirection = normalize(positionWS);

	// Directional light uses isotropic phase (1/4PI) in the volume to avoid angular aliasing
	// at the coarse froxel XY resolution. The actual per-pixel HG phase is applied at full
	// resolution during compositing in SampleVolumetricFog().
	float directionalPhase = 1.0f / (4.0f * Math::PI);

	float directionalShadow = SampleDirectionalShadow(positionWS, eyeIndex) *
	                          SampleDirectionalWorldShadow(positionWS, eyeIndex);
	float3 directionalScattering =
		SharedData::DirLightColor.xyz *
		SharedData::exponentialHeightFogSettings.volumetricDirectionalScatteringIntensity *
		directionalShadow *
		directionalPhase *
		materialScatteringAndExtinction.rgb;

	float3 skyScattering = ComputeSkyLightScattering(positionWS, viewDirection, eyeIndex) *
	                       materialScatteringAndExtinction.rgb;

	float3 localScattering = AccumulateLocalLightScattering(
		coord,
		cellOffset,
		positionWS,
		viewDepth,
		viewDirection,
		eyeIndex,
		materialScatteringAndExtinction.rgb);

	float3 emissive = SharedData::exponentialHeightFogSettings.volumetricFogEmissive.rgb *
	                  SharedData::exponentialHeightFogSettings.volumetricFogEmissive.a *
	                  extinction;

	return float4(max(directionalScattering + skyScattering + localScattering + emissive, 0.0f.xxx), extinction);
}

[numthreads(8, 8, 4)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	if (!ExponentialHeightFog::IsInsideVolumetricGrid(dispatchID))
		return;

	uint eyeIndex;
	float viewDepth;
	float3 centerPositionWS = ExponentialHeightFog::ComputeCellWorldPosition(dispatchID, 0.5f.xxx, eyeIndex, viewDepth);
	if (VolumetricFogHasConservativeDepth && IsFroxelBehindSceneDepth(dispatchID)) {
		LightScattering[dispatchID] = 0.0f.xxxx;
		return;
	}

	bool validHistory;
	float3 historyUV = ComputeHistoryVolumeUV(centerPositionWS, eyeIndex, validHistory);
	if (VolumetricFogHasPrevConservativeDepth && validHistory) {
		uint frontEyeIndex;
		float frontDepth;
		float3 frontPositionWS = ExponentialHeightFog::ComputeCellWorldPosition(dispatchID, float3(0.5f, 0.5f, -0.5f), frontEyeIndex, frontDepth);
		bool validFrontHistory;
		float previousFrontDepth;
		ComputeHistoryVolumeUVAndDepth(frontPositionWS, frontEyeIndex, validFrontHistory, previousFrontDepth);
		if (validFrontHistory) {
			historyUV.xy = saturate(FixupHistoryUV(historyUV.xy, previousFrontDepth, validHistory));
		} else {
			validHistory = false;
		}
	}

	float historyAlpha = VolumetricFogHistoryWeight;
	[flatten] if (!validHistory || any(historyUV < 0.0f) || any(historyUV >= 1.0f))
	{
		historyAlpha = 0.0f;
	}

	uint sampleCount = historyAlpha < 0.001f ? VolumetricFogHistoryMissSampleCount : 1u;
	float4 scatteringAndExtinction = 0.0f.xxxx;
	[loop] for (uint sampleIndex = 0; sampleIndex < sampleCount; sampleIndex++)
	{
		// Per-voxel random noise matching UE's LightScatteringCS:
		// Rand4DPCG32(int4(GridCoordinate.xyz, StateFrameIndexMod8 + 8 * SampleIndex))
		// This decorrelates the jitter pattern across voxels, preventing coherent temporal artifacts
		uint3 Rand32Bits = Rand4DPCG32(int4(dispatchID.xyz, VolumetricFogStateFrameIndexMod8 + 8 * sampleIndex)).xyz;
		float3 Rand3D = (float3(Rand32Bits) / float(uint(0xffffffff))) * 2.0f - 1.0f;
		float3 cellOffset = VolumetricFogFrameJitterOffsets[sampleIndex].xyz + VolumetricFogSampleJitterMultiplier * Rand3D;

		scatteringAndExtinction += ComputeLightScattering(dispatchID, cellOffset);
	}
	scatteringAndExtinction *= rcp(float(sampleCount));

	[branch] if (historyAlpha > 0.0f)
	{
		float4 history = LightScatteringHistory.SampleLevel(LinearSampler, historyUV, 0);
		// Sanitize history to prevent NaN/Inf propagation in the temporal chain
		history = MakePositiveFinite(history);
		scatteringAndExtinction = lerp(scatteringAndExtinction, history, historyAlpha);
	}

	LightScattering[dispatchID] = MakePositiveFinite(scatteringAndExtinction);
}
