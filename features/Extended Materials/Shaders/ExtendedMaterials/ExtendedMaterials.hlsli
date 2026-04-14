// https://github.com/tgjones/slimshader-cpp/blob/master/src/Shaders/Sdk/Direct3D11/DetailTessellation11/POM.hlsl
// https://github.com/alandtse/SSEShaderTools/blob/main/shaders_vr/ParallaxEffect.h

// https://github.com/marselas/Zombie-Direct3D-Samples/blob/5f53dc2d6f7deb32eb2e5e438d6b6644430fe9ee/Direct3D/ParallaxOcclusionMapping/ParallaxOcclusionMapping.fx
// http://www.diva-portal.org/smash/get/diva2:831762/FULLTEXT01.pdf
// https://bartwronski.files.wordpress.com/2014/03/ac4_gdc.pdf

struct DisplacementParams
{
	float DisplacementScale;
	float DisplacementOffset;
	float HeightScale;
	float FlattenAmount;
};

#if defined(LANDSCAPE)
#	include "Common/LandscapeLayers.hlsli"
#endif

namespace ExtendedMaterials
{
	static const float ShadowIntensity = 2.0;

	float ScaleDisplacement(float displacement, DisplacementParams params)
	{
		return (displacement - 0.5) * params.HeightScale;
	}

	float AdjustDisplacementNormalized(float displacement, DisplacementParams params)
	{
		return (displacement - 0.5) * params.DisplacementScale + 0.5 + params.DisplacementOffset;
	}

	float4 AdjustDisplacementNormalized(float4 displacement, DisplacementParams params)
	{
		return float4(AdjustDisplacementNormalized(displacement.x, params), AdjustDisplacementNormalized(displacement.y, params), AdjustDisplacementNormalized(displacement.z, params), AdjustDisplacementNormalized(displacement.w, params));
	}

	// Shared by GetMipLevel and landscape parallax: one ddx/ddy(uv) per pixel when all mips are computed together.
	// (ddx/ddy of uv × dims matches ddx/ddy of uv*dims when dims are uniform per draw.)
	inline float GetMipLevelForTextureDims(float2 textureDims, float2 duvx, float2 duvy, float screenNoise)
	{
		float2 dims = textureDims;

#if !defined(PARALLAX) && !defined(TRUE_PBR)
		dims /= 2.0;
#endif

#if defined(VR)
		dims /= 2.0;
#endif

		float2 dxSize = duvx * dims;
		float2 dySize = duvy * dims;

		float minTexCoordDelta = min(dot(dxSize, dxSize), dot(dySize, dySize));

		float mipLevel = max(0.5 * log2(minTexCoordDelta), 0);

#if !defined(PARALLAX) && !defined(TRUE_PBR)
		mipLevel++;
#endif

#if defined(VR)
		mipLevel++;
#endif

		mipLevel = max(mipLevel + SharedData::MipBias, 0.0);

		mipLevel = floor(mipLevel) + (screenNoise < frac(mipLevel) ? 1.0 : 0.0);

		return mipLevel;
	}

	inline float GetMipLevel(float2 coords, Texture2D<float4> tex, float screenNoise)
	{
		float2 textureDims;
		tex.GetDimensions(textureDims.x, textureDims.y);
		return GetMipLevelForTextureDims(textureDims, ddx(coords), ddy(coords), screenNoise);
	}

#if defined(LANDSCAPE)
#	include "ExtendedMaterials/ExtendedMaterialsLandscape.hlsli"
#endif

#if defined(LANDSCAPE)
	float2 GetParallaxCoords(PS_INPUT input, float distance, float2 coords, float mipLevels[6], float3 viewDir, float3x3 tbn, float noise, DisplacementParams params[6],
		StochasticOffsets sharedOffset, out float pixelOffset, out uint activeMask, out float weights[6])
#else
	float2 GetParallaxCoords(float distance, float2 coords, float mipLevel, float3 viewDir, float3x3 tbn, float noise, Texture2D<float4> tex, SamplerState texSampler, uint channel, DisplacementParams params, out float pixelOffset)
#endif
	{
		pixelOffset = 0.5;
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
		float scalercp = rcp(scale);
#	else
		float scale = 1;
		float scalercp = 1;
#	endif
		float maxHeight = 0.1 * scale;
		activeMask = ComputeActiveMask(w1, w2);
		// Highest parallax mip among contributing layers: large footprint (typically distance / grazing) → fewer POM steps.
		float aggParallaxMip = 0.0;
		[unroll] for (int emMipLi = 0; emMipLi < 6; ++emMipLi)
		{
			if (activeMask & (1u << emMipLi))
				aggParallaxMip = max(aggParallaxMip, mipLevels[emMipLi]);
		}
		float maxStepsF = 16.0;
		maxStepsF = lerp(maxStepsF, 8.0, step(1.45, aggParallaxMip));
		maxStepsF = lerp(maxStepsF, 4.0, step(2.55, aggParallaxMip));
		float distLin = abs(distance);
		// Distance POM: push toward minimum step count from ~0.65k (full) to several km (mostly 4-step march).
		float pomFar = saturate((distLin - 650.0) / 5200.0);
		maxStepsF = lerp(maxStepsF, 4.0, pomFar);
		// Extra high-minification squeeze (continuous, branchless): beyond ~2.75 mip, converge rapidly to 4 steps.
		float pomMinified = saturate((aggParallaxMip - 2.75) / 0.75);
		maxStepsF = lerp(maxStepsF, 4.0, pomMinified);
		// Very far / highly minified: allow 2-step POM to keep height while heavily trading quality for cost.
		float pomUltraFar = saturate((distLin - 3600.0) / 2600.0);
		float pomUltraMinified = saturate((aggParallaxMip - 3.3) / 0.9);
		float pomTwoStep = max(pomUltraFar, pomUltraMinified);
		maxStepsF = lerp(maxStepsF, 2.0, pomTwoStep);
		maxStepsF = max(maxStepsF, 2.0);
#else
		float scale = params.HeightScale;
		float maxHeight = 0.1 * scale;
#endif
		float minHeight = maxHeight * 0.5;

		{
#if defined(LANDSCAPE)
			uint numSteps = uint(maxStepsF + 0.5);
			numSteps = clamp(numSteps, 2, max(2, uint(scale * maxStepsF + 0.5)));
#else
			const float maxSteps = 16;
			uint numSteps = uint(maxSteps + 0.5);
			numSteps = clamp(numSteps, 4, max(4, uint(scale * maxSteps)));
#endif
#if defined(LANDSCAPE)
			[branch] if (numSteps <= 2)
			{
				const float stepSize2 = 0.5;
				float2 offsetPerStep2 = viewDirTS.xy * float2(maxHeight, maxHeight) * stepSize2.xx;
				float2 prevOffset2 = viewDirTS.xy * float2(minHeight, minHeight) + coords.xy;
				float2 sample1Offset = prevOffset2 - offsetPerStep2;
				float2 sample2Offset = sample1Offset - offsetPerStep2;

				float h1 = GetTerrainHeightShadowTap(sample1Offset, mipLevels, params, w1, w2, activeMask, sharedOffset) * scalercp + 0.5;
				float h2 = GetTerrainHeightShadowTap(sample2Offset, mipLevels, params, w1, w2, activeMask, sharedOffset) * scalercp + 0.5;

				float2 pt1 = float2(0.0, h2);
				float2 pt2 = float2(0.5, h1);
				if (h1 >= 0.5)
				{
					pt1 = float2(0.5, h1);
					pt2 = float2(1.0, 1.0);
				}

				float delta2 = pt2.x - pt2.y;
				float delta1 = pt1.x - pt1.y;
				float denominator = delta2 - delta1;
				float parallaxAmount = 0.0;
				[flatten] if (denominator != 0.0)
					parallaxAmount = (pt1.x * delta2 - pt2.x * delta1) / denominator;

				float offset2 = (1.0 - parallaxAmount) * -maxHeight + minHeight;
				pixelOffset = saturate(parallaxAmount);
				float2 outCoords2 = viewDirTS.xy * offset2 + coords.xy;
				GetTerrainHeight(noise, input, outCoords2, mipLevels, params, blendFactor, w1, w2, activeMask, sharedOffset, weights);
				return outCoords2;
			}
#endif
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
				// max(layer SampleLevel) >= linear blended height (same scalar as GetTerrainHeightShadowTap / GetTerrainHeight total).
				// With TV tiling fix on, stochastic parallax can exceed SampleLevel — coarse gate disabled (useParallaxCoarseGate).
#	if defined(TERRAIN_VARIATION)
				bool useParallaxCoarseGate = !SharedData::terrainVariationSettings.enableTilingFix;
#	else
				bool useParallaxCoarseGate = true;
#	endif
				// When already minified, the 4× upper-bound prepass (many SampleLevels) often costs more than four shadow taps.
				useParallaxCoarseGate = useParallaxCoarseGate && (aggParallaxMip < 2.75);
				[branch] if (useParallaxCoarseGate)
				{
					float4 heightUpper = float4(
						GetTerrainHeightUpperBoundNonStochastic(currentOffset[0].xy, mipLevels, params, activeMask),
						GetTerrainHeightUpperBoundNonStochastic(currentOffset[0].zw, mipLevels, params, activeMask),
						GetTerrainHeightUpperBoundNonStochastic(currentOffset[1].xy, mipLevels, params, activeMask),
						GetTerrainHeightUpperBoundNonStochastic(currentOffset[1].zw, mipLevels, params, activeMask));
					float4 upperScaled = heightUpper * scalercp + 0.5;
					bool4 coarseMayHit = upperScaled >= currentBound;
					[branch] if (!any(coarseMayHit))
					{
						currHeight.w = GetTerrainHeightShadowTap(currentOffset[1].zw, mipLevels, params, w1, w2, activeMask, sharedOffset) * scalercp + 0.5;
						prevOffset = currentOffset[1].zw;
						prevBound = currentBound.w;
						prevHeight = currHeight.w;
						numSteps -= 4;
						continue;
					}
				}
				currHeight.x = GetTerrainHeightShadowTap(currentOffset[0].xy, mipLevels, params, w1, w2, activeMask, sharedOffset) * scalercp + 0.5;
				currHeight.y = GetTerrainHeightShadowTap(currentOffset[0].zw, mipLevels, params, w1, w2, activeMask, sharedOffset) * scalercp + 0.5;
				currHeight.z = GetTerrainHeightShadowTap(currentOffset[1].xy, mipLevels, params, w1, w2, activeMask, sharedOffset) * scalercp + 0.5;
				currHeight.w = GetTerrainHeightShadowTap(currentOffset[1].zw, mipLevels, params, w1, w2, activeMask, sharedOffset) * scalercp + 0.5;
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
#if defined(LANDSCAPE)
					// One short refinement phase reduces residual stair stepping/stretching on terrain parallax hits.
					if (contactRefinement) {
						break;
					} else {
						contactRefinement = true;
						prevOffset = outOffset;
						prevBound = pt2.x;
						numSteps = 4;
						stepSize *= 0.25;
						offsetPerStep *= 0.25;
						continue;
					}
#else
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
#endif
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

			float offset = (1.0 - parallaxAmount) * -maxHeight + minHeight;
			pixelOffset = saturate(parallaxAmount);
			float2 outCoords = viewDirTS.xy * offset + coords.xy;
#if defined(LANDSCAPE)
			// ProcessTerrainHeightWeights once for final blend weights (loop only used linear height via GetTerrainHeightShadowTap).
			GetTerrainHeight(noise, input, outCoords, mipLevels, params, blendFactor, w1, w2, activeMask, sharedOffset, weights);
#endif
			return outCoords;
		}
	}

	// https://advances.realtimerendering.com/s2006/Tatarchuk-POM.pdf
	// Cheap method of creating shadows using height for a given light source
	float GetParallaxSoftShadowMultiplier(float2 coords, float mipLevel, float3 L, float sh0, Texture2D<float4> tex, SamplerState texSampler, uint channel, float quality, float noise, DisplacementParams params)
	{
		[branch] if (quality > 0.0)
		{
			float2 rayDir = L.xy * 0.1 * params.HeightScale;
			float4 multipliers = rcp((float4(1, 2, 3, 4) + noise));
			float4 sh = 0;
			sh = AdjustDisplacementNormalized(tex.SampleLevel(texSampler, coords + rayDir * multipliers.x, mipLevel)[channel], params);
			if (quality > 0.25)
				sh.y = AdjustDisplacementNormalized(tex.SampleLevel(texSampler, coords + rayDir * multipliers.y, mipLevel)[channel], params);
			if (quality > 0.5)
				sh.z = AdjustDisplacementNormalized(tex.SampleLevel(texSampler, coords + rayDir * multipliers.z, mipLevel)[channel], params);
			if (quality > 0.75)
				sh.w = AdjustDisplacementNormalized(tex.SampleLevel(texSampler, coords + rayDir * multipliers.w, mipLevel)[channel], params);
			return 1.0 - saturate(dot(max(0, sh - sh0), ShadowIntensity)) * quality;
		}
		return 1.0;
	}

}
