// SSDM: Screen Space Displacement Mapping
// Replaces per-pixel parallax occlusion mapping with a screen-space approach.
// The forward pass writes displacement vectors to a UAV; compute passes
// refine them hierarchically before DeferredComposite.

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

	float GetMipLevel(float2 coords, Texture2D<float4> tex, float screenNoise)
	{
		float2 textureDims;
		tex.GetDimensions(textureDims.x, textureDims.y);

#if !defined(PARALLAX) && !defined(TRUE_PBR)
		textureDims /= 2.0;
#endif

#if defined(VR)
		textureDims /= 2.0;
#endif

		float2 texCoordsPerSize = coords * textureDims;

		float2 dxSize = ddx(texCoordsPerSize);
		float2 dySize = ddy(texCoordsPerSize);

		float minTexCoordDelta = min(dot(dxSize, dxSize), dot(dySize, dySize));
		float mipLevel = max(0.5 * log2(minTexCoordDelta), 0);

#if !defined(PARALLAX) && !defined(TRUE_PBR)
		mipLevel++;
#endif

#if defined(VR)
		mipLevel++;
#endif

		mipLevel = floor(mipLevel) + (screenNoise < frac(mipLevel) ? 1.0 : 0.0);

		return mipLevel;
	}

#if defined(LANDSCAPE)
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
		StochasticOffsets sharedOffset, float2 dx, float2 dy,
#		endif
		out float weights[6])
	{
		float heightBlend = 1 + blendFactor * HEIGHT_POWER;
		float heights[6] = { 0, 0, 0, 0, 0, 0 };

		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile0HasDisplacement) != 0 && w1.x > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[0] = ScaleDisplacement(StochasticEffectParallax(TexLandDisplacement0Sampler, SampTerrainParallaxSampler, coords, mipLevels[0], sharedOffset, dx, dy).x, params[0]);
#		else
			heights[0] = ScaleDisplacement(TexLandDisplacement0Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[0]).x, params[0]);
#		endif
		}
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile1HasDisplacement) != 0 && w1.y > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[1] = ScaleDisplacement(StochasticEffectParallax(TexLandDisplacement1Sampler, SampTerrainParallaxSampler, coords, mipLevels[1], sharedOffset, dx, dy).x, params[1]);
#		else
			heights[1] = ScaleDisplacement(TexLandDisplacement1Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[1]).x, params[1]);
#		endif
		}
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile2HasDisplacement) != 0 && w1.z > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[2] = ScaleDisplacement(StochasticEffectParallax(TexLandDisplacement2Sampler, SampTerrainParallaxSampler, coords, mipLevels[2], sharedOffset, dx, dy).x, params[2]);
#		else
			heights[2] = ScaleDisplacement(TexLandDisplacement2Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[2]).x, params[2]);
#		endif
		}
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile3HasDisplacement) != 0 && w1.w > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[3] = ScaleDisplacement(StochasticEffectParallax(TexLandDisplacement3Sampler, SampTerrainParallaxSampler, coords, mipLevels[3], sharedOffset, dx, dy).x, params[3]);
#		else
			heights[3] = ScaleDisplacement(TexLandDisplacement3Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[3]).x, params[3]);
#		endif
		}
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile4HasDisplacement) != 0 && w2.x > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[4] = ScaleDisplacement(StochasticEffectParallax(TexLandDisplacement4Sampler, SampTerrainParallaxSampler, coords, mipLevels[4], sharedOffset, dx, dy).x, params[4]);
#		else
			heights[4] = ScaleDisplacement(TexLandDisplacement4Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[4]).x, params[4]);
#		endif
		}
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile5HasDisplacement) != 0 && w2.y > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[5] = ScaleDisplacement(StochasticEffectParallax(TexLandDisplacement5Sampler, SampTerrainParallaxSampler, coords, mipLevels[5], sharedOffset, dx, dy).x, params[5]);
#		else
			heights[5] = ScaleDisplacement(TexLandDisplacement5Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[5]).x, params[5]);
#		endif
		}

		float total;
		ProcessTerrainHeightWeights(heightBlend, w1, w2, heights, weights, total);
#		if defined(TERRAIN_VARIATION)
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
		StochasticOffsets sharedOffset, float2 dx, float2 dy,
#		endif
		out float weights[6])
	{
		float heightBlend = 1 + blendFactor * HEIGHT_POWER;
		float heights[6] = { 0, 0, 0, 0, 0, 0 };

		if (w1.x > 0.01) {
			[branch] if ((Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::THLand0HasDisplacement) != 0)
			{
#		if defined(TERRAIN_VARIATION)
				heights[0] = ScaleDisplacement(StochasticEffectParallax(TexLandTHDisp0Sampler, SampTerrainParallaxSampler, coords, mipLevels[0], sharedOffset, dx, dy).x, params[0]);
#		else
				heights[0] = ScaleDisplacement(TexLandTHDisp0Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[0]).x, params[0]);
#		endif
			}
			else
			{
#		if defined(TERRAIN_VARIATION)
				heights[0] = ScaleDisplacement(StochasticEffectParallax(TexColorSampler, SampTerrainParallaxSampler, coords, mipLevels[0], sharedOffset, dx, dy).w, params[0]);
#		else
				heights[0] = ScaleDisplacement(TexColorSampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[0]).w, params[0]);
#		endif
			}
		}
		if (w1.y > 0.01) {
			[branch] if ((Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::THLand1HasDisplacement) != 0)
			{
#		if defined(TERRAIN_VARIATION)
				heights[1] = ScaleDisplacement(StochasticEffectParallax(TexLandTHDisp1Sampler, SampTerrainParallaxSampler, coords, mipLevels[1], sharedOffset, dx, dy).x, params[1]);
#		else
				heights[1] = ScaleDisplacement(TexLandTHDisp1Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[1]).x, params[1]);
#		endif
			}
			else
			{
#		if defined(TERRAIN_VARIATION)
				heights[1] = ScaleDisplacement(StochasticEffectParallax(TexLandColor2Sampler, SampTerrainParallaxSampler, coords, mipLevels[1], sharedOffset, dx, dy).w, params[1]);
#		else
				heights[1] = ScaleDisplacement(TexLandColor2Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[1]).w, params[1]);
#		endif
			}
		}
		if (w1.z > 0.01) {
			[branch] if ((Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::THLand2HasDisplacement) != 0)
			{
#		if defined(TERRAIN_VARIATION)
				heights[2] = ScaleDisplacement(StochasticEffectParallax(TexLandTHDisp2Sampler, SampTerrainParallaxSampler, coords, mipLevels[2], sharedOffset, dx, dy).x, params[2]);
#		else
				heights[2] = ScaleDisplacement(TexLandTHDisp2Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[2]).x, params[2]);
#		endif
			}
			else
			{
#		if defined(TERRAIN_VARIATION)
				heights[2] = ScaleDisplacement(StochasticEffectParallax(TexLandColor3Sampler, SampTerrainParallaxSampler, coords, mipLevels[2], sharedOffset, dx, dy).w, params[2]);
#		else
				heights[2] = ScaleDisplacement(TexLandColor3Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[2]).w, params[2]);
#		endif
			}
		}
		[branch] if ((Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::THLand3HasDisplacement) != 0 && w1.w > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[3] = ScaleDisplacement(StochasticEffectParallax(TexLandTHDisp3Sampler, SampTerrainParallaxSampler, coords, mipLevels[3], sharedOffset, dx, dy).x, params[3]);
#		else
			heights[3] = ScaleDisplacement(TexLandTHDisp3Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[3]).x, params[3]);
#		endif
		}
		else if (w1.w > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[3] = ScaleDisplacement(StochasticEffectParallax(TexLandColor4Sampler, SampTerrainParallaxSampler, coords, mipLevels[3], sharedOffset, dx, dy).w, params[3]);
#		else
			heights[3] = ScaleDisplacement(TexLandColor4Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[3]).w, params[3]);
#		endif
		}
		[branch] if ((Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::THLand4HasDisplacement) != 0 && w2.x > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[4] = ScaleDisplacement(StochasticEffectParallax(TexLandTHDisp4Sampler, SampTerrainParallaxSampler, coords, mipLevels[4], sharedOffset, dx, dy).x, params[4]);
#		else
			heights[4] = ScaleDisplacement(TexLandTHDisp4Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[4]).x, params[4]);
#		endif
		}
		else if (w2.x > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[4] = ScaleDisplacement(StochasticEffectParallax(TexLandColor5Sampler, SampTerrainParallaxSampler, coords, mipLevels[4], sharedOffset, dx, dy).w, params[4]);
#		else
			heights[4] = ScaleDisplacement(TexLandColor5Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[4]).w, params[4]);
#		endif
		}
		[branch] if ((Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::THLand5HasDisplacement) != 0 && w2.y > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[5] = ScaleDisplacement(StochasticEffectParallax(TexLandTHDisp5Sampler, SampTerrainParallaxSampler, coords, mipLevels[5], sharedOffset, dx, dy).x, params[5]);
#		else
			heights[5] = ScaleDisplacement(TexLandTHDisp5Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[5]).x, params[5]);
#		endif
		}
		else if (w2.y > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[5] = ScaleDisplacement(StochasticEffectParallax(TexLandColor6Sampler, SampTerrainParallaxSampler, coords, mipLevels[5], sharedOffset, dx, dy).w, params[5]);
#		else
			heights[5] = ScaleDisplacement(TexLandColor6Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[5]).w, params[5]);
#		endif
		}

		float total;
		ProcessTerrainHeightWeights(heightBlend, w1, w2, heights, weights, total);
#		if defined(TERRAIN_VARIATION)
		[branch] if (SharedData::terrainVariationSettings.enableTilingFix)
		{
			total *= 1.3;
		}
#		endif
		return total;
	}
#	endif

#endif

	float2 ComputeDisplacementVector(float3 viewPos, float3 normalVS, float height, float displacementScale, uint eyeIndex)
	{
		float3 displacedPos = viewPos - normalVS * height * 32;
		float2 uvOriginal = FrameBuffer::ViewToUV(viewPos, true, eyeIndex);
		float2 uvDisplaced = FrameBuffer::ViewToUV(displacedPos, true, eyeIndex);
		return uvDisplaced - uvOriginal;
	}
}
