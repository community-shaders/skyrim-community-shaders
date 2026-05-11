SamplerState LinearSampler : register(s0);
SamplerComparisonState ShadowSampler : register(s1);
Texture3D<float4> VBufferA : register(t0);
Texture2DArray<float4> DirectionalShadowMap : register(t1);
Texture3D<float4> LightScatteringHistory : register(t2);
Texture2D<float> ConservativeDepthTexture : register(t3);
Texture2D<float> PrevConservativeDepthTexture : register(t4);
RWTexture3D<float4> LightScattering : register(u0);

#include "ExponentialHeightFog/VolumetricFogCSCommon.hlsli"
#include "IBL/IBL.hlsli"
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

	float2 texelSize = rcp(float2(max(shadowWidth, 1), max(shadowHeight, 1)));
	float compareDepth = positionLS.z - SharedData::exponentialHeightFogSettings.volumetricShadowBias;

	float shadow = 0.0f;
	[unroll] for (int y = -1; y <= 1; y++)
	{
		[unroll] for (int x = -1; x <= 1; x++)
		{
			float2 uv = positionLS.xy + float2(x, y) * texelSize;
			shadow += DirectionalShadowMap.SampleCmpLevelZero(ShadowSampler, float3(uv, cascadeIndex), compareDepth).x;
		}
	}
	return shadow * rcp(9.0f);
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
	float cascadeSelect = saturate((shadowMapDepth - directionalShadowLightData.StartSplitDistances.y) / splitDenom);
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

float4 ComputeDirectionalLightScattering(uint3 coord, float3 cellOffset)
{
	uint eyeIndex;
	float viewDepth;
	float3 positionWS = ExponentialHeightFog::ComputeCellWorldPosition(coord, cellOffset, eyeIndex, viewDepth);

	float4 materialScatteringAndExtinction = VBufferA[coord];
	float extinction = materialScatteringAndExtinction.w;

	float3 viewDirection = normalize(positionWS);
	float phase = ExponentialHeightFog::HenyeyGreenstein(
		dot(normalize(SharedData::DirLightDirection.xyz), -viewDirection),
		SharedData::exponentialHeightFogSettings.volumetricFogScatteringDistribution);

	float directionalShadow = SampleDirectionalShadow(positionWS, eyeIndex);
	float3 directionalScattering =
		SharedData::DirLightColor.xyz *
		SharedData::exponentialHeightFogSettings.volumetricDirectionalScatteringIntensity *
		directionalShadow *
		phase *
		materialScatteringAndExtinction.rgb;

	float3 skyScattering = ComputeSkyLightScattering(positionWS, viewDirection, eyeIndex) *
	                       materialScatteringAndExtinction.rgb;

	float3 emissive = SharedData::exponentialHeightFogSettings.volumetricFogEmissive.rgb *
	                  SharedData::exponentialHeightFogSettings.volumetricFogEmissive.a *
	                  extinction;

	return float4(max(directionalScattering + skyScattering + emissive, 0.0f.xxx), extinction);
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

	uint sampleCount = (VolumetricFogHistoryWeight > 0.0f && validHistory) ? 1u : 4u;
	float4 scatteringAndExtinction = 0.0f.xxxx;
	[unroll] for (uint sampleIndex = 0; sampleIndex < 4; sampleIndex++)
	{
		if (sampleIndex < sampleCount) {
			scatteringAndExtinction += ComputeDirectionalLightScattering(dispatchID, VolumetricFogFrameJitterAndHistory[sampleIndex].xyz);
		}
	}
	scatteringAndExtinction *= rcp(float(sampleCount));

	[branch] if (VolumetricFogHistoryWeight > 0.0f && validHistory)
	{
		float4 history = LightScatteringHistory.SampleLevel(LinearSampler, historyUV, 0);
		scatteringAndExtinction = lerp(scatteringAndExtinction, history, VolumetricFogHistoryWeight);
	}

	LightScattering[dispatchID] = scatteringAndExtinction;
}
