#ifndef __LIGHTING_LANDSCAPE_HLSLI__
#define __LIGHTING_LANDSCAPE_HLSLI__

// Shared terrain layer indexing for TRUE_PBR (PBRFlags terrain bits) and non-PBR displacement
// (Permutation::ExtraFeatureDescriptor TH land bits). HLSL cannot index texture registers by loop
// variable; use LANDSCAPE_*_LAYER_FOREACH (X-macros) for per-layer texture bodies.
//
// PBR terrain bit layout must stay aligned with PBR::TerrainFlags in PBRMath.hlsli.

#if defined(LANDSCAPE)

#	if !defined(__PERMUTATION_DEPENDENCY_HLSL__)
#		include "Common/Permutation.hlsli"
#	endif

namespace LandscapeLayers
{
#	if defined(TRUE_PBR)
	// Tiles 0..5: PBR, displacement, glint packed contiguously (see PBRMath.hlsli).
	inline bool PbrTileUsesFullPBR(uint tileIndex)
	{
		return (PBRFlags & (1u << tileIndex)) != 0;
	}
	inline bool PbrTileHasDisplacement(uint tileIndex)
	{
		return (PBRFlags & (1u << (tileIndex + 6u))) != 0;
	}
	inline bool PbrTileHasGlint(uint tileIndex)
	{
		return (PBRFlags & (1u << (tileIndex + 12u))) != 0;
	}
#	else
	// Matches Permutation::ExtraFeatureFlags::THLand0HasDisplacement .. THLand5HasDisplacement
	inline bool ThTileHasDisplacement(uint tileIndex)
	{
		return (Permutation::ExtraFeatureDescriptor & (1u << tileIndex)) != 0;
	}
#	endif
}

// tileIndex, displacementTexture, diffuseOrAlphaHeightTexture (mip dims / non-PBR height)
#	if defined(TRUE_PBR)
#		define LANDSCAPE_PBR_LAYER_FOREACH(X)                      \
			X(0, TexLandDisplacement0Sampler, TexColorSampler)      \
			X(1, TexLandDisplacement1Sampler, TexLandColor2Sampler) \
			X(2, TexLandDisplacement2Sampler, TexLandColor3Sampler) \
			X(3, TexLandDisplacement3Sampler, TexLandColor4Sampler) \
			X(4, TexLandDisplacement4Sampler, TexLandColor5Sampler) \
			X(5, TexLandDisplacement5Sampler, TexLandColor6Sampler)
#	else
#		define LANDSCAPE_TH_LAYER_FOREACH(X)                 \
			X(0, TexLandTHDisp0Sampler, TexColorSampler)      \
			X(1, TexLandTHDisp1Sampler, TexLandColor2Sampler) \
			X(2, TexLandTHDisp2Sampler, TexLandColor3Sampler) \
			X(3, TexLandTHDisp3Sampler, TexLandColor4Sampler) \
			X(4, TexLandTHDisp4Sampler, TexLandColor5Sampler) \
			X(5, TexLandTHDisp5Sampler, TexLandColor6Sampler)
#	endif

// ---------------------------------------------------------------------------
// Lighting.hlsl: six-way landscape diffuse / normal / RMAOS blend.
// Requires: SampleTerrain, input, uv, sharedOffset, landDistanceTexMipBias, glossiness, blendedRGB, blendedAlpha,
// blendedNormalRGB, blendedNormalAlpha, glintParameters, Color::*, GetLandSnowMaskValue (non-PBR path).
// ---------------------------------------------------------------------------
#	if defined(TRUE_PBR)
#		define LIGHTING_LANDSCAPE_BLEND_ONE_LAYER_PBR(TILE, COLOR_TEX, COLOR_SAMP, NORM_TEX, NORM_SAMP, RMAOS_TEX, RMAOS_SAMP, PBR_PARAMS3, GLINT_PARAMS, WEIGHT)   \
			if (WEIGHT > 0.01) {                                                                                                                                 \
				float weight = WEIGHT;                                                                                                                           \
				float4 landColor = SampleTerrain(COLOR_TEX, COLOR_SAMP, uv, sharedOffset, landDistanceTexMipBias);                                               \
				float3 landColorRGB = landColor.rgb;                                                                                                             \
				[branch] if (!LandscapeLayers::PbrTileUsesFullPBR(TILE))                                                                                         \
				{                                                                                                                                                \
					landColorRGB = Color::SrgbToLinear(landColorRGB / Color::PBRLightingScale);                                                                  \
				}                                                                                                                                                \
				float landAlpha = landColor.a;                                                                                                                   \
				float4 landNormal = SampleTerrain(NORM_TEX, NORM_SAMP, uv, sharedOffset, landDistanceTexMipBias);                                                \
				float3 landNormalRGB = landNormal.rgb;                                                                                                           \
				float landNormalAlpha = landNormal.a;                                                                                                            \
				float4 landRMAOS;                                                                                                                                \
				[branch] if (LandscapeLayers::PbrTileUsesFullPBR(TILE))                                                                                          \
				{                                                                                                                                                \
					landRMAOS = SampleTerrain(RMAOS_TEX, RMAOS_SAMP, uv, sharedOffset, landDistanceTexMipBias) * float4((PBR_PARAMS3).x, 1, 1, (PBR_PARAMS3).z); \
					if (LandscapeLayers::PbrTileHasGlint(TILE)) {                                                                                                \
						glintParameters += weight * (GLINT_PARAMS);                                                                                              \
					}                                                                                                                                            \
				}                                                                                                                                                \
				else                                                                                                                                             \
				{                                                                                                                                                \
					landRMAOS = weight * float4(1 - glossiness.x, 0, 1, 0);                                                                                      \
				}                                                                                                                                                \
				blendedRMAOS += landRMAOS * weight;                                                                                                              \
				blendedRGB += landColorRGB * weight;                                                                                                             \
				blendedAlpha += landAlpha * weight;                                                                                                              \
				blendedNormalRGB += landNormalRGB * weight;                                                                                                      \
				blendedNormalAlpha += landNormalAlpha * weight;                                                                                                  \
			}
#	else
#		if defined(SNOW)
#			define LIGHTING_LAND_SNOW_ACCUM(SNOW_COMPONENT) \
				landSnowMask += (SNOW_COMPONENT) * weight * GetLandSnowMaskValue(landColor.w);
#		else
#			define LIGHTING_LAND_SNOW_ACCUM(SNOW_COMPONENT)
#		endif
#		define LIGHTING_LANDSCAPE_BLEND_ONE_LAYER(COLOR_TEX, COLOR_SAMP, NORM_TEX, NORM_SAMP, WEIGHT, SNOW_COMPONENT) \
			if (WEIGHT > 0.01) {                                                                                              \
				float weight = WEIGHT;                                                                                        \
				float4 landColor = SampleTerrain(COLOR_TEX, COLOR_SAMP, uv, sharedOffset, landDistanceTexMipBias);            \
				float3 landColorRGB = landColor.rgb;                                                                          \
				float landAlpha = landColor.a;                                                                                \
				float4 landNormal = SampleTerrain(NORM_TEX, NORM_SAMP, uv, sharedOffset, landDistanceTexMipBias);             \
				float3 landNormalRGB = landNormal.rgb;                                                                        \
				float landNormalAlpha = landNormal.a;                                                                         \
				blendedRGB += landColorRGB * weight;                                                                          \
				blendedAlpha += landAlpha * weight;                                                                           \
				blendedNormalRGB += landNormalRGB * weight;                                                                   \
				blendedNormalAlpha += landNormalAlpha * weight;                                                               \
				LIGHTING_LAND_SNOW_ACCUM(SNOW_COMPONENT)                                                               \
			}
#	endif

#endif  // LANDSCAPE

#endif  // __LANDSCAPE_LAYERS_HLSLI__
