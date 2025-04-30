#ifndef __SHADOW_SAMPLING_DEPENDENCY_HLSL__
#define __SHADOW_SAMPLING_DEPENDENCY_HLSL__

#include "Common/Math.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"

namespace ShadowSampling
{

	Texture2DArray<float4> SharedShadowMap : register(t18);

	struct ShadowData
	{
		float4 VPOSOffset;
		float4 ShadowSampleParam;    // fPoissonRadiusScale / iShadowMapResolution in z and w
		float4 EndSplitDistances;    // cascade end distances int xyz, cascade count int z
		float4 StartSplitDistances;  // cascade start ditances int xyz, 4 int z
		float4 FocusShadowFadeParam;
		float4 DebugColor;
		float4 PropertyColor;
		float4 AlphaTestRef;
		float4 ShadowLightParam;  // Falloff in x, ShadowDistance squared in z
		float4x3 FocusShadowMapProj[4];
		// Since ShadowData is passed between c++ and hlsl, can't have different defines due to strong typing
		float4x3 ShadowMapProj[2][3];
		float4x4 CameraViewProjInverse[2];
	};

	StructuredBuffer<ShadowData> SharedShadowData : register(t19);

	float GetShadowDepth(float3 positionWS, uint eyeIndex)
	{
		float4 positionCSShifted = mul(FrameBuffer::CameraViewProj[eyeIndex], float4(positionWS, 1));
		return positionCSShifted.z / positionCSShifted.w;
	}

	float Get3DFilteredShadow(float3 positionWS, float3 viewDirection, float2 screenPosition, uint eyeIndex)
	{
		ShadowData sD = SharedShadowData[0];

		float fadeFactor = 1.0 - pow(saturate(dot(positionWS, positionWS) / sD.ShadowLightParam.z), 8);
		uint sampleCount = ceil(8.0 * (1.0 - saturate(length(positionWS) / sqrt(sD.ShadowLightParam.z))));

		if (sampleCount == 0)
			return 1.0;

		float rcpSampleCount = rcp((float)sampleCount);

		uint3 seed = Random::pcg3d(uint3(screenPosition.xy, screenPosition.x * Math::PI));

		float2 compareValue;
		compareValue.x = mul(transpose(sD.ShadowMapProj[eyeIndex][0]), float4(positionWS, 1)).z - 0.01;
		compareValue.y = mul(transpose(sD.ShadowMapProj[eyeIndex][1]), float4(positionWS, 1)).z - 0.01;

		float shadow = 0.0;
		if (sD.EndSplitDistances.z >= GetShadowDepth(positionWS, eyeIndex)) {
			for (uint i = 0; i < sampleCount; i++) {
				float3 rnd = Random::R3Modified(i + SharedData::FrameCount * sampleCount, seed / 4294967295.f);

				// https://stats.stackexchange.com/questions/8021/how-to-generate-uniformly-distributed-points-in-the-3-d-unit-ball
				float phi = rnd.x * Math::TAU;
				float cos_theta = rnd.y * 2 - 1;
				float sin_theta = sqrt(1 - cos_theta);
				float r = rnd.z;
				float4 sincos_phi;
				sincos(phi, sincos_phi.y, sincos_phi.x);
				float3 sampleOffset = viewDirection * (float(i) - float(sampleCount) * 0.5) * 32 * rcpSampleCount;
				sampleOffset += float3(r * sin_theta * sincos_phi.x, r * sin_theta * sincos_phi.y, r * cos_theta) * 32;

				uint cascadeIndex = sD.EndSplitDistances.x < GetShadowDepth(positionWS.xyz + viewDirection * (sampleOffset.x + sampleOffset.y), eyeIndex);  // Stochastic cascade sampling

				float3 positionLS = mul(transpose(sD.ShadowMapProj[eyeIndex][cascadeIndex]), float4(positionWS + sampleOffset, 1));

				float4 depths = SharedShadowMap.GatherRed(LinearSampler, float3(saturate(positionLS.xy), cascadeIndex), 0);
				shadow += dot(depths > compareValue[cascadeIndex], 0.25);
			}
		} else {
			shadow = 1.0;
		}

		return lerp(1.0, shadow * rcpSampleCount, fadeFactor);
	}

	float Get2DFilteredShadowCascade(float noise, float2x2 rotationMatrix, float sampleOffsetScale, float2 baseUV, float cascadeIndex, float compareValue, uint eyeIndex)
	{
		const uint sampleCount = 16;

		float layerIndexRcp = rcp(1 + cascadeIndex);

		float visibility = 0.0;

#if defined(WATER)
		sampleOffsetScale *= 2.0;
#endif

		for (uint sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
			float2 sampleOffset = mul(Random::PoissonSampleOffsets16[sampleIndex], rotationMatrix);

			float2 sampleUV = layerIndexRcp * sampleOffset * sampleOffsetScale + baseUV;

			float4 depths = SharedShadowMap.GatherRed(LinearSampler, float3(saturate(sampleUV), cascadeIndex), 0);
			visibility += dot(depths > compareValue, 0.25);
		}

		return visibility * rcp((float)sampleCount);
	}

	float Get2DFilteredShadow(float noise, float2x2 rotationMatrix, float3 positionWS, uint eyeIndex)
	{
		ShadowData sD = SharedShadowData[0];

		float shadowMapDepth = GetShadowDepth(positionWS, eyeIndex);

		if (sD.EndSplitDistances.z >= shadowMapDepth) {
			float fadeFactor = 1 - pow(saturate(dot(positionWS.xyz, positionWS.xyz) / sD.ShadowLightParam.z), 8);

			float4x3 lightProjectionMatrix = sD.ShadowMapProj[eyeIndex][0];
			float cascadeIndex = 0;

			if (sD.EndSplitDistances.x < shadowMapDepth) {
				lightProjectionMatrix = sD.ShadowMapProj[eyeIndex][1];
				cascadeIndex = 1;
			}

			float3 positionLS = mul(transpose(lightProjectionMatrix), float4(positionWS.xyz, 1)).xyz;

			float shadowVisibility = Get2DFilteredShadowCascade(noise, rotationMatrix, sD.ShadowSampleParam.z, positionLS.xy, cascadeIndex, positionLS.z, eyeIndex);

			if (cascadeIndex < 1 && sD.StartSplitDistances.y < shadowMapDepth) {
				float3 cascade1PositionLS = mul(transpose(sD.ShadowMapProj[eyeIndex][1]), float4(positionWS.xyz, 1)).xyz;

				float cascade1ShadowVisibility = Get2DFilteredShadowCascade(noise, rotationMatrix, sD.ShadowSampleParam.z, cascade1PositionLS.xy, 1, cascade1PositionLS.z, eyeIndex);

				float cascade1BlendFactor = smoothstep(0, 1, (shadowMapDepth - sD.StartSplitDistances.y) / (sD.EndSplitDistances.x - sD.StartSplitDistances.y));
				shadowVisibility = lerp(shadowVisibility, cascade1ShadowVisibility, cascade1BlendFactor);
			}

			return lerp(1.0, shadowVisibility, fadeFactor);
		}

		return 1.0;
	}

	float GetWorldShadow(float3 positionWS, float3 offset, uint eyeIndex)
	{
		if (SharedData::InInterior || SharedData::HideSky)
			return 1.0;

		float worldShadow = 1.0;
#if defined(TERRAIN_SHADOWS)
		float terrainShadow = TerrainShadows::GetTerrainShadow(positionWS + offset, LinearSampler);
		worldShadow = terrainShadow;
		if (worldShadow == 0.0)
			return worldShadow;
#endif

#if defined(CLOUD_SHADOWS)
		if (!SharedData::InMapMenu)
			worldShadow *= CloudShadows::GetCloudShadowMult(positionWS, LinearSampler);
#endif

		return worldShadow;
	}

	float GetEffectShadow(float3 worldPosition, float3 viewDirection, float2 screenPosition, uint eyeIndex)
	{
		float worldShadow = GetWorldShadow(worldPosition, FrameBuffer::CameraPosAdjust[eyeIndex].xyz, eyeIndex);
		if (worldShadow != 0.0) {
			float shadow = Get3DFilteredShadow(worldPosition, viewDirection, screenPosition, eyeIndex);
			return min(worldShadow, shadow);
		}

		return worldShadow;
	}

	float GetLightingShadow(float noise, float3 worldPosition, uint eyeIndex)
	{
		float2 rotation;
		sincos(Math::TAU * noise, rotation.y, rotation.x);
		float2x2 rotationMatrix = float2x2(rotation.x, rotation.y, -rotation.y, rotation.x);
		return Get2DFilteredShadow(noise, rotationMatrix, worldPosition, eyeIndex);
	}

	float GetWaterShadow(float noise, float3 worldPosition, uint eyeIndex)
	{
		float worldShadow = GetWorldShadow(worldPosition, FrameBuffer::CameraPosAdjust[eyeIndex].xyz, eyeIndex);
		if (worldShadow != 0.0) {
			float2 rotation;
			sincos(Math::TAU * noise, rotation.y, rotation.x);
			float2x2 rotationMatrix = float2x2(rotation.x, rotation.y, -rotation.y, rotation.x);
			float shadow = Get2DFilteredShadow(noise, rotationMatrix, worldPosition, eyeIndex);
			return worldShadow * shadow;
		}

		return worldShadow;
	}

	float3 GetSampledPoint(float depth, float3 positionWS, float4x3 lightProjectionMatrix)
	{
		float3 temp = mul(transpose(lightProjectionMatrix), float4(positionWS.xyz, 1.0));
		float2 positionLS_xy = temp.xy;

		float3 sampledLS = float3(positionLS_xy, depth);

		float4 row0 = float4(lightProjectionMatrix[0][0], lightProjectionMatrix[1][0], lightProjectionMatrix[2][0], lightProjectionMatrix[3][0]);
		float4 row1 = float4(lightProjectionMatrix[0][1], lightProjectionMatrix[1][1], lightProjectionMatrix[2][1], lightProjectionMatrix[3][1]);
		float4 row2 = float4(lightProjectionMatrix[0][2], lightProjectionMatrix[1][2], lightProjectionMatrix[2][2], lightProjectionMatrix[3][2]);

		float3x4 augMatrix = float3x4(
			row0.xyz, sampledLS.x - row0.w,
			row1.xyz, sampledLS.y - row1.w,
			row2.xyz, sampledLS.z - row2.w);

		float pivot = augMatrix[0][0];
		augMatrix[0] /= pivot;

		float factor = augMatrix[1][0];
		augMatrix[1] -= factor * augMatrix[0];

		factor = augMatrix[2][0];
		augMatrix[2] -= factor * augMatrix[0];

		pivot = augMatrix[1][1];
		augMatrix[1] /= pivot;

		factor = augMatrix[0][1];
		augMatrix[0] -= factor * augMatrix[1];

		factor = augMatrix[2][1];
		augMatrix[2] -= factor * augMatrix[1];

		pivot = augMatrix[2][2];
		augMatrix[2] /= pivot;

		factor = augMatrix[0][2];
		augMatrix[0] -= factor * augMatrix[2];

		factor = augMatrix[1][2];
		augMatrix[1] -= factor * augMatrix[2];

		float3 resultWS = float3(augMatrix[0][3], augMatrix[1][3], augMatrix[2][3]);

		return resultWS;
	}

	float Get2DFilteredShadowCascadeAlt(float noise, float2x2 rotationMatrix, float sampleOffsetScale, float2 baseUV, float cascadeIndex, float compareValue, uint eyeIndex, float3 positionWS)
	{
		const uint sampleCount = 16;

		float layerIndexRcp = rcp(1 + cascadeIndex);

		float d = 0.0;

		for (uint sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
			float2 sampleOffset = mul(Random::PoissonSampleOffsets16[sampleIndex], rotationMatrix);

			float2 sampleUV = layerIndexRcp * sampleOffset * sampleOffsetScale + baseUV;

			const float4 depths = SharedShadowMap.GatherRed(LinearSampler, float3(saturate(sampleUV), cascadeIndex), 0);
			const float depth[4] = { depths.x, depths.y, depths.z, depths.w };
			float d2[4];

			for (uint i = 0; i < 4; ++i) {
				float3 p = GetSampledPoint(depth[i], positionWS, SharedShadowData[0].ShadowMapProj[eyeIndex][cascadeIndex]);
				d2[i] = max(length(p - positionWS), 1e-5);
			}
			// d += max(length(positionWS - sampledPoint), 1e-5) * 1e-2;
			d = (d2[0] + d2[1] + d2[2] + d2[3]) * 0.25;
		}

		return d * rcp((float)sampleCount);
	}

	float CalculateThickness(float noise, float3 positionWS, float3 N, uint eyeIndex, float SampleBias)
	{
		ShadowData sD = SharedShadowData[0];

		positionWS = positionWS - N;  // Shrink the position inwards the surface to avoid artifacts

		float2 rotation;
		sincos(Math::TAU * noise, rotation.y, rotation.x);
		float2x2 rotationMatrix = float2x2(rotation.x, rotation.y, -rotation.y, rotation.x);

		float shadowMapDepth = GetShadowDepth(positionWS, eyeIndex);

		if (sD.EndSplitDistances.z >= shadowMapDepth) {
			float fadeFactor = 1 - pow(saturate(dot(positionWS.xyz, positionWS.xyz) / sD.ShadowLightParam.z), 8);

			float4x3 lightProjectionMatrix = sD.ShadowMapProj[eyeIndex][0];
			float cascadeIndex = 0;

			if (sD.EndSplitDistances.x < shadowMapDepth) {
				lightProjectionMatrix = sD.ShadowMapProj[eyeIndex][1];
				cascadeIndex = 1;
			}

			float3 positionLS = mul(transpose(lightProjectionMatrix), float4(positionWS.xyz, 1)).xyz;

			float shadowVisibility = Get2DFilteredShadowCascadeAlt(noise, rotationMatrix, SampleBias, positionLS.xy, cascadeIndex, positionLS.z, eyeIndex, positionWS);

			if (cascadeIndex < 1 && sD.StartSplitDistances.y < shadowMapDepth) {
				float3 cascade1PositionLS = mul(transpose(sD.ShadowMapProj[eyeIndex][1]), float4(positionWS.xyz, 1)).xyz;

				float cascade1ShadowVisibility = Get2DFilteredShadowCascadeAlt(noise, rotationMatrix, SampleBias, cascade1PositionLS.xy, 1, cascade1PositionLS.z, eyeIndex, positionWS);

				float cascade1BlendFactor = smoothstep(0, 1, (shadowMapDepth - sD.StartSplitDistances.y) / (sD.EndSplitDistances.x - sD.StartSplitDistances.y));
				shadowVisibility = lerp(shadowVisibility, cascade1ShadowVisibility, cascade1BlendFactor);
			}

			return shadowVisibility;
		}
		return 1.0;
	}
}

#endif  // __SHADOW_SAMPLING_DEPENDENCY_HLSL__