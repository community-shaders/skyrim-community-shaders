#ifndef __VOLUMETRIC_SHADOWS_HLSLI__
#define __VOLUMETRIC_SHADOWS_HLSLI__

// Moment Shadow Maps (Peters & Klein, I3D 2015)
// Hamburger 4-moment reconstruction for reduced light bleeding

namespace VolumetricShadows
{
	Texture2D<float4> SharedShadowMap : register(t18);

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

//=================================================================================================
//
//  Shadows Sample
//  by MJP
//  http://mynameismjp.wordpress.com/
//
//  All code licensed under the MIT license
//
//=================================================================================================
	float4 ConvertOptimizedMoments(in float4 optimizedMoments)
	{
		optimizedMoments[0] -= 0.035955884801f;
		return mul(optimizedMoments, float4x4(0.2227744146f, 0.1549679261f, 0.1451988946f, 0.163127443f,
											0.0771972861f, 0.1394629426f, 0.2120202157f, 0.2591432266f,
											0.7926986636f, 0.7963415838f, 0.7258694464f, 0.6539092497f,
											0.0319417555f,-0.1722823173f,-0.2758014811f,-0.3376131734f));
	}

	float ComputeMSMHamburger(in float4 moments, in float fragmentDepth, in float momentBias)
	{
		// Bias input data to avoid artifacts
		float4 b = lerp(moments, float4(0.5f, 0.5f, 0.5f, 0.5f), momentBias);
		float3 z;
		z[0] = fragmentDepth;

		// Compute a Cholesky factorization of the Hankel matrix B storing only non-
		// trivial entries or related products
		float L32D22 = mad(-b[0], b[1], b[2]);
		float D22 = mad(-b[0], b[0], b[1]);
		float squaredDepthVariance = mad(-b[1], b[1], b[3]);
		float D33D22 = dot(float2(squaredDepthVariance, -L32D22), float2(D22, L32D22));
		float InvD22 = 1.0f / D22;
		float L32 = L32D22 * InvD22;

		// Obtain a scaled inverse image of bz = (1,z[0],z[0]*z[0])^T
		float3 c = float3(1.0f, z[0], z[0] * z[0]);

		// Forward substitution to solve L*c1=bz
		c[1] -= b.x;
		c[2] -= b.y + L32 * c[1];

		// Scaling to solve D*c2=c1
		c[1] *= InvD22;
		c[2] *= D22 / D33D22;

		// Backward substitution to solve L^T*c3=c2
		c[1] -= L32 * c[2];
		c[0] -= dot(c.yz, b.xy);

		// Solve the quadratic equation c[0]+c[1]*z+c[2]*z^2 to obtain solutions
		// z[1] and z[2]
		float p = c[1] / c[2];
		float q = c[0] / c[2];
		float D = (p * p * 0.25f) - q;
		float r = sqrt(D);
		z[1] =- p * 0.5f - r;
		z[2] =- p * 0.5f + r;

		// Compute the shadow intensity by summing the appropriate weights
		float4 switchVal = (z[2] < z[0]) ? float4(z[1], z[0], 1.0f, 1.0f) :
						((z[1] < z[0]) ? float4(z[0], z[1], 0.0f, 1.0f) :
						float4(0.0f,0.0f,0.0f,0.0f));
		float quotient = (switchVal[0] * z[2] - b[0] * (switchVal[0] + z[2]) + b[1])/((z[2] - switchVal[1]) * (z[0] - z[1]));
		float shadowIntensity = switchVal[2] + switchVal[3] * quotient;
		return 1.0f - saturate(shadowIntensity);
	}

	float ComputeMSMHausdorff(in float4 moments, in float fragmentDepth, in float momentBias)
	{
		// Bias input data to avoid artifacts
		float4 b = lerp(moments, float4(0.5f, 0.5f, 0.5f, 0.5f), momentBias);
		float3 z;
		z[0] = fragmentDepth;

		// Compute a Cholesky factorization of the Hankel matrix B storing only non-
		// trivial entries or related products
		float L32D22 = mad(-b[0], b[1], b[2]);
		float D22 = mad(-b[0], b[0], b[1]);
		float squaredDepthVariance = mad(-b[1], b[1], b[3]);
		float D33D22 = dot(float2(squaredDepthVariance, -L32D22), float2(D22, L32D22));
		float InvD22 = 1.0f / D22;
		float L32 = L32D22 * InvD22;

		// Obtain a scaled inverse image of bz=(1,z[0],z[0]*z[0])^T
		float3 c = float3(1.0f, z[0], z[0] * z[0]);

		// Forward substitution to solve L*c1=bz
		c[1] -= b.x;
		c[2] -= b.y + L32 * c[1];

		// Scaling to solve D*c2=c1
		c[1] *= InvD22;
		c[2] *= D22 / D33D22;

		// Backward substitution to solve L^T*c3=c2
		c[1] -= L32 * c[2];
		c[0] -= dot(c.yz, b.xy);

		// Solve the quadratic equation c[0]+c[1]*z+c[2]*z^2 to obtain solutions z[1]
		// and z[2]
		float p = c[1] / c[2];
		float q = c[0] / c[2];
		float D = ((p * p) / 4.0f) - q;
		float r = sqrt(D);
		z[1] =- (p / 2.0f) - r;
		z[2] =- (p / 2.0f) + r;

		float shadowIntensity = 1.0f;

		// Use a solution made of four deltas if the solution with three deltas is invalid
		if(z[1] < 0.0f || z[2] > 1.0f)
		{
			float zFree = ((b[2] - b[1]) * z[0] + b[2] - b[3]) / ((b[1] - b[0]) * z[0] + b[1] - b[2]);
			float w1Factor = (z[0] > zFree) ? 1.0f : 0.0f;
			shadowIntensity = (b[1] - b[0] + (b[2] - b[0] - (zFree + 1.0f) * (b[1] - b[0])) * (zFree - w1Factor - z[0])
													/(z[0] * (z[0] - zFree))) / (zFree - w1Factor) + 1.0f - b[0];
		}
		// Use the solution with three deltas
		else{
			float4 switchVal = (z[2] < z[0]) ? float4(z[1], z[0], 1.0f, 1.0f) :
							((z[1] < z[0]) ? float4(z[0], z[1], 0.0f, 1.0f) :
							float4(0.0f, 0.0f, 0.0f, 0.0f));
			float quotient = (switchVal[0] * z[2] - b[0] * (switchVal[0] + z[2]) + b[1]) / ((z[2] - switchVal[1]) * (z[0] - z[1]));
			shadowIntensity = switchVal[2] + switchVal[3] * quotient;
		}

		return 1.0f - saturate(shadowIntensity);
	}

	// Sample a single cascade for MSM shadow
	float SampleMSMCascade3D(
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

		[loop]
		for (uint k = 0; k < sampleCount; k++) {
			float t = (float(k) + noise) * rcpSampleCount;
			float3 samplePosLS = lerp(endPositionLS, startPositionLS, t);

			// Sample MSM moments
			float4 moments = SharedShadowMap.SampleLevel(LinearSampler, samplePosLS.xy, 1u - cascadeIndex);
			moments = ConvertOptimizedMoments(moments);
			float lit = ComputeMSMHausdorff(moments, samplePosLS.z, 0.00003);

			lit = lerp(lit, lit * lit, float(cascadeIndex));

			// Last to set firstSample is start position
			firstSample = lit;

			shadow += lit;
		}

		return shadow * rcpSampleCount;
	}

	float GetMSMShadow3D(float3 startPosition, float3 endPosition, float noise, uint baseSampleCount, uint eyeIndex, out float surfaceShadow)
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

		uint sampleCount = max(1, ceil(float(baseSampleCount) * (1.0 - fade)));
		float rcpSampleCount = rcp(sampleCount);

		// Compute cascade blend factor with smoothstep
		float cascadeSelect = saturate((shadowMapDepth - sD.StartSplitDistances.y) / (sD.EndSplitDistances.x - sD.StartSplitDistances.y));

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
		float shadow = SampleMSMCascade3D(primaryCascade, noise, sampleCount, rcpSampleCount, startLS, endLS, primaryFirstSample);
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
			float shadowBlend = SampleMSMCascade3D(secondaryCascade, noise, sampleCount, rcpSampleCount, startLS, endLS, secondaryFirstSample);
			shadow = lerp(shadow, shadowBlend, cascadeSelect);
			surfaceShadow = lerp(surfaceShadow, secondaryFirstSample, cascadeSelect);
		}

		// Apply distance fade
		float fadeFactor = 1.0 - pow(fade, 8);
		surfaceShadow = lerp(1.0, surfaceShadow, fadeFactor);
		return lerp(1.0, shadow, fadeFactor);
	}

	// Sample a single cascade for MSM shadow (2D point sample)
	float SampleMSMCascade2D(uint cascadeIndex, float3 positionLS)
	{
		float4 moments = SharedShadowMap.SampleLevel(LinearSampler, positionLS.xy, 1u - cascadeIndex);
		moments = ConvertOptimizedMoments(moments);
		return ComputeMSMHausdorff(moments, positionLS.z, 0.00003);
	}

	float GetMSMShadow2D(float3 position, uint eyeIndex)
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
		float cascadeSelect = saturate((shadowMapDepth - sD.StartSplitDistances.y) / (sD.EndSplitDistances.x - sD.StartSplitDistances.y));

		// Determine which cascade(s) to sample
		uint primaryCascade = uint(cascadeSelect);
		bool needsBlending = (cascadeSelect > 0.0) && (cascadeSelect < 1.0);

		// Transform ray to light space for primary cascade
		float4x3 shadowProj = sD.ShadowMapProj[eyeIndex][primaryCascade];
		float3 positionLS = mul(transpose(shadowProj), float4(position, 1));
		positionLS.xy = saturate(positionLS.xy);

		// Sample primary cascade
		float shadow = SampleMSMCascade2D(primaryCascade, positionLS);

		// Blend with secondary cascade if needed
		[branch]
		if (needsBlending) {
			uint secondaryCascade = 1 - primaryCascade;

			shadowProj = sD.ShadowMapProj[eyeIndex][secondaryCascade];
			positionLS = mul(transpose(shadowProj), float4(position, 1));
			positionLS.xy = saturate(positionLS.xy);

			float shadowBlend = SampleMSMCascade2D(secondaryCascade, positionLS);
			shadow = lerp(shadow, shadowBlend, cascadeSelect);
		}

		// Apply distance fade
		float fadeFactor = 1.0 - pow(fade, 8);
		return lerp(1.0, shadow, fadeFactor);
	}
}

#endif  // __VOLUMETRIC_SHADOWS_HLSLI__
