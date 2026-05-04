#ifndef EXTENDED_MATERIALS_TERRAIN_HLSLI
#define EXTENDED_MATERIALS_TERRAIN_HLSLI

// Included only for LANDSCAPE Lighting permutations (see ExtendedMaterials.hlsli).

	void InitializeTerrainMipLevels(float2 coords, out float mipLevels[6])
	{
		mipLevels[0] = GetMipLevel(coords, TexColorSampler);
		mipLevels[1] = GetMipLevel(coords, TexLandColor2Sampler);
		mipLevels[2] = GetMipLevel(coords, TexLandColor3Sampler);
		mipLevels[3] = GetMipLevel(coords, TexLandColor4Sampler);
		mipLevels[4] = GetMipLevel(coords, TexLandColor5Sampler);
		mipLevels[5] = GetMipLevel(coords, TexLandColor6Sampler);
	}

	// Offsets are ignored when TERRAIN_VARIATION is unset (SampleLevel path).
	inline float4 TerrainParallaxTexSample(Texture2D tex, float2 uv, float mipLevel, StochasticOffsets sharedOffset)
	{
#	if defined(TERRAIN_VARIATION)
		return StochasticEffectParallax(tex, SampTerrainParallaxSampler, uv, mipLevel, sharedOffset);
#	else
		return tex.SampleLevel(SampTerrainParallaxSampler, uv, mipLevel);
#	endif
	}

#	define HEIGHT_POWER 2
#	define HEIGHT_MULT 8

	// [loop] on fixed-6: less compile/optimizer blow-up than [unroll] here (tiny runtime cost).
	void ProcessTerrainHeightWeights(float heightBlend, float4 w1, float2 w2, float heights[6], inout float weights[6], out float totalHeight)
	{
		weights[0] = w1.x;
		weights[1] = w1.y;
		weights[2] = w1.z;
		weights[3] = w1.w;
		weights[4] = w2.x;
		weights[5] = w2.y;

		totalHeight = 0;
		[loop] for (int i = 0; i < 6; i++)
		{
			totalHeight += heights[i] * weights[i];
		}

		if (heightBlend <= 1.0) {
			float wsum = 0;
			[loop] for (int j = 0; j < 6; j++)
			{
				wsum += weights[j];
			}

			float invwsum = rcp(wsum);
			[loop] for (int k = 0; k < 6; k++)
			{
				weights[k] *= invwsum;
			}
			return;
		}

		[loop] for (int hbIdx = 0; hbIdx < 6; hbIdx++)
		{
			weights[hbIdx] *= pow(heightBlend, HEIGHT_MULT * heights[hbIdx]);
		}

		[loop] for (int j = 0; j < 6; j++)
		{
			weights[j] = min(100, pow(abs(weights[j]), heightBlend));
		}

		float wsum = 0;
		[loop] for (int k = 0; k < 6; k++)
		{
			wsum += weights[k];
		}

		float invwsum = rcp(wsum);
		[loop] for (int l = 0; l < 6; l++)
		{
			weights[l] *= invwsum;
		}
	}

	// Blend four per-tap height vectors like four sequential GetTerrainHeight calls; weights output matches tap 3 (last UV).
	float4 FinishTerrainHeightQuadBlend(float heightBlend, float4 w1, float2 w2,
		float qh0[6], float qh1[6], float qh2[6], float qh3[6], out float weights[6])
	{
		float wTmp[6];
		float t0, t1, t2, t3;
		ProcessTerrainHeightWeights(heightBlend, w1, w2, qh0, wTmp, t0);
		ProcessTerrainHeightWeights(heightBlend, w1, w2, qh1, wTmp, t1);
		ProcessTerrainHeightWeights(heightBlend, w1, w2, qh2, wTmp, t2);
		ProcessTerrainHeightWeights(heightBlend, w1, w2, qh3, weights, t3);
		return float4(t0, t1, t2, t3);
	}

#	if defined(TRUE_PBR)

// FXC does not substitute macro args inside `::EnumParam`; pass full scoped flags as TILEFLAG.
#define EM_PBR_DISP_LAYER_SCALAR(N, TILEFLAG, TEX, WGT) \
		[branch] if ((PBRFlags & (TILEFLAG)) != 0 && (WGT) > 0.01) \
		{ \
			heights[N] = ScaleDisplacement(TerrainParallaxTexSample(TEX, coords, mipLevels[N], sharedOffset).x, params[N]); \
		}

#define EM_PBR_DISP_LAYER_QUAD(N, TILEFLAG, TEX, WGT) \
		[branch] if ((PBRFlags & (TILEFLAG)) != 0 && (WGT) > 0.01) \
		{ \
			[unroll] for (uint k = 0; k < 4; k++) \
				h4[k][N] = ScaleDisplacement(TerrainParallaxTexSample(TEX, uvs[k], mipLevels[N], sharedOffset).x, params[N]); \
		}

#define EM_PBR_DISP_FOREACH(M) \
		M(0, PBR::TerrainFlags::LandTile0HasDisplacement, TexLandDisplacement0Sampler, w1.x) \
		M(1, PBR::TerrainFlags::LandTile1HasDisplacement, TexLandDisplacement1Sampler, w1.y) \
		M(2, PBR::TerrainFlags::LandTile2HasDisplacement, TexLandDisplacement2Sampler, w1.z) \
		M(3, PBR::TerrainFlags::LandTile3HasDisplacement, TexLandDisplacement3Sampler, w1.w) \
		M(4, PBR::TerrainFlags::LandTile4HasDisplacement, TexLandDisplacement4Sampler, w2.x) \
		M(5, PBR::TerrainFlags::LandTile5HasDisplacement, TexLandDisplacement5Sampler, w2.y)

	float GetTerrainHeight(float screenNoise, PS_INPUT input, float2 coords, float mipLevels[6], DisplacementParams params[6], float blendFactor, float4 w1, float2 w2,
		StochasticOffsets sharedOffset,
		out float weights[6])
	{
		float heightBlend = 1 + blendFactor * HEIGHT_POWER;
		float heights[6] = { 0, 0, 0, 0, 0, 0 };

		EM_PBR_DISP_FOREACH(EM_PBR_DISP_LAYER_SCALAR)

		float total;
		ProcessTerrainHeightWeights(heightBlend, w1, w2, heights, weights, total);
		return total;
	}

	// Ray-march coarse step: one branch tree per layer; inner [unroll] fans out four UVs (same IR as manual unroll).
	float4 GetTerrainHeightQuadRayMarch(float screenNoise, PS_INPUT input,
		float2 u0, float2 u1, float2 u2, float2 u3,
		float mipLevels[6], DisplacementParams params[6], float blendFactor, float4 w1, float2 w2,
		StochasticOffsets sharedOffset,
		out float weights[6])
	{
		float heightBlend = 1 + blendFactor * HEIGHT_POWER;
		float2 uvs[4] = { u0, u1, u2, u3 };
		float h4[4][6];
		[unroll] for (uint qi = 0; qi < 4; qi++)
			[unroll] for (uint lj = 0; lj < 6; lj++)
				h4[qi][lj] = 0;

		EM_PBR_DISP_FOREACH(EM_PBR_DISP_LAYER_QUAD)

		return FinishTerrainHeightQuadBlend(heightBlend, w1, w2, h4[0], h4[1], h4[2], h4[3], weights);
	}

#undef EM_PBR_DISP_LAYER_SCALAR
#undef EM_PBR_DISP_LAYER_QUAD
#undef EM_PBR_DISP_FOREACH

#	else

#define EM_LEGACY_LAYER012_SCALAR(N, THFLAG, THSAMPLER, COLSAMPLER, WGT) \
		if ((WGT) > 0.01) { \
			[branch] if ((Permutation::ExtraFeatureDescriptor & (THFLAG)) != 0) \
			{ \
				heights[N] = ScaleDisplacement(TerrainParallaxTexSample(THSAMPLER, coords, mipLevels[N], sharedOffset).x, params[N]); \
			} \
			else \
			{ \
				heights[N] = ScaleDisplacement(TerrainParallaxTexSample(COLSAMPLER, coords, mipLevels[N], sharedOffset).w, params[N]); \
			} \
		}

#define EM_LEGACY_LAYER345_SCALAR(N, THFLAG, THSAMPLER, COLSAMPLER, WPRIMARY, WELSE) \
		[branch] if ((Permutation::ExtraFeatureDescriptor & (THFLAG)) != 0 && (WPRIMARY) > 0.01) \
		{ \
			heights[N] = ScaleDisplacement(TerrainParallaxTexSample(THSAMPLER, coords, mipLevels[N], sharedOffset).x, params[N]); \
		} \
		else if ((WELSE) > 0.01) \
		{ \
			heights[N] = ScaleDisplacement(TerrainParallaxTexSample(COLSAMPLER, coords, mipLevels[N], sharedOffset).w, params[N]); \
		}

#define EM_LEGACY_LAYER012_QUAD(N, THFLAG, THSAMPLER, COLSAMPLER, WGT) \
		if ((WGT) > 0.01) { \
			[branch] if ((Permutation::ExtraFeatureDescriptor & (THFLAG)) != 0) \
			{ \
				[unroll] for (uint k = 0; k < 4; k++) \
					h4[k][N] = ScaleDisplacement(TerrainParallaxTexSample(THSAMPLER, uvs[k], mipLevels[N], sharedOffset).x, params[N]); \
			} \
			else \
			{ \
				[unroll] for (uint k = 0; k < 4; k++) \
					h4[k][N] = ScaleDisplacement(TerrainParallaxTexSample(COLSAMPLER, uvs[k], mipLevels[N], sharedOffset).w, params[N]); \
			} \
		}

#define EM_LEGACY_LAYER345_QUAD(N, THFLAG, THSAMPLER, COLSAMPLER, WPRIMARY, WELSE) \
		[branch] if ((Permutation::ExtraFeatureDescriptor & (THFLAG)) != 0 && (WPRIMARY) > 0.01) \
		{ \
			[unroll] for (uint k = 0; k < 4; k++) \
				h4[k][N] = ScaleDisplacement(TerrainParallaxTexSample(THSAMPLER, uvs[k], mipLevels[N], sharedOffset).x, params[N]); \
		} \
		else if ((WELSE) > 0.01) \
		{ \
			[unroll] for (uint k = 0; k < 4; k++) \
				h4[k][N] = ScaleDisplacement(TerrainParallaxTexSample(COLSAMPLER, uvs[k], mipLevels[N], sharedOffset).w, params[N]); \
		}

	float GetTerrainHeight(float screenNoise, PS_INPUT input, float2 coords, float mipLevels[6], DisplacementParams params[6], float blendFactor, float4 w1, float2 w2,
		StochasticOffsets sharedOffset,
		out float weights[6])
	{
		float heightBlend = 1 + blendFactor * HEIGHT_POWER;
		float heights[6] = { 0, 0, 0, 0, 0, 0 };

		EM_LEGACY_LAYER012_SCALAR(0, Permutation::ExtraFeatureFlags::THLand0HasDisplacement, TexLandTHDisp0Sampler, TexColorSampler, w1.x)
		EM_LEGACY_LAYER012_SCALAR(1, Permutation::ExtraFeatureFlags::THLand1HasDisplacement, TexLandTHDisp1Sampler, TexLandColor2Sampler, w1.y)
		EM_LEGACY_LAYER012_SCALAR(2, Permutation::ExtraFeatureFlags::THLand2HasDisplacement, TexLandTHDisp2Sampler, TexLandColor3Sampler, w1.z)
		EM_LEGACY_LAYER345_SCALAR(3, Permutation::ExtraFeatureFlags::THLand3HasDisplacement, TexLandTHDisp3Sampler, TexLandColor4Sampler, w1.w, w1.w)
		EM_LEGACY_LAYER345_SCALAR(4, Permutation::ExtraFeatureFlags::THLand4HasDisplacement, TexLandTHDisp4Sampler, TexLandColor5Sampler, w2.x, w2.x)
		EM_LEGACY_LAYER345_SCALAR(5, Permutation::ExtraFeatureFlags::THLand5HasDisplacement, TexLandTHDisp5Sampler, TexLandColor6Sampler, w2.y, w2.y)

		float total;
		ProcessTerrainHeightWeights(heightBlend, w1, w2, heights, weights, total);
		return total;
	}

	// Legacy TH/color paths: same branching as GetTerrainHeight; fan out four UVs per branch.
	float4 GetTerrainHeightQuadRayMarch(float screenNoise, PS_INPUT input,
		float2 u0, float2 u1, float2 u2, float2 u3,
		float mipLevels[6], DisplacementParams params[6], float blendFactor, float4 w1, float2 w2,
		StochasticOffsets sharedOffset,
		out float weights[6])
	{
		float heightBlend = 1 + blendFactor * HEIGHT_POWER;
		float2 uvs[4] = { u0, u1, u2, u3 };
		float h4[4][6];
		[unroll] for (uint qi = 0; qi < 4; qi++)
			[unroll] for (uint lj = 0; lj < 6; lj++)
				h4[qi][lj] = 0;

		EM_LEGACY_LAYER012_QUAD(0, Permutation::ExtraFeatureFlags::THLand0HasDisplacement, TexLandTHDisp0Sampler, TexColorSampler, w1.x)
		EM_LEGACY_LAYER012_QUAD(1, Permutation::ExtraFeatureFlags::THLand1HasDisplacement, TexLandTHDisp1Sampler, TexLandColor2Sampler, w1.y)
		EM_LEGACY_LAYER012_QUAD(2, Permutation::ExtraFeatureFlags::THLand2HasDisplacement, TexLandTHDisp2Sampler, TexLandColor3Sampler, w1.z)
		EM_LEGACY_LAYER345_QUAD(3, Permutation::ExtraFeatureFlags::THLand3HasDisplacement, TexLandTHDisp3Sampler, TexLandColor4Sampler, w1.w, w1.w)
		EM_LEGACY_LAYER345_QUAD(4, Permutation::ExtraFeatureFlags::THLand4HasDisplacement, TexLandTHDisp4Sampler, TexLandColor5Sampler, w2.x, w2.x)
		EM_LEGACY_LAYER345_QUAD(5, Permutation::ExtraFeatureFlags::THLand5HasDisplacement, TexLandTHDisp5Sampler, TexLandColor6Sampler, w2.y, w2.y)

		return FinishTerrainHeightQuadBlend(heightBlend, w1, w2, h4[0], h4[1], h4[2], h4[3], weights);
	}

#undef EM_LEGACY_LAYER012_SCALAR
#undef EM_LEGACY_LAYER345_SCALAR
#undef EM_LEGACY_LAYER012_QUAD
#undef EM_LEGACY_LAYER345_QUAD

#	endif
#	if defined(TRUE_PBR)
	static const uint TERRAIN_DISPLACEMENT_MASK = (1u << 6u) | (1u << 7u) | (1u << 8u) | (1u << 9u) | (1u << 10u) | (1u << 11u);
#	endif
#	define TERRAIN_HEIGHT_AT(COORDS, MIP, QUALITY, WEIGHTS) \
		GetTerrainHeight(noise, input, COORDS, MIP, params, SharedData::extendedMaterialSettings.EnableHeightBlending ? 1.0 : 0.0, input.LandBlendWeights1, input.LandBlendWeights2.xy, sharedOffset, WEIGHTS)

	inline bool TerrainHasSignificantBlend(float4 w1, float2 w2)
	{
		return (w1.x + w1.y + w1.z + w1.w + w2.x + w2.y) > 0.01;
	}

	inline bool TerrainHasAnyDisplacement()
	{
#	if defined(TRUE_PBR)
		return (PBRFlags & TERRAIN_DISPLACEMENT_MASK) != 0;
#	else
		// Some distant landscape permutations can lose THLandHasDisplacement even though
		// legacy terrain parallax still uses alpha-based displacement.
		return SharedData::extendedMaterialSettings.EnableTerrainParallax ||
		       (Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::THLandHasDisplacement) != 0;
#	endif
	}

	inline float TerrainMaxWeightedHeightScale(PS_INPUT input, DisplacementParams params[6])
	{
		return max(params[0].HeightScale * input.LandBlendWeights1.x, max(params[1].HeightScale * input.LandBlendWeights1.y, max(params[2].HeightScale * input.LandBlendWeights1.z,
																																 max(params[3].HeightScale * input.LandBlendWeights1.w, max(params[4].HeightScale * input.LandBlendWeights2.x, params[5].HeightScale * input.LandBlendWeights2.y)))));
	}

	inline uint TerrainDirectionalShadowTapCount(float quality)
	{
		// Directional terrain shadows are capped to reduce cost.
		if (quality > 0.7)
			return 2;
		if (quality > 0.0)
			return 1;
		return 0;
	}

	bool ComputeTerrainParallaxShadowBaseHeight(PS_INPUT input, float2 coords, float mipLevels[6], float quality, float noise, DisplacementParams params[6], StochasticOffsets sharedOffset, out float sh0)
	{
		sh0 = 0.0;
		if (!TerrainHasSignificantBlend(input.LandBlendWeights1, input.LandBlendWeights2.xy))
			return false;
		if (!TerrainHasAnyDisplacement())
			return false;

		float weights[6] = { 0, 0, 0, 0, 0, 0 };
		sh0 = TERRAIN_HEIGHT_AT(coords, mipLevels, quality, weights);
		return true;
	}

	float GetParallaxSoftShadowMultiplierTerrain(PS_INPUT input, float2 coords, float mipLevel[6], float3 L, float sh0, float quality, float noise, DisplacementParams params[6], StochasticOffsets sharedOffset)
	{
		if (quality > 0.0) {
			uint tapCount = ParallaxShadowTapCount(quality);
			float shadowStrength = ShadowIntensity * (4.0 / tapCount);
			float4 multipliers = rcp((float4(1, 2, 3, 4) + noise));
			float4 sh = sh0.xxxx;
			float heights[6] = { 0, 0, 0, 0, 0, 0 };
			float2 rayDir = L.xy * 0.1;
			float shadowScaleInv = 1.0;

#	if defined(TRUE_PBR)
			float scale = TerrainMaxWeightedHeightScale(input, params);
			if (scale < 0.01)
				return 1.0;
			rayDir *= scale;
			shadowScaleInv = rcp(scale);
#	endif
			sh.x = TERRAIN_HEIGHT_AT(coords + rayDir * multipliers.x, mipLevel, quality, heights);
			if (quality > 0.25)
				sh.y = TERRAIN_HEIGHT_AT(coords + rayDir * multipliers.y, mipLevel, quality, heights);
			if (quality > 0.5)
				sh.z = TERRAIN_HEIGHT_AT(coords + rayDir * multipliers.z, mipLevel, quality, heights);
			if (quality > 0.75)
				sh.w = TERRAIN_HEIGHT_AT(coords + rayDir * multipliers.w, mipLevel, quality, heights);
			return 1.0 - saturate(dot(max(0, sh - sh0) * shadowScaleInv, shadowStrength));
		}
		return 1.0;
	}

	float EvaluateTerrainDirectionalParallaxShadowMultiplier(PS_INPUT input, float2 coords, float mipLevels[6], float3 lightDirection, float quality, float noise, DisplacementParams params[6], StochasticOffsets sharedOffset, float sh0)
	{
		uint tapCount = TerrainDirectionalShadowTapCount(quality);
		if (tapCount == 0)
			return 1.0;
		float shadowStrength = ShadowIntensity * (2.0 / tapCount);
		if (!TerrainHasSignificantBlend(input.LandBlendWeights1, input.LandBlendWeights2.xy))
			return 1.0;

		float4 multipliers = rcp((float4(1, 2, 3, 4) + noise));
		float4 sh = sh0.xxxx;
		float heights[6] = { 0, 0, 0, 0, 0, 0 };
		float2 rayDir = lightDirection.xy * 0.1;
		float shadowScaleInv = 1.0;

#	if defined(TRUE_PBR)
		float scale = TerrainMaxWeightedHeightScale(input, params);
		if (scale < 0.01)
			return 1.0;
		rayDir *= scale;
		shadowScaleInv = rcp(scale);
#	endif

		sh.x = TERRAIN_HEIGHT_AT(coords + rayDir * multipliers.x, mipLevels, quality, heights);
		if (tapCount > 1)
			sh.y = TERRAIN_HEIGHT_AT(coords + rayDir * multipliers.y, mipLevels, quality, heights);

		return 1.0 - saturate(dot(max(0, sh - sh0) * shadowScaleInv, shadowStrength));
	}

	float EvaluateTerrainParallaxShadowMultiplier(PS_INPUT input, float2 coords, float mipLevels[6], float3 lightDirection, float quality, float noise, DisplacementParams params[6], StochasticOffsets sharedOffset, out float sh0)
	{
		if (!ComputeTerrainParallaxShadowBaseHeight(input, coords, mipLevels, quality, noise, params, sharedOffset, sh0))
			return 1.0;
		return GetParallaxSoftShadowMultiplierTerrain(input, coords, mipLevels, lightDirection, sh0, quality, noise, params, sharedOffset);
	}

#	undef TERRAIN_HEIGHT_AT

#endif  // EXTENDED_MATERIALS_TERRAIN_HLSLI