// https://github.com/tgjones/slimshader-cpp/blob/master/src/Shaders/Sdk/Direct3D11/DetailTessellation11/POM.hlsl
// https://github.com/alandtse/SSEShaderTools/blob/main/shaders_vr/ParallaxEffect.h

// https://github.com/marselas/Zombie-Direct3D-Samples/blob/5f53dc2d6f7deb32eb2e5e438d6b6644430fe9ee/Direct3D/ParallaxOcclusionMapping/ParallaxOcclusionMapping.fx
// http://www.diva-portal.org/smash/get/diva2:831762/FULLTEXT01.pdf
// https://bartwronski.files.wordpress.com/2014/03/ac4_gdc.pdf

#ifndef EXTENDED_MATERIALS_HLSLI
#define EXTENDED_MATERIALS_HLSLI

#if defined(TERRAIN_VARIATION)
#	include "TerrainVariation/TerrainVariation.hlsli"
#else
struct StochasticOffsets
{
	float2 offset1;
	float2 offset2;
	float tap1Weight;
};
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
	static const float ParallaxCheapDistance = 1024.0;
	static const float ParallaxNearShadowQuality = 1.0;
	static const float ParallaxFarShadowQuality = 0.5;
	static const float TerrainParallaxShadowMaxMipLevel = 2.0;

	// Coarse height-map mip for POM march. Distance bias is applied and floored.
	// Grazing bias is omitted — it creates discontinuous hits.
	inline float ComputeParallaxMarchMip(float baseMip, float viewDist)
	{
		float m = baseMip + 1.0;
		m += min(2.0, baseMip * 0.5);
		float farFloor = viewDist > 0.0 ? lerp(0.0, 3.0, saturate((viewDist - 512.0) * rcp(1536.0))) : saturate(baseMip - 1.0);
		m = max(m, farFloor);
		return floor(m);
	}

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

	float GetMipLevelFromDims(float2 coords, float2 textureDims)
	{
#if !defined(PARALLAX) && !defined(TRUE_PBR)
		textureDims /= 2.0;
#endif

		float2 texCoordsPerSize = coords * textureDims;

		// Compute the current gradients:
		float2 dxSize = ddx(texCoordsPerSize);
		float2 dySize = ddy(texCoordsPerSize);

		// Standard mipmapping uses max here
		float minTexCoordDelta = min(dot(dxSize, dxSize), dot(dySize, dySize));

		// Compute the current mip level  (* 0.5 is effectively computing a square root before )
		float mipLevel = max(0.5 * log2(minTexCoordDelta), 0);

#if !defined(PARALLAX) && !defined(TRUE_PBR)
		mipLevel++;
#endif

		return floor(max(mipLevel + SharedData::MipBias, 0));
	}

	float GetMipLevel(float2 coords, Texture2D<float4> tex)
	{
		float2 textureDims;
		tex.GetDimensions(textureDims.x, textureDims.y);
		return GetMipLevelFromDims(coords, textureDims);
	}

	// Parallax warping fix, objects.
	float GetObjectFlattenAmount(float3 vertexNormal, float3 worldPosition, float3 viewDirection)
	{
		float3 ndx = ddx(vertexNormal);
		float3 ndy = ddy(vertexNormal);
		float3 fdx = ddx(worldPosition);
		float3 fdy = ddy(worldPosition);
		float fragSize = rcp(length(max(abs(fdx), abs(fdy))));
		float normalDelta = length(max(abs(ndx), abs(ndy))) * fragSize;
		float3 flatWorldNormal = normalize(-cross(fdx, fdy));
		float normalSmoothness = saturate(1.0 - dot(vertexNormal, flatWorldNormal));
		// Objects: do not crush HeightScale (that flattens convex faces you are looking at).
		// Only add FlattenAmount past the facing hemisphere (grazing > facing) and only if bent.
		// Flat walls stay at bend ≈ 0. Flatten tracks UV stretch (÷ facing).
		float curvature = normalDelta;
		float facing = saturate(dot(vertexNormal, viewDirection));
		float grazing = 1.0 - facing;
		float bend = saturate(normalSmoothness + curvature);
		float silhouette = saturate(grazing - facing);
		// Cap FlattenAmount: ÷facing tracks UV stretch but can explode at grazing (~14× at the 0.0625 floor).
		return saturate(silhouette * bend * rcp(max(facing, 0.0625)));
	}

	// The env mask carries height in alpha, judged on its 1x1 mip: grayscale
	// environment masks are rejected, solid-black height-only masks kept.
	bool IsComplexMaterial(float4 mipSample)
	{
		const float kMaskEpsilon = (4.0 / 255.0);

		bool complexMaterial = mipSample.w < (1.0 - kMaskEpsilon);

		const bool grayscaleMask = (abs(mipSample.x - mipSample.y) < kMaskEpsilon) &&
		                           (abs(mipSample.x - mipSample.z) < kMaskEpsilon) &&
		                           (abs(mipSample.y - mipSample.z) < kMaskEpsilon);
		// Preserve height-only masks while rejecting grayscale environment masks
		const bool solidBlackHeightMask = all(mipSample.xyz < kMaskEpsilon) &&
		                                  mipSample.w > kMaskEpsilon &&
		                                  mipSample.w < (1.0 - kMaskEpsilon);
		if (grayscaleMask && !solidBlackHeightMask)
			complexMaterial = false;
		return complexMaterial;
	}

	// A complex-material texel whose alpha is a height, not a cleared end.
	bool HasComplexHeight(float maskAlpha)
	{
		const float kMaskEpsilon = (4.0 / 255.0);
		return maskAlpha > kMaskEpsilon && maskAlpha < (1.0 - kMaskEpsilon);
	}

#if defined(LANDSCAPE)
#	include "ExtendedMaterials/ExtendedMaterialsTerrain.hlsli"
#endif
#include "ExtendedMaterials/ExtendedMaterialsParallaxCore.hlsli"
}

#endif  // EXTENDED_MATERIALS_HLSLI
