#ifndef EXTENDED_MATERIALS_PARALLAX_CORE_HLSLI
#define EXTENDED_MATERIALS_PARALLAX_CORE_HLSLI

#if defined(LANDSCAPE)
	float2 GetParallaxCoords(PS_INPUT input, float2 coords, float mipLevels[6], float maxTexDim, float3 viewDir, float3x3 tbn, float noise, DisplacementParams params[6],
		StochasticOffsets sharedOffset,
		out float pixelOffset,
		out float weights[6])
#else
	float2 GetParallaxCoords(float2 coords, float mipLevel, float3 viewDir, float3x3 tbn, float noise, Texture2D<float4> tex, SamplerState texSampler, uint channel, DisplacementParams params, out float pixelOffset)
#endif
	{
		pixelOffset = 0.0;
		float3 viewDirTS = normalize(mul(tbn, viewDir));
		float ndotv = saturate(viewDirTS.z);
	
#if defined(LANDSCAPE)
		//Softened view-Z with FlattenAmount; abs + floor limit silhouette stretch.
		float parallaxZ = max(abs(viewDirTS.z) * 0.7 + 0.3 + params[0].FlattenAmount, 0.0625);
		float2 parallaxDir = viewDirTS.xy / parallaxZ;
#else
		// Same soft denom as landscape; abs avoids negative-TS-Z blowups on curved meshes.
		float parallaxZ = max(abs(viewDirTS.z) * 0.7 + 0.3 + params.FlattenAmount, 0.0625);
		float2 parallaxDir = viewDirTS.xy / parallaxZ;
#endif

#if defined(LANDSCAPE)
		float viewDist = length(input.WorldPosition.xyz);
		float blendFactor = SharedData::extendedMaterialSettings.EnableHeightBlending ? 1.0 : 0.0;
		float4 w1 = input.LandBlendWeights1;
		float2 w2 = input.LandBlendWeights2.xy;
		const float marchHeightBlendFactor = 0.0;

		weights[0] = w1.x;
		weights[1] = w1.y;
		weights[2] = w1.z;
		weights[3] = w1.w;
		weights[4] = w2.x;
		weights[5] = w2.y;

#	if defined(TRUE_PBR)
		float scale = TerrainMaxWeightedHeightScaleW(w1, w2, params);
		float terrainHeightNormMul = rcp(max(scale, 1e-4));
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
		float2 resultCoords = coords;

#if defined(LANDSCAPE) && defined(TRUE_PBR)
		[branch] if (scale <= 0.001) {
			pixelOffset = 0.0;
			if (SharedData::extendedMaterialSettings.EnableHeightBlending) {
				float unusedHeight = GetTerrainHeight(noise, input, coords, mipLevels, params, blendFactor, w1, w2, sharedOffset, weights);
			}
		} else
#elif !defined(LANDSCAPE)
		[branch] if (scale <= 0.001) {
			pixelOffset = 0.0;
		} else
#endif
		{
			const uint minSteps = 4;
#if defined(LANDSCAPE)
			const uint maxStepsCap = 32;
#else
			const uint maxStepsCap = 32;
			const float baseMaxSteps = 8;
#endif

			// Squared grazing factor; near head-on stays cheap.
			float grazing = (1.0 - ndotv);
			grazing *= grazing;

#if defined(LANDSCAPE)
			float marchMip = ComputeParallaxMarchMip(mipLevels[0], viewDist);
			float marchMipLevels[6];
			float parallaxLODMip = mipLevels[0];
			[unroll] for (uint mi = 0; mi < 6; mi++)
				marchMipLevels[mi] = marchMip;
#else
			float parallaxLODMip = mipLevel;
			float marchMipLevel = ComputeParallaxMarchMip(mipLevel, 0.0);
#endif
			float distStepScale = lerp(0.25, 1.0, saturate((3.0 - parallaxLODMip) * (1.0 / 3.0)));

#if defined(LANDSCAPE)
			// Step count from UV travel in texels (and a grazing angle floor), so grazing rays do not skip height features between samples.
			float uvMarchSpan = dot(abs(parallaxDir), maxHeight + minHeight);
			float texelsPerStep = lerp(3.5, 1.75, saturate(grazing));
			uint uvSteps = (uint)(uvMarchSpan * maxTexDim * rcp(texelsPerStep) * distStepScale + 0.5);
			uint angleSteps = (uint)(lerp((float)minSteps, (float)maxStepsCap, grazing) * distStepScale + 0.5);
			uint numSteps = max(minSteps, max(uvSteps, angleSteps));
			numSteps = min(numSteps, maxStepsCap);
			numSteps = (numSteps + 2) & ~3;
#else
			float grazingStepBoost = lerp(1.0, 1.65, grazing);
			float angleStepMul = clamp(0.5 * rcp(max(ndotv, 0.0625)), 0.5, 2.5);
			uint numSteps = max(minSteps, (uint)(scale * baseMaxSteps * angleStepMul * distStepScale * grazingStepBoost));
			numSteps = min(numSteps, maxStepsCap);
			numSteps = (numSteps + 2) & ~3;
#endif

			uint contactIters = grazing > 0.2 ? 4u : 2u;
			uint secantIters = grazing > 0.25 ? 2u : 1u;

			float stepSize = rcp((float)numSteps);

			float2 offsetPerStep = parallaxDir * maxHeight * stepSize;
#if defined(LANDSCAPE)
			// Full-step ray-start dither breaks residual step bands on terrain.
			float rayDither = saturate(noise);
			float2 prevOffset = parallaxDir * minHeight + coords.xy - offsetPerStep * rayDither;
			float prevBound = 1.0 - rayDither * stepSize;
#else
			float2 prevOffset = parallaxDir * minHeight + coords.xy;
			float prevBound = 1.0;
#endif
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
				currHeight = GetTerrainHeightQuadRayMarch(noise, input, currentOffset[0].xy, currentOffset[0].zw, currentOffset[1].xy, currentOffset[1].zw, marchMipLevels, params, marchHeightBlendFactor, w1, w2, sharedOffset, weights) * terrainHeightNormMul + 0.5;
#else
				currHeight.x = tex.SampleLevel(texSampler, currentOffset[0].xy, marchMipLevel)[channel];
				currHeight.y = tex.SampleLevel(texSampler, currentOffset[0].zw, marchMipLevel)[channel];
				currHeight.z = tex.SampleLevel(texSampler, currentOffset[1].xy, marchMipLevel)[channel];
				currHeight.w = tex.SampleLevel(texSampler, currentOffset[1].zw, marchMipLevel)[channel];

				currHeight = AdjustDisplacementNormalized(currHeight, params);
#endif

				bool4 testResult = currHeight >= currentBound;
				[branch] if (any(testResult))
				{
					intersectionFound = true;
					float2 outOffset;
					[branch] if (testResult.x)
					{
						outOffset = prevOffset;
						pt1 = float2(currentBound.x, currHeight.x);
						pt2 = float2(prevBound, prevHeight);
					}
					else if (testResult.y)
					{
						outOffset = currentOffset[0].xy;
						pt1 = float2(currentBound.y, currHeight.y);
						pt2 = float2(currentBound.x, currHeight.x);
					}
					else if (testResult.z)
					{
						outOffset = currentOffset[0].zw;
						pt1 = float2(currentBound.z, currHeight.z);
						pt2 = float2(currentBound.y, currHeight.y);
					}
					else
					{
						outOffset = currentOffset[1].xy;
						pt1 = float2(currentBound.w, currHeight.w);
						pt2 = float2(currentBound.z, currHeight.z);
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
				float tNear = pt1.x;
				float hNear = pt1.y;
				float fNear = hNear - tNear;
				float tFar = pt2.x;
				float hFar = pt2.y;
				float fFar = hFar - tFar;

				// Binary search on f(t) = h(t) - t before secant.
				[loop] for (uint c = 0; c < contactIters; c++)
				{
					float tMid = 0.5 * (tNear + tFar);
					float2 midCoords = coords.xy + parallaxDir * (((1.0 - tMid) * -maxHeight) + minHeight);
					float hMid = 0.0;
#if defined(LANDSCAPE)
					hMid = GetTerrainHeight(noise, input, midCoords, marchMipLevels, params, marchHeightBlendFactor, w1, w2, sharedOffset, weights) * terrainHeightNormMul + 0.5;
#else
					hMid = tex.SampleLevel(texSampler, midCoords, marchMipLevel)[channel];
					hMid = AdjustDisplacementNormalized(hMid, params);
#endif
					float fMid = hMid - tMid;
					[branch] if (fMid >= 0.0)
					{
						tNear = tMid;
						hNear = hMid;
						fNear = fMid;
					}
					else
					{
						tFar = tMid;
						hFar = hMid;
						fFar = fMid;
					}
				}

				// Secant iterations on f(t) = h(t) - t.
				[loop] for (uint i = 0; i < secantIters; i++)
				{
					float denominator = fNear - fFar;
					float r = abs(denominator) > EPSILON_DIVISION ? saturate(fNear / denominator) : 0.5;
					float tSecant = lerp(tNear, tFar, r);
					float2 secantCoords = coords.xy + parallaxDir * (((1.0 - tSecant) * -maxHeight) + minHeight);

					float hSecant = 0.0;
#if defined(LANDSCAPE)
					hSecant = GetTerrainHeight(noise, input, secantCoords, marchMipLevels, params, marchHeightBlendFactor, w1, w2, sharedOffset, weights) * terrainHeightNormMul + 0.5;
#else
					hSecant = tex.SampleLevel(texSampler, secantCoords, marchMipLevel)[channel];
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
			float2 finalCoords = parallaxDir * offset + coords.xy;
#if defined(LANDSCAPE)
			if (SharedData::extendedMaterialSettings.EnableHeightBlending) {
				float unusedHeight = GetTerrainHeight(noise, input, finalCoords, mipLevels, params, blendFactor, w1, w2, sharedOffset, weights);
			}
#endif
			resultCoords = finalCoords;
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
