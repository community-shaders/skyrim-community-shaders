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

Texture2DArray<float> EngineLocalShadowMaps : register(t5);
cbuffer EngineLocalShadowCB : register(b1)
{
	LightLimitFix::LocalShadowData EngineLocalShadows[4];
};
#endif
#if defined(PHYSICAL_SKY)
#	include "PhysicalSky/Common.hlsli"
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

float3 ComputeHistoryVolumeUVAndDepth(float3 positionWS, out bool validHistory, out float previousViewDepth)
{
	float3 previousPositionWS = positionWS + FrameBuffer::CameraPosAdjust.xyz - FrameBuffer::CameraPreviousPosAdjust.xyz;
	float4 previousClip = mul(FrameBuffer::CameraPreviousViewProjUnjittered, float4(previousPositionWS, 1.0f));

	previousViewDepth = abs(previousClip.w);
	validHistory = previousClip.w > 0.0f;
	if (!validHistory)
		return 0.0f.xxx;

	float2 historyUV = previousClip.xy / previousClip.w * float2(0.5f, -0.5f) + 0.5f;

	float historyZ = ExponentialHeightFog::ComputeVolumetricNormalizedSlice(previousViewDepth);
	float3 volumeUV = float3(historyUV, historyZ);
	validHistory = !any(volumeUV < 0.0f) && !any(volumeUV >= 1.0f);
	return saturate(volumeUV);
}

float3 ComputeHistoryVolumeUV(float3 positionWS, out bool validHistory)
{
	float previousViewDepth;
	return ComputeHistoryVolumeUVAndDepth(positionWS, validHistory, previousViewDepth);
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

#include "ExponentialHeightFog/VolumetricFogLighting.hlsli"

[numthreads(8, 8, 4)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	if (!ExponentialHeightFog::IsInsideVolumetricGrid(dispatchID))
		return;

	float viewDepth;
	float3 centerPositionWS = ExponentialHeightFog::ComputeCellWorldPosition(dispatchID, 0.5f.xxx, viewDepth);
	if (VolumetricFogHasConservativeDepth && IsFroxelBehindSceneDepth(dispatchID)) {
		LightScattering[dispatchID] = 0.0f.xxxx;
		return;
	}

	if (all(VBufferA[dispatchID] == 0.0f)) {
		LightScattering[dispatchID] = 0.0f.xxxx;
		return;
	}

	bool validHistory;
	float3 historyUV = ComputeHistoryVolumeUV(centerPositionWS, validHistory);
	if (VolumetricFogHasPrevConservativeDepth && validHistory) {
		float frontDepth;
		float3 frontPositionWS = ExponentialHeightFog::ComputeCellWorldPosition(dispatchID, float3(0.5f, 0.5f, -0.5f), frontDepth);
		bool validFrontHistory;
		float previousFrontDepth;
		ComputeHistoryVolumeUVAndDepth(frontPositionWS, validFrontHistory, previousFrontDepth);
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
