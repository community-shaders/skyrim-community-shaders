// Landscape layer helpers and six-way blend macros for Lighting.hlsl.
// PBR tile bits must match PBR::TerrainFlags in PBRMath.hlsli.
// Texture registers cannot be indexed by loop variable; hence one macro expansion per layer.

#ifndef __LIGHTING_LANDSCAPE_HLSLI__
#define __LIGHTING_LANDSCAPE_HLSLI__

#if defined(LANDSCAPE)

#	if !defined(__PERMUTATION_DEPENDENCY_HLSL__)
#		include "Common/Permutation.hlsli"
#	endif

#	if defined(TRUE_PBR)
namespace LandscapeLayers
{
	inline bool PbrTileUsesFullPBR(uint tileIndex)
	{
		return (PBRFlags & (1u << tileIndex)) != 0;
	}
	inline bool PbrTileHasGlint(uint tileIndex)
	{
		return (PBRFlags & (1u << (tileIndex + 12u))) != 0;
	}
}
#	endif

#	if defined(TRUE_PBR)
#		define LIGHTING_LANDSCAPE_BLEND_ONE_LAYER_PBR(TILE, COLOR_TEX, COLOR_SAMP, NORM_TEX, NORM_SAMP, RMAOS_TEX, RMAOS_SAMP, PBR_PARAMS3, GLINT_PARAMS, WEIGHT) \
			[branch] if ((WEIGHT) > 0.01)                                                                                                                          \
			{                                                                                                                                                      \
				float weight = WEIGHT;                                                                                                                             \
				float4 landColor = SampleTerrain(COLOR_TEX, COLOR_SAMP, uv, sharedOffset);                                                                          \
				float3 landColorRGB = landColor.rgb;                                                                                                               \
				[branch] if (!LandscapeLayers::PbrTileUsesFullPBR(TILE))                                                                                           \
				{                                                                                                                                                  \
					landColorRGB = Color::SrgbToLinear(landColorRGB / Color::PBRLightingScale);                                                                    \
				}                                                                                                                                                  \
				float landAlpha = landColor.a;                                                                                                                     \
				float4 landNormal = SampleTerrain(NORM_TEX, NORM_SAMP, uv, sharedOffset);                                                                           \
				float3 landNormalRGB = landNormal.rgb;                                                                                                             \
				float landNormalAlpha = landNormal.a;                                                                                                              \
				float4 landRMAOS;                                                                                                                                  \
				[branch] if (LandscapeLayers::PbrTileUsesFullPBR(TILE))                                                                                            \
				{                                                                                                                                                  \
					landRMAOS = SampleTerrain(RMAOS_TEX, RMAOS_SAMP, uv, sharedOffset) * float4((PBR_PARAMS3).x, 1, 1, (PBR_PARAMS3).z);                           \
					[branch] if (LandscapeLayers::PbrTileHasGlint(TILE))                                                                                           \
					{                                                                                                                                              \
						glintParameters += weight * (GLINT_PARAMS);                                                                                                \
					}                                                                                                                                              \
				}                                                                                                                                                  \
				else                                                                                                                                               \
				{                                                                                                                                                  \
					landRMAOS = float4(1 - glossiness.x, 0, 1, 0);                                                                                                 \
				}                                                                                                                                                  \
				blendedRMAOS += landRMAOS * weight;                                                                                                                \
				blendedRGB += landColorRGB * weight;                                                                                                               \
				blendedAlpha += landAlpha * weight;                                                                                                                \
				blendedNormalRGB += landNormalRGB * weight;                                                                                                        \
				blendedNormalAlpha += landNormalAlpha * weight;                                                                                                    \
			}
#	else
#		if defined(SNOW)
#			define LIGHTING_LAND_SNOW_ACCUM(SNOW_COMPONENT) \
				landSnowMask += (SNOW_COMPONENT) * weight * GetLandSnowMaskValue(landColor.w);
#		else
#			define LIGHTING_LAND_SNOW_ACCUM(SNOW_COMPONENT)
#		endif
#		define LIGHTING_LANDSCAPE_BLEND_ONE_LAYER(TILE, COLOR_TEX, COLOR_SAMP, NORM_TEX, NORM_SAMP, WEIGHT, SNOW_COMPONENT) \
			[branch] if ((WEIGHT) > 0.01)                                                                              \
			{                                                                                                          \
				float weight = WEIGHT;                                                                                 \
				float4 landColor = SampleTerrain(COLOR_TEX, COLOR_SAMP, uv, sharedOffset);                             \
				float3 landColorRGB = landColor.rgb;                                                                   \
				float landAlpha = landColor.a;                                                                         \
				float4 landNormal = SampleTerrain(NORM_TEX, NORM_SAMP, uv, sharedOffset);                              \
				float3 landNormalRGB = landNormal.rgb;                                                                 \
				float landNormalAlpha = landNormal.a;                                                                  \
				blendedRGB += landColorRGB * weight;                                                                   \
				blendedAlpha += landAlpha * weight;                                                                    \
				blendedNormalRGB += landNormalRGB * weight;                                                            \
				blendedNormalAlpha += landNormalAlpha * weight;                                                        \
				LIGHTING_LAND_SNOW_ACCUM(SNOW_COMPONENT)                                                               \
			}
#	endif

#endif  // LANDSCAPE

#endif  // __LIGHTING_LANDSCAPE_HLSLI__
