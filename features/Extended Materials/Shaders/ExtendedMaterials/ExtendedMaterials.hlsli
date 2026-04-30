// https://github.com/tgjones/slimshader-cpp/blob/master/src/Shaders/Sdk/Direct3D11/DetailTessellation11/POM.hlsl
// https://github.com/alandtse/SSEShaderTools/blob/main/shaders_vr/ParallaxEffect.h

// https://github.com/marselas/Zombie-Direct3D-Samples/blob/5f53dc2d6f7deb32eb2e5e438d6b6644430fe9ee/Direct3D/ParallaxOcclusionMapping/ParallaxOcclusionMapping.fx
// http://www.diva-portal.org/smash/get/diva2:831762/FULLTEXT01.pdf
// https://bartwronski.files.wordpress.com/2014/03/ac4_gdc.pdf

#if defined(TERRAIN_VARIATION) && defined(LANDSCAPE)
#	include "TerrainVariation/TerrainVariation.hlsli"
#endif

struct DisplacementParams
{
	float DisplacementScale;
	float DisplacementOffset;
	float HeightScale;
	float FlattenAmount;
};

namespace ExtendedMaterials
{
	static const float ShadowIntensity = 2.0;
	static const float ParallaxCheapDistance = 512.0;
	static const float ParallaxNearShadowQuality = 1.0;
	static const float ParallaxFarShadowQuality = 0.5;
	static const float TerrainParallaxShadowMaxMipLevel = 1.0;
	static const float TargetTexelDensity = 4.0;

	inline uint ParallaxShadowTapCount(float quality)
	{
		uint taps = 1;
		if (quality > 0.25)
			taps++;
		if (quality > 0.5)
			taps++;
		if (quality > 0.75)
			taps++;
		return taps;
	}

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

	float GetMipLevel(float2 coords, Texture2D<float4> tex, float3 worldPos)
	{
		float2 textureDims;
		tex.GetDimensions(textureDims.x, textureDims.y);

		float2 dxUV = ddx(coords);
		float2 dyUV = ddy(coords);
		float3 dxWorld = ddx(worldPos);
		float3 dyWorld = ddy(worldPos);

		float ratioX = dot(dxUV, dxUV) / max(dot(dxWorld, dxWorld), EPSILON_DIVISION);
		float ratioY = dot(dyUV, dyUV) / max(dot(dyWorld, dyWorld), EPSILON_DIVISION);

		float mipLevel = 0.5 * log2(min(ratioX, ratioY)) + log2(max(textureDims.x, textureDims.y) / EPSILON_DIVISION);
		mipLevel = max(mipLevel, 0);

		return mipLevel;
	}

#if defined(LANDSCAPE)
	void InitializeTerrainMipLevels(float2 coords, float3 worldPos, out float mipLevels[6])
	{
		mipLevels[0] = GetMipLevel(coords, TexColorSampler, worldPos);
		mipLevels[1] = GetMipLevel(coords, TexLandColor2Sampler, worldPos);
		mipLevels[2] = GetMipLevel(coords, TexLandColor3Sampler, worldPos);
		mipLevels[3] = GetMipLevel(coords, TexLandColor4Sampler, worldPos);
		mipLevels[4] = GetMipLevel(coords, TexLandColor5Sampler, worldPos);
		mipLevels[5] = GetMipLevel(coords, TexLandColor6Sampler, worldPos);
	}

#	define HEIGHT_POWER 2
#	define HEIGHT_MULT 8

	void ProcessTerrainHeightWeights(float heightBlend, float4 w1, float2 w2, float heights[6], inout float weights[6], out float totalHeight)
	{
		weights[0] = w1.x;
		weights[1] = w1.y;
		weights[2] = w1.z;
		weights[3] = w1.w;
		weights[4] = w2.x;
		weights[5] = w2.y;

		totalHeight = 0;
		[unroll] for (int i = 0; i < 6; i++)
		{
			totalHeight += heights[i] * weights[i];
		}

		if (heightBlend <= 1.0) {
			float wsum = 0;
			[unroll] for (int j = 0; j < 6; j++)
			{
				wsum += weights[j];
			}

			float invwsum = rcp(wsum);
			[unroll] for (int k = 0; k < 6; k++)
			{
				weights[k] *= invwsum;
			}
			return;
		}

		[unroll] for (int i = 0; i < 6; i++)
		{
			weights[i] *= pow(heightBlend, HEIGHT_MULT * heights[i]);
		}

		[unroll] for (int j = 0; j < 6; j++)
		{
			weights[j] = min(100, pow(abs(weights[j]), heightBlend));
		}

		float wsum = 0;
		[unroll] for (int k = 0; k < 6; k++)
		{
			wsum += weights[k];
		}

		float invwsum = rcp(wsum);
		[unroll] for (int l = 0; l < 6; l++)
		{
			weights[l] *= invwsum;
		}
	}

#	if defined(TRUE_PBR)
	float GetTerrainHeight(float screenNoise, PS_INPUT input, float2 coords, float mipLevels[6], DisplacementParams params[6], float blendFactor, float4 w1, float2 w2,
#		if defined(TERRAIN_VARIATION)
		StochasticOffsets sharedOffset,
#		endif
		out float weights[6])
	{
		float heightBlend = 1 + blendFactor * HEIGHT_POWER;
		float heights[6] = { 0, 0, 0, 0, 0, 0 };

		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile0HasDisplacement) != 0 && w1.x > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[0] = ScaleDisplacement(StochasticEffectParallax(TexLandDisplacement0Sampler, SampTerrainParallaxSampler, coords, mipLevels[0], sharedOffset).x, params[0]);
#		else
			heights[0] = ScaleDisplacement(TexLandDisplacement0Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[0]).x, params[0]);
#		endif
		}
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile1HasDisplacement) != 0 && w1.y > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[1] = ScaleDisplacement(StochasticEffectParallax(TexLandDisplacement1Sampler, SampTerrainParallaxSampler, coords, mipLevels[1], sharedOffset).x, params[1]);
#		else
			heights[1] = ScaleDisplacement(TexLandDisplacement1Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[1]).x, params[1]);
#		endif
		}
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile2HasDisplacement) != 0 && w1.z > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[2] = ScaleDisplacement(StochasticEffectParallax(TexLandDisplacement2Sampler, SampTerrainParallaxSampler, coords, mipLevels[2], sharedOffset).x, params[2]);
#		else
			heights[2] = ScaleDisplacement(TexLandDisplacement2Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[2]).x, params[2]);
#		endif
		}
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile3HasDisplacement) != 0 && w1.w > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[3] = ScaleDisplacement(StochasticEffectParallax(TexLandDisplacement3Sampler, SampTerrainParallaxSampler, coords, mipLevels[3], sharedOffset).x, params[3]);
#		else
			heights[3] = ScaleDisplacement(TexLandDisplacement3Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[3]).x, params[3]);
#		endif
		}
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile4HasDisplacement) != 0 && w2.x > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[4] = ScaleDisplacement(StochasticEffectParallax(TexLandDisplacement4Sampler, SampTerrainParallaxSampler, coords, mipLevels[4], sharedOffset).x, params[4]);
#		else
			heights[4] = ScaleDisplacement(TexLandDisplacement4Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[4]).x, params[4]);
#		endif
		}
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile5HasDisplacement) != 0 && w2.y > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[5] = ScaleDisplacement(StochasticEffectParallax(TexLandDisplacement5Sampler, SampTerrainParallaxSampler, coords, mipLevels[5], sharedOffset).x, params[5]);
#		else
			heights[5] = ScaleDisplacement(TexLandDisplacement5Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[5]).x, params[5]);
#		endif
		}

		float total;
		ProcessTerrainHeightWeights(heightBlend, w1, w2, heights, weights, total);
#		if defined(TERRAIN_VARIATION)
		// Boost height by 30% when terrain variation is enabled to enhance depth perception
		[branch] if (SharedData::terrainVariationSettings.enableTilingFix)
		{
			total *= 1.3;
		}
#		endif
		return total;
	}

#	else
	float GetTerrainHeight(float screenNoise, PS_INPUT input, float2 coords, float mipLevels[6], DisplacementParams params[6], float blendFactor, float4 w1, float2 w2,
#		if defined(TERRAIN_VARIATION)
		StochasticOffsets sharedOffset,
#		endif
		out float weights[6])
	{
		float heightBlend = 1 + blendFactor * HEIGHT_POWER;
		float heights[6] = { 0, 0, 0, 0, 0, 0 };

		if (w1.x > 0.01) {
			[branch] if ((Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::THLand0HasDisplacement) != 0)
			{
#		if defined(TERRAIN_VARIATION)
				heights[0] = ScaleDisplacement(StochasticEffectParallax(TexLandTHDisp0Sampler, SampTerrainParallaxSampler, coords, mipLevels[0], sharedOffset).x, params[0]);
#		else
				heights[0] = ScaleDisplacement(TexLandTHDisp0Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[0]).x, params[0]);
#		endif
			}
			else
			{
#		if defined(TERRAIN_VARIATION)
				heights[0] = ScaleDisplacement(StochasticEffectParallax(TexColorSampler, SampTerrainParallaxSampler, coords, mipLevels[0], sharedOffset).w, params[0]);
#		else
				heights[0] = ScaleDisplacement(TexColorSampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[0]).w, params[0]);
#		endif
			}
		}
		if (w1.y > 0.01) {
			[branch] if ((Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::THLand1HasDisplacement) != 0)
			{
#		if defined(TERRAIN_VARIATION)
				heights[1] = ScaleDisplacement(StochasticEffectParallax(TexLandTHDisp1Sampler, SampTerrainParallaxSampler, coords, mipLevels[1], sharedOffset).x, params[1]);
#		else
				heights[1] = ScaleDisplacement(TexLandTHDisp1Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[1]).x, params[1]);
#		endif
			}
			else
			{
#		if defined(TERRAIN_VARIATION)
				heights[1] = ScaleDisplacement(StochasticEffectParallax(TexLandColor2Sampler, SampTerrainParallaxSampler, coords, mipLevels[1], sharedOffset).w, params[1]);
#		else
				heights[1] = ScaleDisplacement(TexLandColor2Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[1]).w, params[1]);
#		endif
			}
		}
		if (w1.z > 0.01) {
			[branch] if ((Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::THLand2HasDisplacement) != 0)
			{
#		if defined(TERRAIN_VARIATION)
				heights[2] = ScaleDisplacement(StochasticEffectParallax(TexLandTHDisp2Sampler, SampTerrainParallaxSampler, coords, mipLevels[2], sharedOffset).x, params[2]);
#		else
				heights[2] = ScaleDisplacement(TexLandTHDisp2Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[2]).x, params[2]);
#		endif
			}
			else
			{
#		if defined(TERRAIN_VARIATION)
				heights[2] = ScaleDisplacement(StochasticEffectParallax(TexLandColor3Sampler, SampTerrainParallaxSampler, coords, mipLevels[2], sharedOffset).w, params[2]);
#		else
				heights[2] = ScaleDisplacement(TexLandColor3Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[2]).w, params[2]);
#		endif
			}
		}
		[branch] if ((Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::THLand3HasDisplacement) != 0 && w1.w > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[3] = ScaleDisplacement(StochasticEffectParallax(TexLandTHDisp3Sampler, SampTerrainParallaxSampler, coords, mipLevels[3], sharedOffset).x, params[3]);
#		else
			heights[3] = ScaleDisplacement(TexLandTHDisp3Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[3]).x, params[3]);
#		endif
		}
		else if (w1.w > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[3] = ScaleDisplacement(StochasticEffectParallax(TexLandColor4Sampler, SampTerrainParallaxSampler, coords, mipLevels[3], sharedOffset).w, params[3]);
#		else
			heights[3] = ScaleDisplacement(TexLandColor4Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[3]).w, params[3]);
#		endif
		}
		[branch] if ((Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::THLand4HasDisplacement) != 0 && w2.x > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[4] = ScaleDisplacement(StochasticEffectParallax(TexLandTHDisp4Sampler, SampTerrainParallaxSampler, coords, mipLevels[4], sharedOffset).x, params[4]);
#		else
			heights[4] = ScaleDisplacement(TexLandTHDisp4Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[4]).x, params[4]);
#		endif
		}
		else if (w2.x > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[4] = ScaleDisplacement(StochasticEffectParallax(TexLandColor5Sampler, SampTerrainParallaxSampler, coords, mipLevels[4], sharedOffset).w, params[4]);
#		else
			heights[4] = ScaleDisplacement(TexLandColor5Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[4]).w, params[4]);
#		endif
		}
		[branch] if ((Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::THLand5HasDisplacement) != 0 && w2.y > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[5] = ScaleDisplacement(StochasticEffectParallax(TexLandTHDisp5Sampler, SampTerrainParallaxSampler, coords, mipLevels[5], sharedOffset).x, params[5]);
#		else
			heights[5] = ScaleDisplacement(TexLandTHDisp5Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[5]).x, params[5]);
#		endif
		}
		else if (w2.y > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[5] = ScaleDisplacement(StochasticEffectParallax(TexLandColor6Sampler, SampTerrainParallaxSampler, coords, mipLevels[5], sharedOffset).w, params[5]);
#		else
			heights[5] = ScaleDisplacement(TexLandColor6Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[5]).w, params[5]);
#		endif
		}

		float total;
		ProcessTerrainHeightWeights(heightBlend, w1, w2, heights, weights, total);
#		if defined(TERRAIN_VARIATION)
		// Boost height by 30% when terrain variation is enabled to enhance depth perception
		[branch] if (SharedData::terrainVariationSettings.enableTilingFix)
		{
			total *= 1.3;
		}
#		endif
		return total;
	}

#	endif

#endif

#if defined(LANDSCAPE)
	float2 GetParallaxCoords(PS_INPUT input, float distance, float2 coords, float mipLevels[6], float3 viewDir, float3x3 tbn, float noise, DisplacementParams params[6],
#	if defined(TERRAIN_VARIATION)
		StochasticOffsets sharedOffset,
#	endif
		out float pixelOffset,
#	if defined(VR_STEREO_OPT)
		out bool hasPOM,
#	endif
		out float weights[6])
#else
	float2 GetParallaxCoords(float distance, float2 coords, float mipLevel, float3 viewDir, float3x3 tbn, float noise, Texture2D<float4> tex, SamplerState texSampler, uint channel, DisplacementParams params, out float pixelOffset
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

		bool useCheapParallax = distance >= ParallaxCheapDistance;
#if defined(LANDSCAPE) && defined(LANDSCAPE_HEIGHT_APPROX)
		useCheapParallax = true;
#endif

#if defined(LANDSCAPE)
		float blendFactor = SharedData::extendedMaterialSettings.EnableHeightBlending ? 1.0 : 0.0;
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
		float scale = params.HeightScale;
		float maxHeight = 0.1 * scale;
#endif
		float minHeight = maxHeight * 0.5;

		if (useCheapParallax && scale > 0.001) {
#if defined(LANDSCAPE)
			float terrainWeights0[6] = { 0, 0, 0, 0, 0, 0 };
#	if defined(TRUE_PBR)
#		if defined(TERRAIN_VARIATION)
#			define LANDSCAPE_HEIGHT_AT(COORDS, OUT_WEIGHTS) (GetTerrainHeight(noise, input, COORDS, mipLevels, params, blendFactor, w1, w2, sharedOffset, OUT_WEIGHTS) * scalercp + 0.5)
#		else
#			define LANDSCAPE_HEIGHT_AT(COORDS, OUT_WEIGHTS) (GetTerrainHeight(noise, input, COORDS, mipLevels, params, blendFactor, w1, w2, OUT_WEIGHTS) * scalercp + 0.5)
#		endif
#	else
#		if defined(TERRAIN_VARIATION)
#			define LANDSCAPE_HEIGHT_AT(COORDS, OUT_WEIGHTS) (GetTerrainHeight(noise, input, COORDS, mipLevels, params, blendFactor, w1, w2, sharedOffset, OUT_WEIGHTS) + 0.5)
#		else
#			define LANDSCAPE_HEIGHT_AT(COORDS, OUT_WEIGHTS) (GetTerrainHeight(noise, input, COORDS, mipLevels, params, blendFactor, w1, w2, OUT_WEIGHTS) + 0.5)
#		endif
#	endif
			float height0 = saturate(LANDSCAPE_HEIGHT_AT(coords, terrainWeights0));
			float offset0 = (1.0 - height0) * -maxHeight + minHeight;
			float2 refinedCoords = coords.xy + viewDirTS.xy * offset0;
			float height1 = saturate(LANDSCAPE_HEIGHT_AT(refinedCoords, weights));
#	undef LANDSCAPE_HEIGHT_AT
#else
			float height0 = saturate(AdjustDisplacementNormalized(tex.SampleLevel(texSampler, coords, mipLevel)[channel], params));
			float offset0 = (1.0 - height0) * -maxHeight + minHeight;
			float2 refinedCoords = coords.xy + viewDirTS.xy * offset0;
			float height1 = saturate(AdjustDisplacementNormalized(tex.SampleLevel(texSampler, refinedCoords, mipLevel)[channel], params));
#endif

			float heightDelta = abs(height1 - height0);
			float heightStability = saturate(1.0 - heightDelta * 2.0);
			float height = lerp(lerp(height0, height1, 0.5), 0.5, (1.0 - heightStability) * 0.35);
			float offset = (1.0 - height) * -maxHeight + minHeight;
			pixelOffset = saturate(height);
#if defined(VR_STEREO_OPT)
			hasPOM = true;
#endif
			return viewDirTS.xy * offset + coords.xy;
		}
#if defined(TRUE_PBR)
		if ((PBRFlags & PBR::Flags::InterlayerParallax) != 0 || !useCheapParallax)
#else
		if (!useCheapParallax)
#endif
		{
			const float maxSteps = 4;
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
#	if defined(TRUE_PBR)
#		if defined(TERRAIN_VARIATION)
				currHeight.x = GetTerrainHeight(noise, input, currentOffset[0].xy, mipLevels, params, blendFactor, w1, w2, sharedOffset, weights) * scalercp + 0.5;
				currHeight.y = GetTerrainHeight(noise, input, currentOffset[0].zw, mipLevels, params, blendFactor, w1, w2, sharedOffset, weights) * scalercp + 0.5;
				currHeight.z = GetTerrainHeight(noise, input, currentOffset[1].xy, mipLevels, params, blendFactor, w1, w2, sharedOffset, weights) * scalercp + 0.5;
				currHeight.w = GetTerrainHeight(noise, input, currentOffset[1].zw, mipLevels, params, blendFactor, w1, w2, sharedOffset, weights) * scalercp + 0.5;
#		else
				currHeight.x = GetTerrainHeight(noise, input, currentOffset[0].xy, mipLevels, params, blendFactor, w1, w2, weights) * scalercp + 0.5;
				currHeight.y = GetTerrainHeight(noise, input, currentOffset[0].zw, mipLevels, params, blendFactor, w1, w2, weights) * scalercp + 0.5;
				currHeight.z = GetTerrainHeight(noise, input, currentOffset[1].xy, mipLevels, params, blendFactor, w1, w2, weights) * scalercp + 0.5;
				currHeight.w = GetTerrainHeight(noise, input, currentOffset[1].zw, mipLevels, params, blendFactor, w1, w2, weights) * scalercp + 0.5;
#		endif
#	else
#		if defined(TERRAIN_VARIATION)
				currHeight.x = GetTerrainHeight(noise, input, currentOffset[0].xy, mipLevels, params, blendFactor, w1, w2, sharedOffset, weights) + 0.5;
				currHeight.y = GetTerrainHeight(noise, input, currentOffset[0].zw, mipLevels, params, blendFactor, w1, w2, sharedOffset, weights) + 0.5;
				currHeight.z = GetTerrainHeight(noise, input, currentOffset[1].xy, mipLevels, params, blendFactor, w1, w2, sharedOffset, weights) + 0.5;
				currHeight.w = GetTerrainHeight(noise, input, currentOffset[1].zw, mipLevels, params, blendFactor, w1, w2, sharedOffset, weights) + 0.5;
#		else
				currHeight.x = GetTerrainHeight(noise, input, currentOffset[0].xy, mipLevels, params, blendFactor, w1, w2, weights) + 0.5;
				currHeight.y = GetTerrainHeight(noise, input, currentOffset[0].zw, mipLevels, params, blendFactor, w1, w2, weights) + 0.5;
				currHeight.z = GetTerrainHeight(noise, input, currentOffset[1].xy, mipLevels, params, blendFactor, w1, w2, weights) + 0.5;
				currHeight.w = GetTerrainHeight(noise, input, currentOffset[1].zw, mipLevels, params, blendFactor, w1, w2, weights) + 0.5;
#		endif
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

				[unroll] for (uint i = 0; i < 3; i++)
				{
					float denominator = fNear - fFar;
					float r = abs(denominator) > EPSILON_DIVISION ? saturate(fNear / denominator) : 0.5;
					float tSecant = lerp(tNear, tFar, r);
					float2 secantCoords = coords.xy + viewDirTS.xy * (((1.0 - tSecant) * -maxHeight) + minHeight);

					float hSecant;
#if defined(LANDSCAPE)
#	if defined(TRUE_PBR)
#		if defined(TERRAIN_VARIATION)
					hSecant = GetTerrainHeight(noise, input, secantCoords, mipLevels, params, blendFactor, w1, w2, sharedOffset, weights) * scalercp + 0.5;
#		else
					hSecant = GetTerrainHeight(noise, input, secantCoords, mipLevels, params, blendFactor, w1, w2, weights) * scalercp + 0.5;
#		endif
#	else
#		if defined(TERRAIN_VARIATION)
					hSecant = GetTerrainHeight(noise, input, secantCoords, mipLevels, params, blendFactor, w1, w2, sharedOffset, weights) + 0.5;
#		else
					hSecant = GetTerrainHeight(noise, input, secantCoords, mipLevels, params, blendFactor, w1, w2, weights) + 0.5;
#		endif
#	endif
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

#if defined(LANDSCAPE)
		weights[0] = input.LandBlendWeights1.x;
		weights[1] = input.LandBlendWeights1.y;
		weights[2] = input.LandBlendWeights1.z;
		weights[3] = input.LandBlendWeights1.w;
		weights[4] = input.LandBlendWeights2.x;
		weights[5] = input.LandBlendWeights2.y;
#endif

		pixelOffset = 0.0;
		return coords;
	}

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

#if defined(LANDSCAPE)
#	if defined(TRUE_PBR)
	static const uint TERRAIN_DISPLACEMENT_MASK = (1u << 6u) | (1u << 7u) | (1u << 8u) | (1u << 9u) | (1u << 10u) | (1u << 11u);
#	endif
#	if defined(TERRAIN_VARIATION)
#		define TERRAIN_HEIGHT_AT(COORDS, MIP, QUALITY, WEIGHTS) \
			GetTerrainHeight(noise, input, COORDS, MIP, params, SharedData::extendedMaterialSettings.EnableHeightBlending ? 1.0 : 0.0, input.LandBlendWeights1, input.LandBlendWeights2.xy, sharedOffset, WEIGHTS)
#	else
#		define TERRAIN_HEIGHT_AT(COORDS, MIP, QUALITY, WEIGHTS) \
			GetTerrainHeight(noise, input, COORDS, MIP, params, SharedData::extendedMaterialSettings.EnableHeightBlending ? 1.0 : 0.0, input.LandBlendWeights1, input.LandBlendWeights2.xy, WEIGHTS)
#	endif

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

#	if defined(TERRAIN_VARIATION)
	bool ComputeTerrainParallaxShadowBaseHeight(PS_INPUT input, float2 coords, float mipLevels[6], float quality, float noise, DisplacementParams params[6], StochasticOffsets sharedOffset, out float sh0)
#	else
	bool ComputeTerrainParallaxShadowBaseHeight(PS_INPUT input, float2 coords, float mipLevels[6], float quality, float noise, DisplacementParams params[6], out float sh0)
#	endif
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

#	if defined(TERRAIN_VARIATION)
	float GetParallaxSoftShadowMultiplierTerrain(PS_INPUT input, float2 coords, float mipLevel[6], float3 L, float sh0, float quality, float noise, DisplacementParams params[6], StochasticOffsets sharedOffset)
#	else
	float GetParallaxSoftShadowMultiplierTerrain(PS_INPUT input, float2 coords, float mipLevel[6], float3 L, float sh0, float quality, float noise, DisplacementParams params[6])
#	endif
	{
		if (quality > 0.0) {
			uint tapCount = ParallaxShadowTapCount(quality);
			float shadowStrength = ShadowIntensity * (4.0 / tapCount);
			float4 multipliers = rcp((float4(1, 2, 3, 4) + noise));
			float4 sh = sh0.xxxx;
			float heights[6] = { 0, 0, 0, 0, 0, 0 };
			float2 rayDir = L.xy * 0.1;

#	if defined(TRUE_PBR)
			float scale = TerrainMaxWeightedHeightScale(input, params);
			if (scale < 0.01)
				return 1.0;
			rayDir *= scale;
#	endif
			sh.x = TERRAIN_HEIGHT_AT(coords + rayDir * multipliers.x, mipLevel, quality, heights);
			if (quality > 0.25)
				sh.y = TERRAIN_HEIGHT_AT(coords + rayDir * multipliers.y, mipLevel, quality, heights);
			if (quality > 0.5)
				sh.z = TERRAIN_HEIGHT_AT(coords + rayDir * multipliers.z, mipLevel, quality, heights);
			if (quality > 0.75)
				sh.w = TERRAIN_HEIGHT_AT(coords + rayDir * multipliers.w, mipLevel, quality, heights);
#	if defined(TRUE_PBR)
			return 1.0 - saturate(dot(max(0, sh - sh0) / scale, shadowStrength));
#	else
			return 1.0 - saturate(dot(max(0, sh - sh0), shadowStrength));
#	endif
		}
		return 1.0;
	}

#	if defined(TERRAIN_VARIATION)
	float EvaluateTerrainDirectionalParallaxShadowMultiplier(PS_INPUT input, float2 coords, float mipLevels[6], float3 lightDirection, float quality, float noise, DisplacementParams params[6], StochasticOffsets sharedOffset, float sh0)
#	else
	float EvaluateTerrainDirectionalParallaxShadowMultiplier(PS_INPUT input, float2 coords, float mipLevels[6], float3 lightDirection, float quality, float noise, DisplacementParams params[6], float sh0)
#	endif
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

#	if defined(TRUE_PBR)
		float scale = TerrainMaxWeightedHeightScale(input, params);
		if (scale < 0.01)
			return 1.0;
		rayDir *= scale;
#	endif

		sh.x = TERRAIN_HEIGHT_AT(coords + rayDir * multipliers.x, mipLevels, quality, heights);
		if (tapCount > 1)
			sh.y = TERRAIN_HEIGHT_AT(coords + rayDir * multipliers.y, mipLevels, quality, heights);

#	if defined(TRUE_PBR)
		return 1.0 - saturate(dot(max(0, sh - sh0) / scale, shadowStrength));
#	else
		return 1.0 - saturate(dot(max(0, sh - sh0), shadowStrength));
#	endif
	}

#	if defined(TERRAIN_VARIATION)
	float EvaluateTerrainParallaxShadowMultiplier(PS_INPUT input, float2 coords, float mipLevels[6], float3 lightDirection, float quality, float noise, DisplacementParams params[6], StochasticOffsets sharedOffset, out float sh0)
	{
		if (!ComputeTerrainParallaxShadowBaseHeight(input, coords, mipLevels, quality, noise, params, sharedOffset, sh0))
			return 1.0;
		return GetParallaxSoftShadowMultiplierTerrain(input, coords, mipLevels, lightDirection, sh0, quality, noise, params, sharedOffset);
	}
#	else
	float EvaluateTerrainParallaxShadowMultiplier(PS_INPUT input, float2 coords, float mipLevels[6], float3 lightDirection, float quality, float noise, DisplacementParams params[6], out float sh0)
	{
		if (!ComputeTerrainParallaxShadowBaseHeight(input, coords, mipLevels, quality, noise, params, sh0))
			return 1.0;
		return GetParallaxSoftShadowMultiplierTerrain(input, coords, mipLevels, lightDirection, sh0, quality, noise, params);
	}
#	endif

#	undef TERRAIN_HEIGHT_AT
#endif  // defined(LANDSCAPE) && defined(TERRAIN_VARIATION)
}
