#ifndef __EFFECT_SHADOWS_HLSLI__
#define __EFFECT_SHADOWS_HLSLI__

namespace EffectShadows
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

	float GetShadowDepth(float3 positionWS, uint eyeIndex)
	{
		float4 positionCSShifted = mul(FrameBuffer::CameraViewProj[eyeIndex], float4(positionWS, 1));
		return positionCSShifted.z / positionCSShifted.w;
	}

	float GetVSMShadow(float3 positionWS, float3 startPosition, float3 endPosition, float noise, uint sampleCount, uint eyeIndex)
	{
		ShadowData sD = SharedShadowData[0];

		float shadowMapDepth = GetShadowDepth(positionWS, eyeIndex);
		
		// Early out beyond cascade 2
		if (sD.EndSplitDistances.w < shadowMapDepth)
			return 1.0;

		float rcpSampleCount = 1.0 / float(sampleCount);

		// Precompute cascade data
		float cascade1Probability = (shadowMapDepth - sD.StartSplitDistances.y) / (sD.EndSplitDistances.x - sD.StartSplitDistances.y);

		float compareValuesStart[2];
		float compareValuesEnd[2];

		float3 positionsLS[2];
		float3 viewOffsetsLS[2];
		for (uint cascadeIdx = 0; cascadeIdx < 2; cascadeIdx++) {
			compareValuesStart[cascadeIdx] = mul(transpose(sD.ShadowMapProj[eyeIndex][cascadeIdx]), float4(startPosition, 1)).z - sD.AlphaTestRef[1 + cascadeIdx];
			compareValuesEnd[cascadeIdx] = mul(transpose(sD.ShadowMapProj[eyeIndex][cascadeIdx]), float4(endPosition, 1)).z - sD.AlphaTestRef[1 + cascadeIdx];

			positionsLS[cascadeIdx] = mul(transpose(sD.ShadowMapProj[eyeIndex][cascadeIdx]), float4(startPosition, 1));
			viewOffsetsLS[cascadeIdx] = mul(transpose(sD.ShadowMapProj[eyeIndex][cascadeIdx]), float4(endPosition, 1));
		}

		float shadow = 0.0;
		for (uint k = 0; k < sampleCount; k++) {
			float t = float(k) * rcpSampleCount;

			// Probabilistically select cascade (0 or 1 within the pair)
			uint cascadeIndex = (t + noise) < cascade1Probability;

			// March with consistent steps
			float3 sampledPositionLS = lerp(positionsLS[cascadeIndex], viewOffsetsLS[cascadeIndex], t);

			// Sample VSM shadow map
			float2 moments = SharedShadowMap.SampleLevel(LinearSampler, saturate(sampledPositionLS.xy), 1u - cascadeIndex);
			float depth = moments.x;      // E[x]
			float depth2 = moments.y;     // E[x²]

			float receiverDepth = lerp(compareValuesStart[cascadeIndex], compareValuesEnd[cascadeIndex], t);

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
		shadow = lerp(1.0, shadow * rcpSampleCount, fadeFactor);

		return shadow;
	}
}

#endif  // __EFFECT_SHADOWS_HLSLI__
