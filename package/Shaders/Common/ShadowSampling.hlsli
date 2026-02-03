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

namespace ShadowSampling
{
	Texture2D<float2> SharedShadowMap : register(t18);

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

	float GetShadowDepth(float3 positionWS, uint eyeIndex)
	{
		float4 positionCSShifted = mul(FrameBuffer::CameraViewProj[eyeIndex], float4(positionWS, 1));
		return positionCSShifted.z / positionCSShifted.w;
	}

	float Get3DFilteredShadow(float3 positionWS, float3 viewDirection, float2 screenPosition, uint eyeIndex, float depth)
	{
		const float stepSize = 32.0;  // Fixed step size in world units
		
		float noise = Random::InterleavedGradientNoise(screenPosition, SharedData::FrameCount);
		float2 rotation;
		sincos(Math::TAU * noise, rotation.y, rotation.x);
		float2x2 rotationMatrix = float2x2(rotation.x, rotation.y, -rotation.y, rotation.x);

		float screenDepth = SharedData::GetScreenDepth(depth);
		float objectDepth = length(positionWS);
		float maxDistance = max(0, screenDepth - objectDepth);

	#if defined(EFFECT)
		float viewRayLength = min(Permutation::EffectRadius * 0.1, 128);
		float3 startPosition = positionWS - viewDirection * viewRayLength;
		float3 endPosition = positionWS + viewDirection * min(viewRayLength, maxDistance);
	#elif defined(UNDERWATER)
		float viewRayLength = 128.0;
		float3 startPosition = positionWS - viewDirection * viewRayLength;
		float3 endPosition = positionWS;
	#else
		float viewRayLength = 128.0;
		float3 startPosition = positionWS;
		float3 endPosition = positionWS + viewDirection * min(viewRayLength, maxDistance);
	#endif

		float totalRayLength = distance(endPosition, startPosition);
		uint sampleCount = uint(totalRayLength / stepSize + 0.5);
		float rcpSampleCount = 1.0 / float(sampleCount);

		startPosition += (endPosition - startPosition) * noise * rcpSampleCount;

		// Sample world shadows with consistent step size
		float worldShadow = 0;
		for(uint i = 0; i < sampleCount; i++){
			float t = float(i) * rcpSampleCount;
			float3 sampledPositionWS = lerp(startPosition, endPosition, t);
			worldShadow += ShadowSampling::GetWorldShadow(sampledPositionWS, FrameBuffer::CameraPosAdjust[eyeIndex].xyz, eyeIndex);
		}

		if (worldShadow == 0.0)
			return 0.0;

		worldShadow *= rcpSampleCount;

		float shadowMapDepth = GetShadowDepth(positionWS, eyeIndex);

		ShadowData sD = SharedShadowData[0];

		// Early out beyond cascade 2
		if (sD.EndSplitDistances.w < shadowMapDepth)
			return worldShadow;

		// Precompute cascade data
		float cascade1Probability = saturate((shadowMapDepth - sD.StartSplitDistances.y) / (sD.EndSplitDistances.x - sD.StartSplitDistances.y));
		
		float compareValues[2];
		float3 positionsLS[2];
		float3 viewOffsetsLS[2];
		for (uint cascadeIdx = 0; cascadeIdx < 2; cascadeIdx++) {
			compareValues[cascadeIdx] = mul(transpose(sD.ShadowMapProj[eyeIndex][cascadeIdx]), float4(positionWS, 1)).z - sD.AlphaTestRef[1 + cascadeIdx];
			positionsLS[cascadeIdx] = mul(transpose(sD.ShadowMapProj[eyeIndex][cascadeIdx]), float4(startPosition, 1));
			viewOffsetsLS[cascadeIdx] = mul(transpose(sD.ShadowMapProj[eyeIndex][cascadeIdx]), float4(endPosition, 1));
		}

		float shadow = 0.0;
		for (uint k = 0; k < sampleCount; k++) {
			float t = float(k) * rcpSampleCount;

			// Probabilistically select cascade (0 or 1 within the pair)
			uint cascadeIndex = uint(frac(t + noise) < cascade1Probability);

			// March with consistent steps
			float3 sampledPositionLS = lerp(positionsLS[cascadeIndex], viewOffsetsLS[cascadeIndex], t);

			// Sample VSM shadow map
			float2 moments = SharedShadowMap.SampleLevel(LinearSampler, saturate(sampledPositionLS.xy), 1u - cascadeIndex);
			float depth = moments.x;      // E[x]
			float depth2 = moments.y;     // E[x²]

			float receiverDepth = compareValues[cascadeIndex];

			// VSM using Chebyshev's inequality
			float lit = 1.0;
			if (receiverDepth > depth) {
				float variance = max(depth2 - (depth * depth), 0.00001); // σ² = E[x²] - E[x]²
				float d = receiverDepth - depth;
				lit = variance / (variance + d * d); // p_max(t) = σ²/(σ² + (t - μ)²)
			}

			shadow += lit;
		}

		float fadeFactor = 1.0 - pow(saturate(dot(positionWS, positionWS) / sD.ShadowLightParam.z), 8);
		return worldShadow * lerp(1.0, shadow * rcpSampleCount, fadeFactor);
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

			//float4 depths = SharedShadowMap.SampleLevel(LinearSampler, saturate(sampleUV), 1 - cascadeIndex);
			visibility += dot(0 > compareValue, 0.25);
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

		dirColor = lerp(dirLightColorAmb, dirLightColorDir, 0.0);
		ambientColor = lerp(ambientColorAmb, ambientColorDir, 0.0);
	}
}

#endif  // __SHADOW_SAMPLING_DEPENDENCY_HLSL__