#ifndef EXTENDED_MATERIALS_PARALLAX_CORE_HLSLI
#define EXTENDED_MATERIALS_PARALLAX_CORE_HLSLI

#if defined(LANDSCAPE)
	float2 GetParallaxCoords(PS_INPUT input, float distance, float2 coords, float mipLevels[6], float maxTexDim, float3 viewDir, float3x3 tbn, float noise, DisplacementParams params[6],
		StochasticOffsets sharedOffset,
		out float pixelOffset,
		out float weights[6])
#else
	float2 GetParallaxCoords(float distance, float2 coords, float mipLevel, float3 viewDir, float3x3 tbn, float noise, Texture2D<float4> tex, SamplerState texSampler, uint channel, DisplacementParams params, out float pixelOffset)
#endif
	{
		pixelOffset = 0.0;
#if defined(LANDSCAPE)
		maxTexDim = maxTexDim;
#endif
		float3 viewDirTS = normalize(mul(tbn, viewDir));
#if defined(LANDSCAPE)
		viewDirTS.xy /= viewDirTS.z * 0.7 + 0.3 + params[0].FlattenAmount;
#else
		viewDirTS.xy /= viewDirTS.z * 0.7 + 0.3 + params.FlattenAmount;
#endif

#if defined(LANDSCAPE)
		float distSq = dot(distance, distance);
		float nearBlendToFar = smoothstep(1024.0 * 1024.0, 2048.0 * 2048.0, distSq);
		float blendFactor = SharedData::extendedMaterialSettings.EnableHeightBlending ? sqrt(saturate(1 - nearBlendToFar)) : 0;
		float4 w1 = lerp(input.LandBlendWeights1, smoothstep(0, 1, input.LandBlendWeights1), blendFactor);
		float2 w2 = lerp(input.LandBlendWeights2.xy, smoothstep(0, 1, input.LandBlendWeights2.xy), blendFactor);
#	if defined(TRUE_PBR)
		float scale = max(params[0].HeightScale * w1.x, max(params[1].HeightScale * w1.y, max(params[2].HeightScale * w1.z, max(params[3].HeightScale * w1.w, max(params[4].HeightScale * w2.x, params[5].HeightScale * w2.y)))));
		float scalercp = rcp(max(scale, 1e-4));
		float maxHeight = 0.1 * scale;
#	else
		float scale = 1;
		float maxHeight = 0.1 * scale;
#	endif
#else
		float nearBlendToFar = 0.0;
		float scale = params.HeightScale;
		float maxHeight = 0.1 * scale;
#endif
		float minHeight = maxHeight * 0.5;

		float2 resultCoords = coords;

#if defined(LANDSCAPE)
		if (nearBlendToFar < 1.0) {
#else
#	if defined(TRUE_PBR)
		if ((PBRFlags & PBR::Flags::InterlayerParallax) != 0 || nearBlendToFar < 1.0)
#	else
		if (nearBlendToFar < 1.0)
#	endif
		{
#endif
			const float maxSteps = 16;
			uint numSteps = uint((maxSteps * (1.0 - nearBlendToFar)) + 0.5);
			numSteps = clamp(numSteps, 4, max(4, uint(scale * maxSteps)));
			numSteps = (numSteps + 2) & ~3;

			float stepSize = rcp(numSteps);

			float2 offsetPerStep = viewDirTS.xy * float2(maxHeight, maxHeight) * stepSize.xx;
			float2 prevOffset = viewDirTS.xy * float2(minHeight, minHeight) + coords.xy;

			float prevBound = 1.0;
			float prevHeight = 1.0;

			float2 pt1 = 0;
			float2 pt2 = 0;

			uint numStepsTemp = numSteps;
			bool contactRefinement = false;

			[loop] while (numSteps > 0)
			{
				float4 currentOffset[2];
				currentOffset[0] = prevOffset.xyxy - float4(1, 1, 2, 2) * offsetPerStep.xyxy;
				currentOffset[1] = prevOffset.xyxy - float4(3, 3, 4, 4) * offsetPerStep.xyxy;
				float4 currentBound = prevBound.xxxx - float4(1, 2, 3, 4) * stepSize;

				float4 currHeight;
#if defined(LANDSCAPE)
#	if defined(TRUE_PBR)
				currHeight = GetTerrainHeightQuadRayMarch(noise, input, currentOffset[0].xy, currentOffset[0].zw, currentOffset[1].xy, currentOffset[1].zw, mipLevels, params, blendFactor, w1, w2, sharedOffset, weights) * scalercp + 0.5;
#	else
				currHeight = GetTerrainHeightQuadRayMarch(noise, input, currentOffset[0].xy, currentOffset[0].zw, currentOffset[1].xy, currentOffset[1].zw, mipLevels, params, blendFactor, w1, w2, sharedOffset, weights) + 0.5;
#	endif
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
					if (contactRefinement) {
						break;
					} else {
						contactRefinement = true;
						prevOffset = outOffset;
						prevBound = pt2.x;
						numSteps = numStepsTemp;
						stepSize /= (float)numSteps;
						offsetPerStep /= (float)numSteps;
						continue;
					}
				}

				prevOffset = currentOffset[1].zw;
				prevBound = currentBound.w;
				prevHeight = currHeight.w;
				numSteps -= 4;
			}

			float delta2 = pt2.x - pt2.y;
			float delta1 = pt1.x - pt1.y;
			float denominator = delta2 - delta1;

			float parallaxAmount = 0.0;
			[flatten] if (denominator == 0.0)
			{
				parallaxAmount = 0.0;
			}
			else
			{
				parallaxAmount = (pt1.x * delta2 - pt2.x * delta1) / denominator;
			}

#if defined(TRUE_PBR)
			if ((PBRFlags & PBR::Flags::InterlayerParallax) != 0)
				nearBlendToFar = 0;
			else
#endif
				nearBlendToFar *= nearBlendToFar;
			float offset = (1.0 - parallaxAmount) * -maxHeight + minHeight;
			pixelOffset = saturate(lerp(parallaxAmount, 0.5, nearBlendToFar));
			resultCoords = lerp(viewDirTS.xy * offset + coords.xy, coords, nearBlendToFar);
		} else {
#if defined(LANDSCAPE)
			weights[0] = input.LandBlendWeights1.x;
			weights[1] = input.LandBlendWeights1.y;
			weights[2] = input.LandBlendWeights1.z;
			weights[3] = input.LandBlendWeights1.w;
			weights[4] = input.LandBlendWeights2.x;
			weights[5] = input.LandBlendWeights2.y;
#endif
			pixelOffset = 0.0;
		}

		return resultCoords;
	}

#	if !defined(LANDSCAPE)
	// https://advances.realtimerendering.com/s2006/Tatarchuk-POM.pdf
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
