#ifndef __VOLUMETRIC_SHADOWS_HLSLI__
#define __VOLUMETRIC_SHADOWS_HLSLI__

// Variance Shadow Maps (VSM)
// Chebyshev's inequality on filtered depth moments

namespace VolumetricShadows
{
	Texture2D<float2> SharedShadowMap : register(t18);

	static const float VSM_MIN_VARIANCE = 0.00001;
	static const float VSM_BLEEDING_REDUCTION = 0.2;

	// Chebyshev upper bound on P(X >= t)
	// moments.x = mean(z), moments.y = mean(z^2)
	float ComputeVSM(float2 moments, float depth)
	{
		float variance = max(moments.y - moments.x * moments.x, VSM_MIN_VARIANCE);
		float d = depth - moments.x;
		float pMax = variance / (variance + d * d);
		return (depth <= moments.x) ? 1.0 : pMax;
	}

	// Reduces light bleeding by remapping shadow values below a threshold to zero
	float ReduceBleeding(float shadow, float amount)
	{
		return saturate((shadow - amount) / (1.0 - amount));
	}

	// Sample a single cascade for VSM shadow
	float SampleVSMCascade3D(
		uint cascadeIndex,
		float noise,
		uint sampleCount,
		float rcpSampleCount,
		float3 startPositionLS,
		float3 endPositionLS,
		out float firstSample)
	{
		float shadow = 0.0;
		firstSample = 1.0;

		[loop] for (uint k = 0; k < sampleCount; k++)
		{
			float t = (float(k) + noise) * rcpSampleCount;
			float3 samplePosLS = lerp(endPositionLS, startPositionLS, t);

			float2 moments = SharedShadowMap.SampleLevel(LinearSampler, samplePosLS.xy, 1u - cascadeIndex);
			float lit = ComputeVSM(moments, samplePosLS.z);

			// Last to set firstSample is start position
			firstSample = lit;

			shadow += lit;
		}

		return shadow * rcpSampleCount;
	}

	float GetVSMShadow3D(float3 startPosition, float3 endPosition, float noise, uint baseSampleCount, out float surfaceShadow)
	{
		DirectionalShadowLightData directionalShadowLightData = DirectionalShadowLights[0];

		// View-space z — matches the linear cascade split distances from BSShadowDirectionalLight.
		float3 midPosition = (startPosition + endPosition) * 0.5;
		float shadowMapDepth = SharedData::GetScreenDepth(FrameBuffer::GetShadowDepth(midPosition));

		// Cascade projections are world-space; positions come in camera-relative.
		startPosition += FrameBuffer::CameraPosAdjust.xyz;
		endPosition += FrameBuffer::CameraPosAdjust.xyz;

		// Early out beyond cascade range
		if (shadowMapDepth >= directionalShadowLightData.EndSplitDistances.y) {
			surfaceShadow = 1.0;
			return 1.0;
		}

		// Reduce over distance
		float fade = saturate(shadowMapDepth / directionalShadowLightData.EndSplitDistances.y);

		uint sampleCount = max(1, ceil(float(baseSampleCount) * (1.0 - fade)));
		float rcpSampleCount = rcp(sampleCount);

		// Compute cascade blend factor
		float cascadeSelect = saturate((shadowMapDepth - directionalShadowLightData.StartSplitDistances.y) / (directionalShadowLightData.EndSplitDistances.x - directionalShadowLightData.StartSplitDistances.y));

		// Determine which cascade(s) to sample
		uint primaryCascade = uint(cascadeSelect);
		bool needsBlending = (cascadeSelect > 0.0) && (cascadeSelect < 1.0);

		// Transform ray to light space for primary cascade
		float4x4 shadowProj = directionalShadowLightData.ShadowProj[primaryCascade];
		float3 startLS = mul(shadowProj, float4(startPosition, 1)).xyz;
		float3 endLS = mul(shadowProj, float4(endPosition, 1)).xyz;
		startLS.xy = saturate(startLS.xy);
		endLS.xy = saturate(endLS.xy);

		// Sample primary cascade
		float primaryFirstSample;
		float shadow = SampleVSMCascade3D(primaryCascade, noise, sampleCount, rcpSampleCount, startLS, endLS, primaryFirstSample);
		surfaceShadow = primaryFirstSample;

		// Blend with secondary cascade if needed
		[branch] if (needsBlending)
		{
			uint secondaryCascade = 1 - primaryCascade;

			shadowProj = directionalShadowLightData.ShadowProj[secondaryCascade];
			startLS = mul(shadowProj, float4(startPosition, 1)).xyz;
			endLS = mul(shadowProj, float4(endPosition, 1)).xyz;
			startLS.xy = saturate(startLS.xy);
			endLS.xy = saturate(endLS.xy);

			float secondaryFirstSample;
			float shadowBlend = SampleVSMCascade3D(secondaryCascade, noise, sampleCount, rcpSampleCount, startLS, endLS, secondaryFirstSample);
			float blendFactor = smoothstep(0, 1, cascadeSelect);
			shadow = lerp(shadow, shadowBlend, blendFactor);
			surfaceShadow = lerp(surfaceShadow, secondaryFirstSample, blendFactor);
		}

		// Apply distance fade
		float fadeFactor = 1.0 - pow(fade * fade, 8);
		surfaceShadow = lerp(1.0, surfaceShadow, fadeFactor);
		return lerp(1.0, shadow, fadeFactor);
	}

	// Sample a single cascade for VSM shadow (2D point sample)
	float SampleVSMCascade2D(uint cascadeIndex, float3 positionLS)
	{
		float2 moments = SharedShadowMap.SampleLevel(LinearSampler, positionLS.xy, 1u - cascadeIndex);
		return ComputeVSM(moments, positionLS.z);
	}

	float GetVSMShadow2D(float3 position, out float detailedShadow)
	{
		DirectionalShadowLightData directionalShadowLightData = DirectionalShadowLights[0];

		float shadowMapDepth = SharedData::GetScreenDepth(FrameBuffer::GetShadowDepth(position));

		// Early out beyond cascade range
		if (shadowMapDepth >= directionalShadowLightData.EndSplitDistances.y) {
			detailedShadow = 1.0;
			return 1.0;
		}

		// Reduce over distance
		float fade = saturate(shadowMapDepth / directionalShadowLightData.EndSplitDistances.y);

		// Cascade projections are world-space; position comes in camera-relative.
		float3 positionWS = position + FrameBuffer::CameraPosAdjust.xyz;

		// Compute cascade blend factor
		float cascadeSelect = saturate((shadowMapDepth - directionalShadowLightData.StartSplitDistances.y) / (directionalShadowLightData.EndSplitDistances.x - directionalShadowLightData.StartSplitDistances.y));

		// Determine which cascade(s) to sample
		uint primaryCascade = uint(cascadeSelect);
		bool needsBlending = (cascadeSelect > 0.0) && (cascadeSelect < 1.0);

		// Transform position to light space for primary cascade
		float3 positionLS = mul(directionalShadowLightData.ShadowProj[primaryCascade], float4(positionWS, 1)).xyz;
		positionLS.xy = saturate(positionLS.xy);

		// Sample primary cascade
		float shadow = SampleVSMCascade2D(primaryCascade, positionLS);

		// Blend with secondary cascade if needed
		[branch] if (needsBlending)
		{
			uint secondaryCascade = 1 - primaryCascade;

			positionLS = mul(directionalShadowLightData.ShadowProj[secondaryCascade], float4(positionWS, 1)).xyz;
			positionLS.xy = saturate(positionLS.xy);

			float shadowBlend = SampleVSMCascade2D(secondaryCascade, positionLS);
			shadow = lerp(shadow, shadowBlend, smoothstep(0, 1, cascadeSelect));
		}

		// Apply distance fade
		float fadeFactor = 1.0 - pow(fade * fade, 8);
		detailedShadow = lerp(1.0, ReduceBleeding(shadow, VSM_BLEEDING_REDUCTION), fadeFactor);
		return lerp(1.0, shadow, fadeFactor);
	}
}

#endif  // __VOLUMETRIC_SHADOWS_HLSLI__
