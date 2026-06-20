#pragma once

#include "TruePBR.h"

/**
 * @brief PBR material class for landscape (terrain) meshes.
 *
 * Extends BSLightingShaderMaterialBase to support up to 6 tiled PBR texture layers
 * on terrain, each with independent base color, normal, displacement, and RMAOS
 * textures. Field offsets for terrain overlay/noise textures, blend params, and
 * tex-offset/fade are kept in sync with vanilla BSLightingShaderMaterialLandscape
 * via static_assert checks.
 */
class BSLightingShaderMaterialPBRLandscape : public RE::BSLightingShaderMaterialBase
{
public:
	inline static constexpr auto FEATURE = static_cast<RE::BSShaderMaterial::Feature>(33);

	inline static constexpr auto BaseColorTexture = static_cast<RE::BSTextureSet::Texture>(0);
	inline static constexpr auto NormalTexture = static_cast<RE::BSTextureSet::Texture>(1);
	inline static constexpr auto DisplacementTexture = static_cast<RE::BSTextureSet::Texture>(3);
	inline static constexpr auto RmaosTexture = static_cast<RE::BSTextureSet::Texture>(5);

	inline static constexpr uint32_t NumTiles = 6;

	/** @brief Initializes per-tile arrays with default PBR values (roughness 1.0, displacement 1.0, specular 0.04). */
	BSLightingShaderMaterialPBRLandscape();
	/** @brief Destructor that removes this material from the global tracking map. */
	~BSLightingShaderMaterialPBRLandscape();

	// override (BSLightingShaderMaterialBase)
	/**
	 * @brief Creates a heap-allocated canonical copy for the shader material hash map.
	 *
	 * MUST use regular heap (new), NOT Make()/scrap heap. BSLightingShaderProperty::LinkObject
	 * calls ScrapHeap::Free() immediately after Link -- a scrap-heap canonical would be popped
	 * off the stack and freed while property->material still points to it (use-after-free).
	 *
	 * @return A new heap-allocated BSLightingShaderMaterialPBRLandscape instance.
	 */
	RE::BSShaderMaterial* Create() override;                                                                                      // 01
	/**
	 * @brief Copies all landscape PBR members (textures, per-tile parameters, terrain data) to the target material.
	 * @param that Target material to copy into (must be BSLightingShaderMaterialPBRLandscape).
	 */
	void CopyMembers(RE::BSShaderMaterial* that) override;                                                                        // 02
	/**
	 * @brief Returns the material feature type.
	 * @return Always returns kMultiTexLandLODBlend to integrate with the vanilla landscape shader dispatch.
	 */
	Feature GetFeature() const override;                                                                                          // 06
	/** @brief Releases all landscape texture references (base color, normal, displacement, RMAOS, overlay, noise). */
	void ClearTextures() override;                                                                                                // 09
	/**
	 * @brief Assigns default textures to any landscape texture slots that are still null.
	 * @param skinned Whether the mesh is skinned.
	 * @param rimLighting Whether rim lighting is enabled.
	 * @param softLighting Whether soft lighting is enabled.
	 * @param backLighting Whether back lighting is enabled.
	 * @param MSN Whether model-space normals are used.
	 */
	void ReceiveValuesFromRootMaterial(bool skinned, bool rimLighting, bool softLighting, bool backLighting, bool MSN) override;  // 0A
	/**
	 * @brief Writes all non-null landscape textures into the output array.
	 * @param textures Output array to fill with texture pointers.
	 * @return The number of textures written.
	 */
	uint32_t GetTextures(RE::NiSourceTexture** textures) override;                                                                // 0B

	/**
	 * @brief Allocates a scrap-heap temporary for use during BSLightingShaderProperty::LoadBinary.
	 *
	 * The temp is direct-assigned to property->material so that BSLightingShaderProperty::LinkObject
	 * (NiStream link phase) can find it, call BSShaderMaterialHashMap::Link to produce the canonical,
	 * then ScrapHeap::Free() to pop this temp. Never use Make() as the Create() implementation.
	 *
	 * @return A scrap-heap-allocated PBR landscape material, or nullptr on allocation failure.
	 */
	static BSLightingShaderMaterialPBRLandscape* Make();

	/**
	 * @brief Checks whether any active landscape tile has glint rendering enabled.
	 * @return True if at least one tile's glint parameters are enabled.
	 */
	bool HasGlint() const;

	inline static std::unordered_map<BSLightingShaderMaterialPBRLandscape*, std::array<TruePBR::PBRTextureSetData*, NumTiles>> All;

	// members
	std::uint32_t numLandscapeTextures = 0;

	// We need terrainOverlayTexture, terrainNoiseTexture, landBlendParams, terrainTexOffsetX, terrainTexOffsetY, terrainTexFade
	// to be at the same offsets as in vanilla BSLightingShaderMaterialLandscape so we arrange PBR fields in specific way
	std::array<RE::NiPointer<RE::NiSourceTexture>, NumTiles> landscapeBaseColorTextures;
	std::array<bool, NumTiles> isPbr;
	std::array<float, NumTiles> roughnessScales;

	RE::NiPointer<RE::NiSourceTexture> terrainOverlayTexture;
	RE::NiPointer<RE::NiSourceTexture> terrainNoiseTexture;
	RE::NiColorA landBlendParams;

	std::array<RE::NiPointer<RE::NiSourceTexture>, NumTiles> landscapeNormalTextures;

	float terrainTexOffsetX = 0.f;
	float terrainTexOffsetY = 0.f;
	float terrainTexFade = 0.f;

	std::array<RE::NiPointer<RE::NiSourceTexture>, NumTiles> landscapeDisplacementTextures;
	std::array<RE::NiPointer<RE::NiSourceTexture>, NumTiles> landscapeRMAOSTextures;
	std::array<float, NumTiles> displacementScales;
	std::array<float, NumTiles> specularLevels;

	std::array<GlintParameters, NumTiles> glintParameters;
};
static_assert(offsetof(BSLightingShaderMaterialPBRLandscape, terrainOverlayTexture) == offsetof(RE::BSLightingShaderMaterialLandscape, terrainOverlayTexture));
static_assert(offsetof(BSLightingShaderMaterialPBRLandscape, terrainNoiseTexture) == offsetof(RE::BSLightingShaderMaterialLandscape, terrainNoiseTexture));
static_assert(offsetof(BSLightingShaderMaterialPBRLandscape, landBlendParams) == offsetof(RE::BSLightingShaderMaterialLandscape, landBlendParams));
static_assert(offsetof(BSLightingShaderMaterialPBRLandscape, terrainTexOffsetX) == offsetof(RE::BSLightingShaderMaterialLandscape, terrainTexOffsetX));
static_assert(offsetof(BSLightingShaderMaterialPBRLandscape, terrainTexOffsetY) == offsetof(RE::BSLightingShaderMaterialLandscape, terrainTexOffsetY));
static_assert(offsetof(BSLightingShaderMaterialPBRLandscape, terrainTexFade) == offsetof(RE::BSLightingShaderMaterialLandscape, terrainTexFade));
