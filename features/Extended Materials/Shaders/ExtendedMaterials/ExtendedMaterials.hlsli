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

	float GetMipLevel(float2 coords, Texture2D<float4> tex)
	{
		// Compute the current gradients:
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

			// Find min of change in u and v across quad: compute du and dv magnitude across quad
			//float2 dTexCoords = dxSize * dxSize + dySize * dySize;

			// Standard mipmapping uses max here
			float minTexCoordDelta = min(dot(dxSize, dxSize), dot(dySize, dySize));

			// Compute the current mip level  (* 0.5 is effectively computing a square root before )
			float mipLevel = max(0.5 * log2(minTexCoordDelta), 0);

		#if !defined(PARALLAX) && !defined(TRUE_PBR) && !defined(TERRAIN_VARIATION)
				mipLevel++;
		#endif

		// VR: Apply more conservative mipmap level adjustments to reduce over-blurring and shimmering
		#if defined(VR)
			#if defined(TERRAIN_VARIATION) && defined(LANDSCAPE)
				// No additional VR mip penalty when terrain variation is active - let TV handle mip adjustments
			#else
				mipLevel++;
			#endif
		#endif

		#if defined(TERRAIN_VARIATION) && defined(LANDSCAPE)
			#if !defined(VR)
				mipLevel++; // Increase mip level to match vanilla appearance since terrain variation tends to be sharper than vanilla.
			#else
				// When VR + terrain variation are both active, no additional mip penalty - VR gets only TV features without mip changes
			#endif
		#endif

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
			weights[j] = min(100, pow(weights[j], heightBlend));
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
			heights[0] = ScaleDisplacement(StochasticEffectNoHeight(mipLevels[0], TexLandDisplacement0Sampler, SampTerrainParallaxSampler, coords, sharedOffset).x, params[0]);
#		else
			heights[0] = ScaleDisplacement(TexLandDisplacement0Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[0]).x, params[0]);
#		endif
		}
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile1HasDisplacement) != 0 && w1.y > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[1] = ScaleDisplacement(StochasticEffectNoHeight(mipLevels[1], TexLandDisplacement1Sampler, SampTerrainParallaxSampler, coords, sharedOffset).x, params[1]);
#		else
			heights[1] = ScaleDisplacement(TexLandDisplacement1Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[1]).x, params[1]);
#		endif
		}
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile2HasDisplacement) != 0 && w1.z > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[2] = ScaleDisplacement(StochasticEffectNoHeight(mipLevels[2], TexLandDisplacement2Sampler, SampTerrainParallaxSampler, coords, sharedOffset).x, params[2]);
#		else
			heights[2] = ScaleDisplacement(TexLandDisplacement2Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[2]).x, params[2]);
#		endif
		}
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile3HasDisplacement) != 0 && w1.w > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[3] = ScaleDisplacement(StochasticEffectNoHeight(mipLevels[3], TexLandDisplacement3Sampler, SampTerrainParallaxSampler, coords, sharedOffset).x, params[3]);
#		else
			heights[3] = ScaleDisplacement(TexLandDisplacement3Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[3]).x, params[3]);
#		endif
		}
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile4HasDisplacement) != 0 && w2.x > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[4] = ScaleDisplacement(StochasticEffectNoHeight(mipLevels[4], TexLandDisplacement4Sampler, SampTerrainParallaxSampler, coords, sharedOffset).x, params[4]);
#		else
			heights[4] = ScaleDisplacement(TexLandDisplacement4Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[4]).x, params[4]);
#		endif
		}
		[branch] if ((PBRFlags & PBR::TerrainFlags::LandTile5HasDisplacement) != 0 && w2.y > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[5] = ScaleDisplacement(StochasticEffectNoHeight(mipLevels[5], TexLandDisplacement5Sampler, SampTerrainParallaxSampler, coords, sharedOffset).x, params[5]);
#		else
			heights[5] = ScaleDisplacement(TexLandDisplacement5Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[5]).x, params[5]);
#		endif
		}

		float total;
		ProcessTerrainHeightWeights(heightBlend, w1, w2, heights, weights, total);
#		if defined(TERRAIN_VARIATION)
		// Boost height by 30% when terrain variation is enabled to enhance depth perception
		[branch] if (SharedData::terrainVariationSettings.enableTilingFix) {
			total *= 1.3;
		}
#		endif
		return total;	}
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
				heights[0] = ScaleDisplacement(StochasticEffectNoHeight(mipLevels[0], TexLandTHDisp0Sampler, SampTerrainParallaxSampler, coords, sharedOffset).x, params[0]);
#		else
				heights[0] = ScaleDisplacement(TexLandTHDisp0Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[0]).x, params[0]);
#		endif
			}
			else
			{
#		if defined(TERRAIN_VARIATION)
				heights[0] = ScaleDisplacement(StochasticEffectNoHeight(mipLevels[0], TexColorSampler, SampTerrainParallaxSampler, coords, sharedOffset).w, params[0]);
#		else
				heights[0] = ScaleDisplacement(TexColorSampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[0]).w, params[0]);
#		endif
			}
		}
		if (w1.y > 0.01) {
			[branch] if ((Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::THLand1HasDisplacement) != 0)
			{
#		if defined(TERRAIN_VARIATION)
				heights[1] = ScaleDisplacement(StochasticEffectNoHeight(mipLevels[1], TexLandTHDisp1Sampler, SampTerrainParallaxSampler, coords, sharedOffset).x, params[1]);
#		else
				heights[1] = ScaleDisplacement(TexLandTHDisp1Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[1]).x, params[1]);
#		endif
			}
			else
			{
#		if defined(TERRAIN_VARIATION)
				heights[1] = ScaleDisplacement(StochasticEffectNoHeight(mipLevels[1], TexLandColor2Sampler, SampTerrainParallaxSampler, coords, sharedOffset).w, params[1]);
#		else
				heights[1] = ScaleDisplacement(TexLandColor2Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[1]).w, params[1]);
#		endif
			}
		}
		if (w1.z > 0.01) {
			[branch] if ((Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::THLand2HasDisplacement) != 0)
			{
#		if defined(TERRAIN_VARIATION)
				heights[2] = ScaleDisplacement(StochasticEffectNoHeight(mipLevels[2], TexLandTHDisp2Sampler, SampTerrainParallaxSampler, coords, sharedOffset).x, params[2]);
#		else
				heights[2] = ScaleDisplacement(TexLandTHDisp2Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[2]).x, params[2]);
#		endif
			}
			else
			{
#		if defined(TERRAIN_VARIATION)
				heights[2] = ScaleDisplacement(StochasticEffectNoHeight(mipLevels[2], TexLandColor3Sampler, SampTerrainParallaxSampler, coords, sharedOffset).w, params[2]);
#		else
				heights[2] = ScaleDisplacement(TexLandColor3Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[2]).w, params[2]);
#		endif
			}
		}
		[branch] if ((Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::THLand3HasDisplacement) != 0 && w1.w > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[3] = ScaleDisplacement(StochasticEffectNoHeight(mipLevels[3], TexLandTHDisp3Sampler, SampTerrainParallaxSampler, coords, sharedOffset).x, params[3]);
#		else
			heights[3] = ScaleDisplacement(TexLandTHDisp3Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[3]).x, params[3]);
#		endif
		}
		else if (w1.w > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[3] = ScaleDisplacement(StochasticEffectNoHeight(mipLevels[3], TexLandColor4Sampler, SampTerrainParallaxSampler, coords, sharedOffset).w, params[3]);
#		else
			heights[3] = ScaleDisplacement(TexLandColor4Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[3]).w, params[3]);
#		endif
		}
		[branch] if ((Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::THLand4HasDisplacement) != 0 && w2.x > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[4] = ScaleDisplacement(StochasticEffectNoHeight(mipLevels[4], TexLandTHDisp4Sampler, SampTerrainParallaxSampler, coords, sharedOffset).x, params[4]);
#		else
			heights[4] = ScaleDisplacement(TexLandTHDisp4Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[4]).x, params[4]);
#		endif
		}
		else if (w2.x > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[4] = ScaleDisplacement(StochasticEffectNoHeight(mipLevels[4], TexLandColor5Sampler, SampTerrainParallaxSampler, coords, sharedOffset).w, params[4]);
#		else
			heights[4] = ScaleDisplacement(TexLandColor5Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[4]).w, params[4]);
#		endif
		}
		[branch] if ((Permutation::ExtraFeatureDescriptor & Permutation::ExtraFeatureFlags::THLand5HasDisplacement) != 0 && w2.y > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[5] = ScaleDisplacement(StochasticEffectNoHeight(mipLevels[5], TexLandTHDisp5Sampler, SampTerrainParallaxSampler, coords, sharedOffset).x, params[5]);
#		else
			heights[5] = ScaleDisplacement(TexLandTHDisp5Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[5]).x, params[5]);
#		endif
		}
		else if (w2.y > 0.01)
		{
#		if defined(TERRAIN_VARIATION)
			heights[5] = ScaleDisplacement(StochasticEffectNoHeight(mipLevels[5], TexLandColor6Sampler, SampTerrainParallaxSampler, coords, sharedOffset).w, params[5]);
#		else
			heights[5] = ScaleDisplacement(TexLandColor6Sampler.SampleLevel(SampTerrainParallaxSampler, coords, mipLevels[5]).w, params[5]);
#		endif
		}

		float total;
		ProcessTerrainHeightWeights(heightBlend, w1, w2, heights, weights, total);
#		if defined(TERRAIN_VARIATION)
		// Boost height by 30% when terrain variation is enabled to enhance depth perception
		[branch] if (SharedData::terrainVariationSettings.enableTilingFix) {
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
		StochasticOffsets sharedOffset, float2 dx, float2 dy,
#	endif
		out float pixelOffset, out float weights[6])
#else
	float2 GetParallaxCoords(float distance, float2 coords, float mipLevel, float3 viewDir, float3x3 tbn, float noise, Texture2D<float4> tex, SamplerState texSampler, uint channel, DisplacementParams params, out float pixelOffset)
#endif
	{
		float3 viewDirTS = normalize(mul(tbn, viewDir));
#if defined(LANDSCAPE)
		viewDirTS.xy /= viewDirTS.z * 0.7 + 0.3 + params[0].FlattenAmount;  // Fix for objects at extreme viewing angles
#else
		viewDirTS.xy /= viewDirTS.z * 0.7 + 0.3 + params.FlattenAmount;  // Fix for objects at extreme viewing angles
#endif

		float nearBlendToFar = saturate(distance / 2048.0);
#if defined(LANDSCAPE)
#	if defined(TRUE_PBR)
		float blendFactor = SharedData::extendedMaterialSettings.EnableHeightBlending ? sqrt(saturate(1 - nearBlendToFar)) : 0;
		float4 w1 = lerp(input.LandBlendWeights1, smoothstep(0, 1, input.LandBlendWeights1), blendFactor);
		float2 w2 = lerp(input.LandBlendWeights2.xy, smoothstep(0, 1, input.LandBlendWeights2.xy), blendFactor);
		float scale = max(params[0].HeightScale * w1.x, max(params[1].HeightScale * w1.y, max(params[2].HeightScale * w1.z, max(params[3].HeightScale * w1.w, max(params[4].HeightScale * w2.x, params[5].HeightScale * w2.y)))));
		float scalercp = rcp(scale);
		float maxHeight = 0.1 * scale;
#	else
		float blendFactor = SharedData::extendedMaterialSettings.EnableHeightBlending ? sqrt(saturate(1 - nearBlendToFar)) : 0;
		float4 w1 = lerp(input.LandBlendWeights1, smoothstep(0, 1, input.LandBlendWeights1), blendFactor);
		float2 w2 = lerp(input.LandBlendWeights2.xy, smoothstep(0, 1, input.LandBlendWeights2.xy), blendFactor);
		float scale = 1;
		float maxHeight = 0.1 * scale;
#	endif
#else
		float scale = params.HeightScale;
		float maxHeight = 0.1 * scale;
#endif
		float minHeight = maxHeight * 0.5;

#if defined(LANDSCAPE)
		if (nearBlendToFar < 1.0) {
			uint numSteps = uint((max(6, scale * 8) * (1.0 - nearBlendToFar)) + 0.5);
			numSteps = clamp((numSteps + 3) & ~0x03, 4, max(8, scale * 8));
#else
#	if defined(TRUE_PBR)
		if ((PBRFlags & PBR::Flags::InterlayerParallax) != 0 || nearBlendToFar < 1.0)
#	else
		if (nearBlendToFar < 1.0)
#	endif
		{
			float maxSteps = SharedData::InInterior ? 8 : 16;
			uint numSteps = uint((maxSteps * (1.0 - nearBlendToFar)) + 0.5);
			numSteps = clamp((numSteps + 3) & ~0x03, 4, max(6, scale * maxSteps));
#endif
			float stepSize = rcp(numSteps);
			stepSize += (noise * 2.0 - 1.0) * stepSize * stepSize;

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
#		if defined(TERRAIN_VARIATION)
				currHeight.x = GetTerrainHeight(noise, input, currentOffset[0].xy, mipLevels, params, blendFactor, w1, w2, sharedOffset, dx, dy, weights) * scalercp + 0.5;
				currHeight.y = GetTerrainHeight(noise, input, currentOffset[0].zw, mipLevels, params, blendFactor, w1, w2, sharedOffset, dx, dy, weights) * scalercp + 0.5;
				currHeight.z = GetTerrainHeight(noise, input, currentOffset[1].xy, mipLevels, params, blendFactor, w1, w2, sharedOffset, dx, dy, weights) * scalercp + 0.5;
				currHeight.w = GetTerrainHeight(noise, input, currentOffset[1].zw, mipLevels, params, blendFactor, w1, w2, sharedOffset, dx, dy, weights) * scalercp + 0.5;
#		else
				currHeight.x = GetTerrainHeight(noise, input, currentOffset[0].xy, mipLevels, params, blendFactor, w1, w2, weights) * scalercp + 0.5;
				currHeight.y = GetTerrainHeight(noise, input, currentOffset[0].zw, mipLevels, params, blendFactor, w1, w2, weights) * scalercp + 0.5;
				currHeight.z = GetTerrainHeight(noise, input, currentOffset[1].xy, mipLevels, params, blendFactor, w1, w2, weights) * scalercp + 0.5;
				currHeight.w = GetTerrainHeight(noise, input, currentOffset[1].zw, mipLevels, params, blendFactor, w1, w2, weights) * scalercp + 0.5;
#		endif
#	else
#		if defined(TERRAIN_VARIATION)
				currHeight.x = GetTerrainHeight(noise, input, currentOffset[0].xy, mipLevels, params, blendFactor, w1, w2, sharedOffset, dx, dy, weights) + 0.5;
				currHeight.y = GetTerrainHeight(noise, input, currentOffset[0].zw, mipLevels, params, blendFactor, w1, w2, sharedOffset, dx, dy, weights) + 0.5;
				currHeight.z = GetTerrainHeight(noise, input, currentOffset[1].xy, mipLevels, params, blendFactor, w1, w2, sharedOffset, dx, dy, weights) + 0.5;
				currHeight.w = GetTerrainHeight(noise, input, currentOffset[1].zw, mipLevels, params, blendFactor, w1, w2, sharedOffset, dx, dy, weights) + 0.5;
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
			pixelOffset = lerp(parallaxAmount * scale, 0, nearBlendToFar);
			return lerp(viewDirTS.xy * offset + coords.xy, coords, nearBlendToFar);
		}

#if defined(LANDSCAPE)
		weights[0] = input.LandBlendWeights1.x;
		weights[1] = input.LandBlendWeights1.y;
		weights[2] = input.LandBlendWeights1.z;
		weights[3] = input.LandBlendWeights1.w;
		weights[4] = input.LandBlendWeights2.x;
		weights[5] = input.LandBlendWeights2.y;
#endif

		pixelOffset = 0;
		return coords;
	}

	// https://advances.realtimerendering.com/s2006/Tatarchuk-POM.pdf
	// Cheap method of creating shadows using height for a given light source
	float GetParallaxSoftShadowMultiplier(float2 coords, float mipLevel, float3 L, float sh0, Texture2D<float4> tex, SamplerState texSampler, uint channel, float quality, float noise, DisplacementParams params)
	{
		[branch] if (quality > 0.0)
		{
			float2 rayDir = L.xy * 0.1 * params.HeightScale;
			float4 multipliers = rcp((float4(1, 2, 3, 4) + noise));
			float4 sh;
			sh = AdjustDisplacementNormalized(tex.SampleLevel(texSampler, coords + rayDir * multipliers.x, mipLevel)[channel], params);
			if (quality > 0.25)
				sh.y = AdjustDisplacementNormalized(tex.SampleLevel(texSampler, coords + rayDir * multipliers.y, mipLevel)[channel], params);
			if (quality > 0.5)
				sh.z = AdjustDisplacementNormalized(tex.SampleLevel(texSampler, coords + rayDir * multipliers.z, mipLevel)[channel], params);
			if (quality > 0.75)
				sh.w = AdjustDisplacementNormalized(tex.SampleLevel(texSampler, coords + rayDir * multipliers.w, mipLevel)[channel], params);
			return pow(1.0 - saturate(dot(max(0, sh - sh0), 1.0)) * quality, 2.0);
		}
		return 1.0;
	}

#if defined(LANDSCAPE)
#	if defined(TERRAIN_VARIATION)
	float GetParallaxSoftShadowMultiplierTerrain(PS_INPUT input, float2 coords, float mipLevel[6], float3 L, float sh0, float quality, float noise, DisplacementParams params[6], StochasticOffsets sharedOffset, float2 dx, float2 dy)
#	else
	float GetParallaxSoftShadowMultiplierTerrain(PS_INPUT input, float2 coords, float mipLevel[6], float3 L, float sh0, float quality, float noise, DisplacementParams params[6])
#	endif
	{
		if (quality > 0.0) {
			float4 multipliers = rcp((float4(1, 2, 3, 4) + noise));
			float4 sh;
			float heights[6] = { 0, 0, 0, 0, 0, 0 };
			float2 rayDir = L.xy * 0.1;

#	if defined(TRUE_PBR)
			float scale = max(params[0].HeightScale * input.LandBlendWeights1.x, max(params[1].HeightScale * input.LandBlendWeights1.y, max(params[2].HeightScale * input.LandBlendWeights1.z,
																																			max(params[3].HeightScale * input.LandBlendWeights1.w, max(params[4].HeightScale * input.LandBlendWeights2.x, params[5].HeightScale * input.LandBlendWeights2.y)))));
			if (scale < 0.01)
				return 1.0;
			rayDir *= scale;

#		if defined(TERRAIN_VARIATION)
			sh = GetTerrainHeight(noise, input, coords + rayDir * multipliers.x, mipLevel, params, quality, input.LandBlendWeights1, input.LandBlendWeights2.xy, sharedOffset, dx, dy, heights);
			if (quality > 0.25)
				sh.y = GetTerrainHeight(noise, input, coords + rayDir * multipliers.y, mipLevel, params, quality, input.LandBlendWeights1, input.LandBlendWeights2.xy, sharedOffset, dx, dy, heights);
			if (quality > 0.5)
				sh.z = GetTerrainHeight(noise, input, coords + rayDir * multipliers.z, mipLevel, params, quality, input.LandBlendWeights1, input.LandBlendWeights2.xy, sharedOffset, dx, dy, heights);
			if (quality > 0.75)
				sh.w = GetTerrainHeight(noise, input, coords + rayDir * multipliers.w, mipLevel, params, quality, input.LandBlendWeights1, input.LandBlendWeights2.xy, sharedOffset, dx, dy, heights);
#		else
			sh = GetTerrainHeight(noise, input, coords + rayDir * multipliers.x, mipLevel, params, quality, input.LandBlendWeights1, input.LandBlendWeights2.xy, heights);
			if (quality > 0.25)
				sh.y = GetTerrainHeight(noise, input, coords + rayDir * multipliers.y, mipLevel, params, quality, input.LandBlendWeights1, input.LandBlendWeights2.xy, heights);
			if (quality > 0.5)
				sh.z = GetTerrainHeight(noise, input, coords + rayDir * multipliers.z, mipLevel, params, quality, input.LandBlendWeights1, input.LandBlendWeights2.xy, heights);
			if (quality > 0.75)
				sh.w = GetTerrainHeight(noise, input, coords + rayDir * multipliers.w, mipLevel, params, quality, input.LandBlendWeights1, input.LandBlendWeights2.xy, heights);
#		endif
#		if defined(TERRAIN_VARIATION)
			// Enhance shadow contrast for terrain variation to maintain visual quality
			[branch] if (SharedData::terrainVariationSettings.enableTilingFix) {
				float shadowIntensity = saturate(dot(max(0, sh - sh0), 1.0)) * quality;
				shadowIntensity = pow(shadowIntensity, 0.8); // Slight contrast boost
				return pow(1.0 - shadowIntensity, 2.0);
			} else {
				return pow(1.0 - saturate(dot(max(0, sh - sh0), 1.0)) * quality, 2.0);
			}
#		else
			return pow(1.0 - saturate(dot(max(0, sh - sh0) / scale, 1.0)) * quality, 2.0);
#		endif
#	else
#		if defined(TERRAIN_VARIATION)
			sh = GetTerrainHeight(noise, input, coords + rayDir * multipliers.x, mipLevel, params, quality, input.LandBlendWeights1, input.LandBlendWeights2.xy, sharedOffset, dx, dy, heights);
			if (quality > 0.25)
				sh.y = GetTerrainHeight(noise, input, coords + rayDir * multipliers.y, mipLevel, params, quality, input.LandBlendWeights1, input.LandBlendWeights2.xy, sharedOffset, dx, dy, heights);
			if (quality > 0.5)
				sh.z = GetTerrainHeight(noise, input, coords + rayDir * multipliers.z, mipLevel, params, quality, input.LandBlendWeights1, input.LandBlendWeights2.xy, sharedOffset, dx, dy, heights);
			if (quality > 0.75)
				sh.w = GetTerrainHeight(noise, input, coords + rayDir * multipliers.w, mipLevel, params, quality, input.LandBlendWeights1, input.LandBlendWeights2.xy, sharedOffset, dx, dy, heights);
#		else
			sh = GetTerrainHeight(noise, input, coords + rayDir * multipliers.x, mipLevel, params, quality, input.LandBlendWeights1, input.LandBlendWeights2.xy, heights);
			if (quality > 0.25)
				sh.y = GetTerrainHeight(noise, input, coords + rayDir * multipliers.y, mipLevel, params, quality, input.LandBlendWeights1, input.LandBlendWeights2.xy, heights);
			if (quality > 0.5)
				sh.z = GetTerrainHeight(noise, input, coords + rayDir * multipliers.z, mipLevel, params, quality, input.LandBlendWeights1, input.LandBlendWeights2.xy, heights);
			if (quality > 0.75)
				sh.w = GetTerrainHeight(noise, input, coords + rayDir * multipliers.w, mipLevel, params, quality, input.LandBlendWeights1, input.LandBlendWeights2.xy, heights);
#		endif
#		if defined(TERRAIN_VARIATION)
			// Enhance shadow contrast for terrain variation to maintain visual quality
			[branch] if (SharedData::terrainVariationSettings.enableTilingFix) {
				float shadowIntensity = saturate(dot(max(0, sh - sh0), 1.0)) * quality;
				shadowIntensity = pow(shadowIntensity, 0.8); // Slight contrast boost
				return pow(1.0 - shadowIntensity, 2.0);
			} else {
				return pow(1.0 - saturate(dot(max(0, sh - sh0), 1.0)) * quality, 2.0);
			}
#		else
			return pow(1.0 - saturate(dot(max(0, sh - sh0), 1.0)) * quality, 2.0);
#		endif
#	endif
		}
		return 1.0;
	}

#endif  // defined(LANDSCAPE) && defined(TERRAIN_VARIATION)
	}