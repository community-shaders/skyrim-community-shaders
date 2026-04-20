#pragma once

#include "TruePBR.h"

class BSLightingShaderMaterialPBRLandscape : public RE::BSLightingShaderMaterialBase
{
public:
	inline static constexpr auto FEATURE = static_cast<RE::BSShaderMaterial::Feature>(33);

	inline static constexpr auto BaseColorTexture = static_cast<RE::BSTextureSet::Texture>(0);
	inline static constexpr auto NormalTexture = static_cast<RE::BSTextureSet::Texture>(1);
	inline static constexpr auto DisplacementTexture = static_cast<RE::BSTextureSet::Texture>(3);
	inline static constexpr auto RmaosTexture = static_cast<RE::BSTextureSet::Texture>(5);

	inline static constexpr uint32_t NumTiles = 6;

	BSLightingShaderMaterialPBRLandscape();
	~BSLightingShaderMaterialPBRLandscape();

	// override (BSLightingShaderMaterialBase)
	// Called by BSShaderMaterialHashMap::Link to produce the heap-allocated canonical copy.
	// MUST use regular heap (new), NOT Make()/scrap heap. BSLightingShaderProperty::LinkObject
	// calls ScrapHeap::Free() immediately after Link — a scrap-heap canonical would be popped
	// off the stack and freed while property->material still points to it (use-after-free).
	RE::BSShaderMaterial* Create() override;                                                                                      // 01
	void CopyMembers(RE::BSShaderMaterial* that) override;                                                                        // 02
	Feature GetFeature() const override;                                                                                          // 06
	void ClearTextures() override;                                                                                                // 09
	void ReceiveValuesFromRootMaterial(bool skinned, bool rimLighting, bool softLighting, bool backLighting, bool MSN) override;  // 0A
	uint32_t GetTextures(RE::NiSourceTexture** textures) override;                                                                // 0B

	// Allocates a scrap-heap temp for use during BSLightingShaderProperty::LoadBinary.
	// The temp is direct-assigned to property->material so that BSLightingShaderProperty::LinkObject
	// (NiStream link phase) can find it, call BSShaderMaterialHashMap::Link to produce the canonical,
	// then ScrapHeap::Free() to pop this temp. Never use Make() as the Create() implementation.
	static BSLightingShaderMaterialPBRLandscape* Make();

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
