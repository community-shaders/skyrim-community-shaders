#ifndef __EFFECT_SHADOWS_HLSLI__
#define __EFFECT_SHADOWS_HLSLI__

// "Assassin's Creed 4: Black Flag - Road to next-gen graphics" https://bartwronski.com/wp-content/uploads/2014/03/ac4_gdc.pdf

namespace EffectShadows
{
	Texture2D<float2> SharedShadowMap : register(t18);

	struct ShadowData
	{
		float4 VPOSOffset;
		float4 ShadowSampleParam;    // fPoissonRadiusScale / iShadowMapResolution in z and w
		float4 EndSplitDistances;    // cascade end distances int xyz, cascade count int z
		float4 StartSplitDistances;  // cascade start distances int xyz, 4 int z
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
		float4 positionCS = mul(FrameBuffer::CameraViewProj[eyeIndex], float4(positionWS, 1));
		return positionCS.z / positionCS.w;
	}

	// Sample a single cascade for VSM shadow
	float SampleVSMCascade3D(
		uint cascadeIndex,
		uint sampleCount,
		float rcpSampleCount,
		float3 startPositionLS,
		float3 endPositionLS,
		out float firstSample)
	{
		float shadow = 0.0;
		firstSample = 1.0;

		[loop]
		for (uint k = 0; k < sampleCount; k++) {
			float t = float(k) * rcpSampleCount;
			float3 samplePosLS = lerp(endPositionLS, startPositionLS, t);

			// Sample VSM moments
			float2 moments = SharedShadowMap.SampleLevel(LinearSampler, samplePosLS.xy, 1u - cascadeIndex);

			// VSM shadow test using Chebyshev's inequality
			float lit = 1.0;
			if (samplePosLS.z > moments.x) {
				float variance = max(moments.y - moments.x * moments.x, 1e-5);
				float d = samplePosLS.z - moments.x;
				lit = variance / (variance + d * d);
			}

			// Last to set firstSample is start position
			firstSample = lit;

			shadow += lit;
		}

		return shadow * rcpSampleCount;
	}

	float GetVSMShadow3D(float3 startPosition, float3 endPosition, uint baseSampleCount, uint eyeIndex, out float surfaceShadow)
	{
		ShadowData sD = SharedShadowData[0];

		float3 midPosition = (startPosition + endPosition) * 0.5;
		float shadowMapDepth = GetShadowDepth(midPosition, eyeIndex);

		// Early out beyond cascade range
		if (shadowMapDepth >= sD.EndSplitDistances.w) {
			surfaceShadow = 1.0;
			return 1.0;
		}

		// Reduce over distance
		float distSq = dot(midPosition, midPosition);
		float fade = saturate(distSq / sD.ShadowLightParam.z);

		uint sampleCount = max(1, round(float(baseSampleCount) * (1.0 - fade)));
		float rcpSampleCount = rcp(sampleCount);

		// Compute cascade blend factor with smoothstep
		float cascadeSelect = smoothstep(0.0, 1.0,
			(shadowMapDepth - sD.StartSplitDistances.y) /
			(sD.EndSplitDistances.x - sD.StartSplitDistances.y));

		// Determine which cascade(s) to sample
		uint primaryCascade = uint(cascadeSelect);
		bool needsBlending = (cascadeSelect > 0.0) && (cascadeSelect < 1.0);

		// Transform ray to light space for primary cascade
		float4x3 shadowProj = sD.ShadowMapProj[eyeIndex][primaryCascade];
		float3 startLS = mul(transpose(shadowProj), float4(startPosition, 1));
		float3 endLS = mul(transpose(shadowProj), float4(endPosition, 1));
		startLS.xy = saturate(startLS.xy);
		endLS.xy = saturate(endLS.xy);

		// Sample primary cascade
		float primaryFirstSample;
		float shadow = SampleVSMCascade3D(primaryCascade, sampleCount, rcpSampleCount, startLS, endLS, primaryFirstSample);
		surfaceShadow = primaryFirstSample;

		// Blend with secondary cascade if needed
		[branch]
		if (needsBlending) {
			uint secondaryCascade = 1 - primaryCascade;

			shadowProj = sD.ShadowMapProj[eyeIndex][secondaryCascade];
			startLS = mul(transpose(shadowProj), float4(startPosition, 1));
			endLS = mul(transpose(shadowProj), float4(endPosition, 1));
			startLS.xy = saturate(startLS.xy);
			endLS.xy = saturate(endLS.xy);

			float secondaryFirstSample;
			float shadowBlend = SampleVSMCascade3D(secondaryCascade, sampleCount, rcpSampleCount, startLS, endLS, secondaryFirstSample);
			shadow = lerp(shadow, shadowBlend, cascadeSelect);
			surfaceShadow = lerp(surfaceShadow, secondaryFirstSample, cascadeSelect);
		}

		// Apply distance fade
		float fadeFactor = 1.0 - pow(fade, 8);
		surfaceShadow = lerp(1.0, surfaceShadow, fadeFactor);
		return lerp(1.0, shadow, fadeFactor);
	}

	// Sample a single cascade for VSM shadow
	float SampleVSMCascade2D(uint cascadeIndex, float3 positionLS)
	{
		// Sample VSM moments
		float2 moments = SharedShadowMap.SampleLevel(LinearSampler, positionLS.xy, 1u - cascadeIndex);

		// VSM shadow test using Chebyshev's inequality
		float lit = 1.0;
		if (positionLS.z > moments.x) {
			float variance = max(moments.y - moments.x * moments.x, 1e-5);
			float d = positionLS.z - moments.x;
			lit = variance / (variance + d * d);
		}


		return lit;
	}

	float GetVSMShadow2D(float3 position, uint eyeIndex)
	{
		ShadowData sD = SharedShadowData[0];

		float shadowMapDepth = GetShadowDepth(position, eyeIndex);

		// Early out beyond cascade range
		if (shadowMapDepth >= sD.EndSplitDistances.w) {
			return 1.0;
		}

		// Reduce over distance
		float distSq = dot(position, position);
		float fade = saturate(distSq / sD.ShadowLightParam.z);

		// Compute cascade blend factor with smoothstep
		float cascadeSelect = smoothstep(0.0, 1.0,
			(shadowMapDepth - sD.StartSplitDistances.y) /
			(sD.EndSplitDistances.x - sD.StartSplitDistances.y));

		// Determine which cascade(s) to sample
		uint primaryCascade = uint(cascadeSelect);
		bool needsBlending = (cascadeSelect > 0.0) && (cascadeSelect < 1.0);

		// Transform ray to light space for primary cascade
		float4x3 shadowProj = sD.ShadowMapProj[eyeIndex][primaryCascade];
		float3 positionLS = mul(transpose(shadowProj), float4(position, 1));
		positionLS.xy = saturate(positionLS.xy);

		// Sample primary cascade
		float shadow = SampleVSMCascade2D(primaryCascade, positionLS);

		// Blend with secondary cascade if needed
		[branch]
		if (needsBlending) {
			uint secondaryCascade = 1 - primaryCascade;

			shadowProj = sD.ShadowMapProj[eyeIndex][secondaryCascade];
			positionLS = mul(transpose(shadowProj), float4(position, 1));
			positionLS.xy = saturate(positionLS.xy);

			float shadowBlend = SampleVSMCascade2D(secondaryCascade, positionLS);
			shadow = lerp(shadow, shadowBlend, cascadeSelect);
		}

		// Apply distance fade
		float fadeFactor = 1.0 - pow(fade, 8);
		return lerp(1.0, shadow, fadeFactor);
	}
}

#endif  // __EFFECT_SHADOWS_HLSLI__