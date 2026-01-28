#ifndef __SHADOW_SAMPLING_DEPENDENCY_HLSL__
#define __SHADOW_SAMPLING_DEPENDENCY_HLSL__

#include "Common/Math.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/Color.hlsli"

#	if defined(TERRAIN_SHADOWS)
#		include "TerrainShadows/TerrainShadows.hlsli"
#	endif

#	if defined(CLOUD_SHADOWS)
#		include "CloudShadows/CloudShadows.hlsli"
#	endif

#if defined(IBL)
#	include "IBL/IBL.hlsli"
#endif

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

		static const uint sampleCount = 8;
		static const float rcpSampleCount = 1.0 / float(sampleCount);

		float noise = Random::InterleavedGradientNoise(screenPosition, SharedData::FrameCount);
		float noiseTransform = noise * 2.0 - 1.0;
		float2 rotation;
		sincos(Math::TAU * noise, rotation.y, rotation.x);
		float2x2 rotationMatrix = float2x2(rotation.x, rotation.y, -rotation.y, rotation.x);

		float shadowMapDepth = GetShadowDepth(positionWS, eyeIndex);

		float shadow = 0.0;
		if (sD.EndSplitDistances.z >= shadowMapDepth) {
			float cascade1Probability = saturate((shadowMapDepth - sD.StartSplitDistances.y) / (sD.EndSplitDistances.x - sD.StartSplitDistances.y));

			// Precompute cascade data for both cascades
			float compareValues[2];
			float sampleRadii[2];
			float3 positionsLS[2];
			float3 viewOffsetsLS[2];

			[unroll]
			for (uint cascadeIdx = 0; cascadeIdx < 2; cascadeIdx++) {
				compareValues[cascadeIdx] = mul(transpose(sD.ShadowMapProj[eyeIndex][cascadeIdx]), float4(positionWS, 1)).z - sD.AlphaTestRef[1 + cascadeIdx];
				sampleRadii[cascadeIdx] = sD.ShadowSampleParam.z * rcp(1 + cascadeIdx) * 2.0;

#if defined(EFFECT)
				// Enough for non-billboards + enough for Sovngarde fog
				float viewRayLength = 16.0 + Permutation::BillboardRadius * 0.1;
				positionsLS[cascadeIdx] = mul(transpose(sD.ShadowMapProj[eyeIndex][cascadeIdx]), float4(positionWS - viewDirection * viewRayLength, 1));
				viewOffsetsLS[cascadeIdx] = mul(transpose(sD.ShadowMapProj[eyeIndex][cascadeIdx]), float4(positionWS + viewDirection * viewRayLength, 1));
#else
				// Enough for Eastmarch water
				float viewRayLength = 128.0;
				positionsLS[cascadeIdx] = mul(transpose(sD.ShadowMapProj[eyeIndex][cascadeIdx]), float4(positionWS, 1));
				viewOffsetsLS[cascadeIdx] = mul(transpose(sD.ShadowMapProj[eyeIndex][cascadeIdx]), float4(positionWS + viewDirection * viewRayLength, 1));
#endif
			}

			for (uint i = 0; i < sampleCount; i++) {
				uint noisyIndex = uint((float(i) + sampleCount * noise) % sampleCount);
				float t = (float(sampleCount) - float(noisyIndex + 1)) * rcpSampleCount;
				uint cascadeIndex = frac(t + noise) < cascade1Probability;

				float compareValue = compareValues[cascadeIndex];
				float sampleRadius = sampleRadii[cascadeIndex];
				float3 positionLS = positionsLS[cascadeIndex];
				float3 viewOffsetLS = viewOffsetsLS[cascadeIndex];

				// Offset along view ray with optimised sample pattern
				float3 sampledPositionLS = lerp(positionLS, viewOffsetLS, t + noiseTransform * rcpSampleCount);

				// Blur shadow with poisson disc
				sampledPositionLS.xy += mul(Random::SpiralSampleOffsets8[i], rotationMatrix) * sampleRadius;

				// Average 4 shadow samples for improved quality
				float4 depths = SharedShadowMap.GatherRed(LinearSampler, float3(saturate(sampledPositionLS.xy), cascadeIndex), 0);
				shadow += dot(depths > compareValue, 0.25);
			}
		} else {
			shadow = 1.0;
		}

		float fadeFactor = 1.0 - pow(saturate(dot(positionWS, positionWS) / sD.ShadowLightParam.z), 8);
		return lerp(1.0, shadow * rcpSampleCount, fadeFactor);
	}

	float Get2DFilteredShadowCascade(float noise, float2x2 rotationMatrix, float sampleOffsetScale, float2 baseUV, float cascadeIndex, float compareValue, uint eyeIndex)
	{
		const uint sampleCount = 16;

		float layerIndexRcp = rcp(1 + cascadeIndex);

		float visibility = 0.0;

		sampleOffsetScale *= 4.0;

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
		worldShadow = TerrainShadows::GetTerrainShadow(positionWS + offset, LinearSampler);
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
		[branch] if (worldShadow > 0.0) {
			float shadow = Get3DFilteredShadow(worldPosition, viewDirection, screenPosition, eyeIndex);
			return worldShadow * shadow;
		}
		return 0.0;
	}

	float GetLightingShadow(float noise, float3 worldPosition, uint eyeIndex)
	{
		float2 rotation;
		sincos(Math::TAU * noise, rotation.y, rotation.x);
		float2x2 rotationMatrix = float2x2(rotation.x, rotation.y, -rotation.y, rotation.x);
		return Get2DFilteredShadow(noise, rotationMatrix, worldPosition, eyeIndex);
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

		{
			float maxScale = 1.0;
			if (ambientColorAmb.x > 0.0)
				maxScale = min(maxScale, inputColor.x / ambientColorAmb.x);
			if (ambientColorAmb.y > 0.0)
				maxScale = min(maxScale, inputColor.y / ambientColorAmb.y);
			if (ambientColorAmb.z > 0.0)
				maxScale = min(maxScale, inputColor.z / ambientColorAmb.z);
			ambientColorAmb *= maxScale;
		}

		{
			float maxScale = 1.0;
			if (dirLightColorDir.x > 0.0)
				maxScale = min(maxScale, inputColor.x / dirLightColorDir.x);
			if (dirLightColorDir.y > 0.0)
				maxScale = min(maxScale, inputColor.y / dirLightColorDir.y);
			if (dirLightColorDir.z > 0.0)
				maxScale = min(maxScale, inputColor.z / dirLightColorDir.z);
			dirLightColorDir *= maxScale;
		}

		float3 dirLightColorAmb = max(0.0, inputColor - ambientColorAmb);
		float3 ambientColorDir = max(0.0, inputColor - dirLightColorDir);

		dirColor = lerp(dirLightColorAmb, dirLightColorDir, 0.5);
		ambientColor = lerp(ambientColorAmb, ambientColorDir, 0.5);
	}
}

#endif  // __SHADOW_SAMPLING_DEPENDENCY_HLSL__