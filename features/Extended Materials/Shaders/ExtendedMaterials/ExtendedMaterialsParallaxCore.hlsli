#ifndef EXTENDED_MATERIALS_PARALLAX_CORE_HLSLI
#define EXTENDED_MATERIALS_PARALLAX_CORE_HLSLI

// Body included inside `namespace ExtendedMaterials` from ExtendedMaterials.hlsli.

#if defined(LANDSCAPE)
	float2 GetParallaxCoords(PS_INPUT input, float2 coords, float mipLevels[6], float3 viewDir, float3x3 tbn, float noise, DisplacementParams params[6],
		StochasticOffsets sharedOffset,
		out float pixelOffset,
#	if defined(VR_STEREO_OPT)
		out bool hasPOM,
#	endif
		out float weights[6])
#else
	float2 GetParallaxCoords(float2 coords, float mipLevel, float3 viewDir, float3x3 tbn, float noise, Texture2D<float4> tex, SamplerState texSampler, uint channel, DisplacementParams params, out float pixelOffset
#	if defined(VR_STEREO_OPT)
		,
		out bool hasPOM
#	endif
	)
#endif
	{
		pixelOffset = 0.0;
#if defined(VR_STEREO_OPT)
		hasPOM = false;
#endif
		float3 viewDirTS = normalize(mul(tbn, viewDir));
#if defined(LANDSCAPE)
		viewDirTS.xy /= viewDirTS.z * 0.7 + 0.3 + params[0].FlattenAmount;  // Fix for objects at extreme viewing angles
#else
		viewDirTS.xy /= viewDirTS.z * 0.7 + 0.3 + params.FlattenAmount;  // Fix for objects at extreme viewing angles
#endif

#if defined(LANDSCAPE)
		float blendFactor = SharedData::extendedMaterialSettings.EnableHeightBlending ? 1.0 : 0.0;
		float4 w1 = lerp(input.LandBlendWeights1, smoothstep(0, 1, input.LandBlendWeights1), blendFactor);
		float2 w2 = lerp(input.LandBlendWeights2.xy, smoothstep(0, 1, input.LandBlendWeights2.xy), blendFactor);
#	if defined(TRUE_PBR)
		float scale = max(params[0].HeightScale * w1.x, max(params[1].HeightScale * w1.y, max(params[2].HeightScale * w1.z, max(params[3].HeightScale * w1.w, max(params[4].HeightScale * w2.x, params[5].HeightScale * w2.y)))));
		float scalercp = rcp(max(scale, 1e-4));
		float terrainHeightNormMul = scalercp;
		float maxHeight = 0.1 * scale;
#	else
		float scale = 1;
		float terrainHeightNormMul = 1.0;
		float maxHeight = 0.1 * scale;
#	endif
#else
		float scale = params.HeightScale;
		float maxHeight = 0.1 * scale;
#endif
		float minHeight = maxHeight * 0.5;

#if defined(LANDSCAPE)
#	if defined(TRUE_PBR)
		if (scale <= 0.001) {
			weights[0] = input.LandBlendWeights1.x;
			weights[1] = input.LandBlendWeights1.y;
			weights[2] = input.LandBlendWeights1.z;
			weights[3] = input.LandBlendWeights1.w;
			weights[4] = input.LandBlendWeights2.x;
			weights[5] = input.LandBlendWeights2.y;
			pixelOffset = 0.0;
			return coords;
		}
#	endif
#else
		if (scale <= 0.001) {
			pixelOffset = 0.0;
			return coords;
		}
#endif

		{
			const float maxSteps = 8;
			uint numSteps = max(4, uint(scale * maxSteps));
			numSteps = (numSteps + 2) & ~3;

			float stepSize = rcp(numSteps);

			float2 offsetPerStep = viewDirTS.xy * float2(maxHeight, maxHeight) * stepSize.xx;
			float2 prevOffset = viewDirTS.xy * float2(minHeight, minHeight) + coords.xy;

			float prevBound = 1.0;
			float prevHeight = 1.0;

			float2 pt1 = 0;
			float2 pt2 = 0;
			bool intersectionFound = false;

			[loop] while (numSteps > 0)
			{
				float4 currentOffset[2];
				currentOffset[0] = prevOffset.xyxy - float4(1, 1, 2, 2) * offsetPerStep.xyxy;
				currentOffset[1] = prevOffset.xyxy - float4(3, 3, 4, 4) * offsetPerStep.xyxy;
				float4 currentBound = prevBound.xxxx - float4(1, 2, 3, 4) * stepSize;

				float4 currHeight;
#if defined(LANDSCAPE)
				currHeight = GetTerrainHeightQuadRayMarch(noise, input, currentOffset[0].xy, currentOffset[0].zw, currentOffset[1].xy, currentOffset[1].zw, mipLevels, params, blendFactor, w1, w2, sharedOffset, weights) * terrainHeightNormMul + 0.5;
#else
				currHeight.x = tex.SampleLevel(texSampler, currentOffset[0].xy, mipLevel)[channel];
				currHeight.y = tex.SampleLevel(texSampler, currentOffset[0].zw, mipLevel)[channel];
				currHeight.z = tex.SampleLevel(texSampler, currentOffset[1].xy, mipLevel)[channel];
				currHeight.w = tex.SampleLevel(texSampler, currentOffset[1].zw, mipLevel)[channel];

				currHeight = AdjustDisplacementNormalized(currHeight, params);
#endif

				bool4 testResult = currHeight >= currentBound;
				[branch] if (any(testResult))
				{
					float2 outOffset = 0;
					intersectionFound = true;
					[flatten] if (testResult.w)
					{
						outOffset = currentOffset[1].xy;
						pt1 = float2(currentBound.w, currHeight.w);
						pt2 = float2(currentBound.z, currHeight.z);
					}
					[flatten] if (testResult.z)
					{
						outOffset = currentOffset[0].zw;
						pt1 = float2(currentBound.z, currHeight.z);
						pt2 = float2(currentBound.y, currHeight.y);
					}
					[flatten] if (testResult.y)
					{
						outOffset = currentOffset[0].xy;
						pt1 = float2(currentBound.y, currHeight.y);
						pt2 = float2(currentBound.x, currHeight.x);
					}
					[flatten] if (testResult.x)
					{
						outOffset = prevOffset;
						pt1 = float2(currentBound.x, currHeight.x);
						pt2 = float2(prevBound, prevHeight);
					}
					prevOffset = outOffset;
					break;
				}

				prevOffset = currentOffset[1].zw;
				prevBound = currentBound.w;
				prevHeight = currHeight.w;
				numSteps -= 4;
			}

			float parallaxAmount = 0.0;
			[branch] if (intersectionFound)
			{
				// Refine coarse hit interval with secant iterations:
				// f(t) = sampledHeight(t) - t, t in [0,1] where t is ray depth bound.
				float tNear = pt1.x;
				float hNear = pt1.y;
				float fNear = hNear - tNear;
				float tFar = pt2.x;
				float hFar = pt2.y;
				float fFar = hFar - tFar;

				[loop] for (uint i = 0; i < 5; i++)
				{
					float denominator = fNear - fFar;
					float r = abs(denominator) > EPSILON_DIVISION ? saturate(fNear / denominator) : 0.5;
					float tSecant = lerp(tNear, tFar, r);
					float2 secantCoords = coords.xy + viewDirTS.xy * (((1.0 - tSecant) * -maxHeight) + minHeight);

					float hSecant;
#if defined(LANDSCAPE)
					hSecant = GetTerrainHeight(noise, input, secantCoords, mipLevels, params, blendFactor, w1, w2, sharedOffset, weights) * terrainHeightNormMul + 0.5;
#else
					hSecant = tex.SampleLevel(texSampler, secantCoords, mipLevel)[channel];
					hSecant = AdjustDisplacementNormalized(hSecant, params);
#endif

					float fSecant = hSecant - tSecant;
					[branch] if (fSecant >= 0.0)
					{
						tNear = tSecant;
						hNear = hSecant;
						fNear = fSecant;
					}
					else
					{
						tFar = tSecant;
						hFar = hSecant;
						fFar = fSecant;
					}
				}

				float denominator = fNear - fFar;
				float r = abs(denominator) > EPSILON_DIVISION ? saturate(fNear / denominator) : 0.5;
				parallaxAmount = lerp(tNear, tFar, r);
			}

			float offset = (1.0 - parallaxAmount) * -maxHeight + minHeight;
			pixelOffset = saturate(parallaxAmount);
#if defined(VR_STEREO_OPT)
			hasPOM = true;
#endif
			return viewDirTS.xy * offset + coords.xy;
		}
	}

#	if !defined(LANDSCAPE)
	// https://advances.realtimerendering.com/s2006/Tatarchuk-POM.pdf
	// Cheap method of creating shadows using height for a given light source
	float GetParallaxSoftShadowMultiplier(float2 coords, float mipLevel, float3 L, float sh0, Texture2D<float4> tex, SamplerState texSampler, uint channel, float quality, float noise, DisplacementParams params)
	{
		[branch] if (quality > 0.0)
		{
			uint tapCount = ParallaxShadowTapCount(quality);
			float shadowStrength = ShadowIntensity * (4.0 / tapCount);
			float2 rayDir = L.xy * 0.1 * params.HeightScale;
			float4 multipliers = rcp((float4(1, 2, 3, 4) + noise));
			float4 sh = sh0.xxxx;
			sh.x = AdjustDisplacementNormalized(tex.SampleLevel(texSampler, coords + rayDir * multipliers.x, mipLevel)[channel], params);
			if (quality > 0.25)
				sh.y = AdjustDisplacementNormalized(tex.SampleLevel(texSampler, coords + rayDir * multipliers.y, mipLevel)[channel], params);
			if (quality > 0.5)
				sh.z = AdjustDisplacementNormalized(tex.SampleLevel(texSampler, coords + rayDir * multipliers.z, mipLevel)[channel], params);
			if (quality > 0.75)
				sh.w = AdjustDisplacementNormalized(tex.SampleLevel(texSampler, coords + rayDir * multipliers.w, mipLevel)[channel], params);
			return 1.0 - saturate(dot(max(0, sh - sh0), shadowStrength));
		}
		return 1.0;
	}

#	endif

#endif  // EXTENDED_MATERIALS_PARALLAX_CORE_HLSLI
