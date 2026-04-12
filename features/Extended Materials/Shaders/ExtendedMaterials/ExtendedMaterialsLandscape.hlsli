// Landscape-only Extended Materials helpers. Included only when LANDSCAPE is defined (see ExtendedMaterials.hlsli)
// so non-terrain EMAT permutations skip lexing/parsing this entire file.

#	define HEIGHT_POWER 2
#	define HEIGHT_MULT 8

	// Stochastic blending averages multiple offset samples, flattening the height range.
	// These compensate for that energy loss in parallax height and shadow contrast.
	static const float STOCHASTIC_HEIGHT_BOOST = 1.3;
	static const float STOCHASTIC_SHADOW_GAMMA = 0.8;

	void ProcessTerrainHeightWeights(float heightBlend, float4 w1, float2 w2, float heights[6], inout float weights[6], out float totalHeight)
	{
		weights[0] = w1.x;
		weights[1] = w1.y;
		weights[2] = w1.z;
		weights[3] = w1.w;
		weights[4] = w2.x;
		weights[5] = w2.y;

		float logHB = log2(heightBlend) * HEIGHT_MULT;
		float wsum = 0;
		totalHeight = 0;

		[unroll] for (int i = 0; i < 6; i++)
		{
			totalHeight += heights[i] * weights[i];
			weights[i] *= exp2(logHB * heights[i]);
			weights[i] = min(100.0, exp2(log2(abs(weights[i])) * heightBlend));
			wsum += weights[i];
		}

		float invwsum = rcp(wsum);
		[unroll] for (int j = 0; j < 6; j++)
		{
			weights[j] *= invwsum;
		}
	}

	inline float4 SampleHeightUnified(Texture2D tex, SamplerState samp, float2 coords, float mipLevel, StochasticOffsets offsets)
	{
#	if defined(TERRAIN_VARIATION)
		if (!SharedData::terrainVariationSettings.enableTilingFix)
			return tex.SampleLevel(samp, coords, mipLevel);
		return StochasticEffectParallax(tex, samp, coords, mipLevel, offsets);
#	else
		return tex.SampleLevel(samp, coords, mipLevel);
#	endif
	}

	inline uint ComputeActiveMask(float4 w1, float2 w2)
	{
		uint mask = 0;
		mask |= (w1.x > 0.01) ? 1u : 0u;
		mask |= (w1.y > 0.01) ? 2u : 0u;
		mask |= (w1.z > 0.01) ? 4u : 0u;
		mask |= (w1.w > 0.01) ? 8u : 0u;
		mask |= (w2.x > 0.01) ? 16u : 0u;
		mask |= (w2.y > 0.01) ? 32u : 0u;
		return mask;
	}

	// Parallax height mips: per-layer GetDimensions (displacement vs diffuse sizes can differ per tile).
	// Shared mip from tile 0 only was faster but broke soft shadows when layer mips did not match.
	void ComputeLandscapeParallaxMipLevels(float2 uv, float screenNoise, out float mipLevels[6])
	{
		float2 duvx = ddx(uv);
		float2 duvy = ddy(uv);
		float2 dims;

#	if defined(TRUE_PBR)
#		define EMAT_LAND_PBR_MIPDIMS_FOREACH(i, DISPTEX, COLTEX) \
			if (LandscapeLayers::PbrTileHasDisplacement(i)) { \
				DISPTEX.GetDimensions(dims.x, dims.y); \
				mipLevels[i] = GetMipLevelForTextureDims(dims, duvx, duvy, screenNoise); \
			} else { \
				COLTEX.GetDimensions(dims.x, dims.y); \
				mipLevels[i] = GetMipLevelForTextureDims(dims, duvx, duvy, screenNoise); \
			}
		LANDSCAPE_PBR_LAYER_FOREACH(EMAT_LAND_PBR_MIPDIMS_FOREACH)
#		undef EMAT_LAND_PBR_MIPDIMS_FOREACH
#	else
#		define EMAT_LAND_TH_MIPDIMS_FOREACH(i, THDISP, COLTEX) \
			if (LandscapeLayers::ThTileHasDisplacement(i)) { \
				THDISP.GetDimensions(dims.x, dims.y); \
				mipLevels[i] = GetMipLevelForTextureDims(dims, duvx, duvy, screenNoise); \
			} else { \
				COLTEX.GetDimensions(dims.x, dims.y); \
				mipLevels[i] = GetMipLevelForTextureDims(dims, duvx, duvy, screenNoise); \
			}
		LANDSCAPE_TH_LAYER_FOREACH(EMAT_LAND_TH_MIPDIMS_FOREACH)
#		undef EMAT_LAND_TH_MIPDIMS_FOREACH
#	endif
	}

	// Extra mip for parallax height (and coarse gate) at view distance — cheaper taps, less shimmer than diffuse-only bias.
	void ApplyLandscapeParallaxDistanceMipBias(inout float mipLevels[6], float viewZ)
	{
		float t = saturate((abs(viewZ) - 450.0) / 3800.0);
		float bias = t * 1.05;
		[unroll] for (int li = 0; li < 6; ++li)
			mipLevels[li] = min(mipLevels[li] + bias, 11.0);
	}

	// Single SampleLevel per active layer (no stochastic). max(layer) upper-bounds linear blend height.
	void SampleTerrainLayerHeightsNonStochastic(float2 coords, float mipLevels[6], DisplacementParams params[6], uint activeMask, out float heights[6])
	{
		heights[0] = heights[1] = heights[2] = heights[3] = heights[4] = heights[5] = 0;

#	if defined(TRUE_PBR)
#		define EMAT_LAND_PBR_HEIGHT_NS_FOREACH(i, DISPTEX, COLTEX) \
			[branch] if ((activeMask & (1u << i)) && LandscapeLayers::PbrTileHasDisplacement(i)) \
				heights[i] = ScaleDisplacement(DISPTEX.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[i]).x, params[i]);
		LANDSCAPE_PBR_LAYER_FOREACH(EMAT_LAND_PBR_HEIGHT_NS_FOREACH)
#		undef EMAT_LAND_PBR_HEIGHT_NS_FOREACH
#	else
#		define EMAT_LAND_TH_HEIGHT_NS_FOREACH(i, THDISP, COLTEX) \
			if (activeMask & (1u << i)) { \
				[branch] if (LandscapeLayers::ThTileHasDisplacement(i)) \
					heights[i] = ScaleDisplacement(THDISP.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[i]).x, params[i]); \
				else \
					heights[i] = ScaleDisplacement(COLTEX.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[i]).w, params[i]); \
			}
		LANDSCAPE_TH_LAYER_FOREACH(EMAT_LAND_TH_HEIGHT_NS_FOREACH)
#		undef EMAT_LAND_TH_HEIGHT_NS_FOREACH
#	endif
	}

	inline float GetTerrainHeightUpperBoundNonStochastic(float2 coords, float mipLevels[6], DisplacementParams params[6], uint activeMask)
	{
		float heights[6];
		SampleTerrainLayerHeightsNonStochastic(coords, mipLevels, params, activeMask, heights);
		float m = 0;
		[unroll] for (int li = 0; li < 6; ++li) {
			if (activeMask & (1u << li))
				m = max(m, heights[li]);
		}
		return m;
	}

	void SampleTerrainLayerHeights(float2 coords, float mipLevels[6], DisplacementParams params[6], uint activeMask, StochasticOffsets sharedOffset, out float heights[6])
	{
		heights[0] = heights[1] = heights[2] = heights[3] = heights[4] = heights[5] = 0;

#	if defined(TRUE_PBR)
#		define EMAT_LAND_PBR_HEIGHT_S_FOREACH(i, DISPTEX, COLTEX) \
			[branch] if ((activeMask & (1u << i)) && LandscapeLayers::PbrTileHasDisplacement(i)) \
				heights[i] = ScaleDisplacement(SampleHeightUnified(DISPTEX, SampTerrainParallaxSampler, coords, mipLevels[i], sharedOffset).x, params[i]);
		LANDSCAPE_PBR_LAYER_FOREACH(EMAT_LAND_PBR_HEIGHT_S_FOREACH)
#		undef EMAT_LAND_PBR_HEIGHT_S_FOREACH
#	else
#		define EMAT_LAND_TH_HEIGHT_S_FOREACH(i, THDISP, COLTEX) \
			if (activeMask & (1u << i)) { \
				[branch] if (LandscapeLayers::ThTileHasDisplacement(i)) \
					heights[i] = ScaleDisplacement(SampleHeightUnified(THDISP, SampTerrainParallaxSampler, coords, mipLevels[i], sharedOffset).x, params[i]); \
				else \
					heights[i] = ScaleDisplacement(SampleHeightUnified(COLTEX, SampTerrainParallaxSampler, coords, mipLevels[i], sharedOffset).w, params[i]); \
			}
		LANDSCAPE_TH_LAYER_FOREACH(EMAT_LAND_TH_HEIGHT_S_FOREACH)
#		undef EMAT_LAND_TH_HEIGHT_S_FOREACH
#	endif
	}

	// Linear blend of layer heights × boost (same scalar GetTerrainHeight returns before nonlinear weight remap).
	// Used for soft shadows and POM ray height; ProcessTerrainHeightWeights only affects out-weights, not this total.
	float GetTerrainHeightShadowTap(float2 coords, float mipLevels[6], DisplacementParams params[6], float4 w1, float2 w2, uint activeMask, StochasticOffsets sharedOffset)
	{
		float heights[6];
		SampleTerrainLayerHeights(coords, mipLevels, params, activeMask, sharedOffset, heights);

		if (countbits(activeMask) == 1) {
			uint layerIdx = firstbitlow(activeMask);
			float total = heights[layerIdx];
#	if defined(TERRAIN_VARIATION)
			if (SharedData::terrainVariationSettings.enableTilingFix)
				total *= STOCHASTIC_HEIGHT_BOOST;
#	endif
			return total;
		}

		float total = w1.x * heights[0] + w1.y * heights[1] + w1.z * heights[2] + w1.w * heights[3] + w2.x * heights[4] + w2.y * heights[5];
#	if defined(TERRAIN_VARIATION)
		if (SharedData::terrainVariationSettings.enableTilingFix)
			total *= STOCHASTIC_HEIGHT_BOOST;
#	endif
		return total;
	}

	float GetTerrainHeight(float screenNoise, PS_INPUT input, float2 coords, float mipLevels[6], DisplacementParams params[6], float blendFactor, float4 w1, float2 w2,
		uint activeMask, StochasticOffsets sharedOffset, out float weights[6])
	{
		float heightBlend = 1 + blendFactor * HEIGHT_POWER;
		float heights[6];
		SampleTerrainLayerHeights(coords, mipLevels, params, activeMask, sharedOffset, heights);

		//Single active layer fast path, skips expensive weight processing
		if (countbits(activeMask) == 1) {
			uint layerIdx = firstbitlow(activeMask);
			weights[0] = weights[1] = weights[2] = weights[3] = weights[4] = weights[5] = 0;
			weights[layerIdx] = 1.0;
			float total = heights[layerIdx];
#	if defined(TERRAIN_VARIATION)
			if (SharedData::terrainVariationSettings.enableTilingFix)
				total *= STOCHASTIC_HEIGHT_BOOST;
#	endif
			return total;
		}

		float total = 0;
		ProcessTerrainHeightWeights(heightBlend, w1, w2, heights, weights, total);
#	if defined(TERRAIN_VARIATION)
		if (SharedData::terrainVariationSettings.enableTilingFix)
			total *= STOCHASTIC_HEIGHT_BOOST;
#	endif
		return total;
	}

	float GetParallaxSoftShadowMultiplierTerrain(PS_INPUT input, float2 coords, float mipLevel[6], float3 L, float sh0, float quality, float noise, DisplacementParams params[6], StochasticOffsets sharedOffset)
	{
		if (quality > 0.0) {
			float4 multipliers = rcp((float4(1, 2, 3, 4) + noise));
			float4 sh = 0;
			float2 rayDir = L.xy * 0.1;
			uint activeMask = ComputeActiveMask(input.LandBlendWeights1, input.LandBlendWeights2.xy);

#	if defined(TRUE_PBR)
			float scale = max(params[0].HeightScale * input.LandBlendWeights1.x, max(params[1].HeightScale * input.LandBlendWeights1.y, max(params[2].HeightScale * input.LandBlendWeights1.z,
																																			max(params[3].HeightScale * input.LandBlendWeights1.w, max(params[4].HeightScale * input.LandBlendWeights2.x, params[5].HeightScale * input.LandBlendWeights2.y)))));
			if (scale < 0.01)
				return 1.0;
			rayDir *= scale;
			float scaleRcp = rcp(scale);
#	else
			float scaleRcp = 1;
#	endif

			// Shadow only needs the same scalar as GetTerrainHeight return; ProcessTerrainHeightWeights is redundant (totalHeight is linear in w1/w2 before nonlinear remap).
			sh.x = GetTerrainHeightShadowTap(coords + rayDir * multipliers.x, mipLevel, params, input.LandBlendWeights1, input.LandBlendWeights2.xy, activeMask, sharedOffset);
			if (quality > 0.25)
				sh.y = GetTerrainHeightShadowTap(coords + rayDir * multipliers.y, mipLevel, params, input.LandBlendWeights1, input.LandBlendWeights2.xy, activeMask, sharedOffset);
			if (quality > 0.5)
				sh.z = GetTerrainHeightShadowTap(coords + rayDir * multipliers.z, mipLevel, params, input.LandBlendWeights1, input.LandBlendWeights2.xy, activeMask, sharedOffset);
			if (quality > 0.75)
				sh.w = GetTerrainHeightShadowTap(coords + rayDir * multipliers.w, mipLevel, params, input.LandBlendWeights1, input.LandBlendWeights2.xy, activeMask, sharedOffset);

#		if defined(TERRAIN_VARIATION)
			if (SharedData::terrainVariationSettings.enableTilingFix) {
				float shadowIntensity = saturate(dot(max(0, sh - sh0), 1.0)) * quality;
				shadowIntensity = exp2(log2(shadowIntensity) * STOCHASTIC_SHADOW_GAMMA);
				float invShadow = 1.0 - shadowIntensity;
				return invShadow * invShadow;
			}
#		endif
			float shadowParallaxTerm = 1.0 - saturate(dot(max(0, sh - sh0) * scaleRcp, 1.0)) * quality;
			return shadowParallaxTerm * shadowParallaxTerm;
		}
		return 1.0;
	}
