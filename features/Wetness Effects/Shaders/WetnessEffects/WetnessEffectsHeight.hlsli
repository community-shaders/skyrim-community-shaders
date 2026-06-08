#ifndef WETNESS_EFFECTS_HEIGHT_HLSLI
#define WETNESS_EFFECTS_HEIGHT_HLSLI

// Height-map cavity pooling, enhanced puddle shading, and raindrop placement.
// Requires EMAT (Extended Materials) for displacement sampling.

#if defined(EMAT)

namespace WetnessEffects
{
	float GetHeightAwareDistanceFade(float viewDistanceZ)
	{
		if (!SharedData::wetnessEffectsSettings.EnableHeightAwarePuddles)
			return 0.0;
		return saturate(1.0 - viewDistanceZ * rcp(ExtendedMaterials::ParallaxCheapDistance));
	}

	float SampleMeshDisplacementHeight(float2 coords, float mipLevel, DisplacementParams params, Texture2D<float4> tex, SamplerState texSampler, uint channel)
	{
		return ExtendedMaterials::AdjustDisplacementNormalized(tex.SampleLevel(texSampler, coords, mipLevel)[channel], params);
	}

	float GetMeshHeightCavity(float2 coords, float mipLevel, DisplacementParams params, Texture2D<float4> tex, SamplerState texSampler, uint channel, float sensitivity)
	{
		float2 uvDx = ddx(coords);
		float2 uvDy = ddy(coords);
		float h0 = SampleMeshDisplacementHeight(coords, mipLevel, params, tex, texSampler, channel);
		float h1 = SampleMeshDisplacementHeight(coords + uvDx, mipLevel, params, tex, texSampler, channel);
		float h2 = SampleMeshDisplacementHeight(coords + uvDy, mipLevel, params, tex, texSampler, channel);
		float h3 = SampleMeshDisplacementHeight(coords - uvDx, mipLevel, params, tex, texSampler, channel);
		float h4 = SampleMeshDisplacementHeight(coords - uvDy, mipLevel, params, tex, texSampler, channel);
		float hMax = max(h0, max(max(h1, h2), max(h3, h4)));
		return saturate((hMax - h0) * sensitivity);
	}

#	if defined(LANDSCAPE)
	float SampleTerrainDisplacementHeight(float noise, PS_INPUT input, float2 coords, float mipLevels[6], DisplacementParams params[6], StochasticOffsets sharedOffset)
	{
		float weights[6] = { 0, 0, 0, 0, 0, 0 };
		float blendFactor = SharedData::extendedMaterialSettings.EnableHeightBlending ? 1.0 : 0.0;
		return ExtendedMaterials::GetTerrainHeight(noise, input, coords, mipLevels, params, blendFactor, input.LandBlendWeights1, input.LandBlendWeights2.xy, sharedOffset, weights);
	}

	float GetTerrainHeightCavity(float noise, PS_INPUT input, float2 coords, float mipLevels[6], DisplacementParams params[6], StochasticOffsets sharedOffset, float sensitivity)
	{
		float2 uvDx = ddx(coords);
		float2 uvDy = ddy(coords);
		float h0 = SampleTerrainDisplacementHeight(noise, input, coords, mipLevels, params, sharedOffset);
		float h1 = SampleTerrainDisplacementHeight(noise, input, coords + uvDx, mipLevels, params, sharedOffset);
		float h2 = SampleTerrainDisplacementHeight(noise, input, coords + uvDy, mipLevels, params, sharedOffset);
		float h3 = SampleTerrainDisplacementHeight(noise, input, coords - uvDx, mipLevels, params, sharedOffset);
		float h4 = SampleTerrainDisplacementHeight(noise, input, coords - uvDy, mipLevels, params, sharedOffset);
		float hMax = max(h0, max(max(h1, h2), max(h3, h4)));
		return saturate((hMax - h0) * sensitivity);
	}

	float3 GetTerrainHeightGradientNormal(float noise, PS_INPUT input, float2 coords, float mipLevels[6], DisplacementParams params[6], StochasticOffsets sharedOffset, float3x3 tbn)
	{
		float2 uvDx = ddx(coords);
		float2 uvDy = ddy(coords);
		float hCenter = SampleTerrainDisplacementHeight(noise, input, coords, mipLevels, params, sharedOffset);
		float hX = SampleTerrainDisplacementHeight(noise, input, coords + uvDx, mipLevels, params, sharedOffset) -
		           SampleTerrainDisplacementHeight(noise, input, coords - uvDx, mipLevels, params, sharedOffset);
		float hY = SampleTerrainDisplacementHeight(noise, input, coords + uvDy, mipLevels, params, sharedOffset) -
		           SampleTerrainDisplacementHeight(noise, input, coords - uvDy, mipLevels, params, sharedOffset);
		float3 gradTS = normalize(float3(-hX, -hY, max(length(float2(hX, hY)), 1e-3)));
		return normalize(mul(tbn, gradTS));
	}
#	endif

	float3 GetMeshHeightGradientNormal(float2 coords, float mipLevel, DisplacementParams params, Texture2D<float4> tex, SamplerState texSampler, uint channel, float3x3 tbn)
	{
		float2 uvDx = ddx(coords);
		float2 uvDy = ddy(coords);
		float hX = SampleMeshDisplacementHeight(coords + uvDx, mipLevel, params, tex, texSampler, channel) -
		           SampleMeshDisplacementHeight(coords - uvDx, mipLevel, params, tex, texSampler, channel);
		float hY = SampleMeshDisplacementHeight(coords + uvDy, mipLevel, params, tex, texSampler, channel) -
		           SampleMeshDisplacementHeight(coords - uvDy, mipLevel, params, tex, texSampler, channel);
		float3 gradTS = normalize(float3(-hX, -hY, max(length(float2(hX, hY)), 1e-3)));
		return normalize(mul(tbn, gradTS));
	}

	// Returns 0 in peaks, 1 in depressions. When height-aware is disabled or no displacement, returns 0.5 (neutral).
	float GetHeightPoolMask(float rawCavity, float distanceFade)
	{
		float cavity = saturate(rawCavity * SharedData::wetnessEffectsSettings.PuddleHeightSensitivity);
		return lerp(0.5, cavity, distanceFade);
	}

	float ApplyHeightPoolMaskToPuddle(float puddle, float poolMask, float distanceFade)
	{
		// poolMask: 0.5 neutral, >0.5 depressions, <0.5 peaks
		float cavityFactor = lerp(0.35, 1.35, saturate((poolMask - 0.15) * 1.4));
		return puddle * lerp(1.0, cavityFactor, distanceFade);
	}

	float GetRaindropHeightMask(float poolMask, float flatnessAmount)
	{
		float depression = saturate((poolMask - 0.35) * 2.0);
		float peakCut = saturate((0.65 - poolMask) * 2.0);
		return saturate(flatnessAmount * lerp(0.25, 1.0, depression) * lerp(1.0, 0.2, peakCut));
	}

	void ApplyHeightGradientPuddleNormal(
		float wetnessPuddleDepth,
		float distanceFade,
		float3 heightGradientNormal,
		inout float3 wetnessNormal)
	{
		if (wetnessPuddleDepth < 0.05 || distanceFade < 0.01)
			return;
		wetnessNormal = normalize(lerp(wetnessNormal, heightGradientNormal, wetnessPuddleDepth * distanceFade * 0.65));
	}
}

#endif  // EMAT

#endif  // WETNESS_EFFECTS_HEIGHT_HLSLI
