#pragma once

#include "TruePBR.h"

enum class PBRFlags : uint32_t
{
	Subsurface = 1 << 0,
	TwoLayer = 1 << 1,
	ColoredCoat = 1 << 2,
	InterlayerParallax = 1 << 3,
	CoatNormal = 1 << 4,
	Fuzz = 1 << 5,
	HairMarschner = 1 << 6,
};

enum class PBRShaderFlags : uint32_t
{
	HasEmissive = 1 << 0,
	HasDisplacement = 1 << 1,
	HasFeaturesTexture0 = 1 << 2,
	HasFeaturesTexture1 = 1 << 3,
	Subsurface = 1 << 4,
	TwoLayer = 1 << 5,
	ColoredCoat = 1 << 6,
	InterlayerParallax = 1 << 7,
	CoatNormal = 1 << 8,
	Fuzz = 1 << 9,
	HairMarschner = 1 << 10,
	Glint = 1 << 11,
	ProjectedGlint = 1 << 12,
};

class BSLightingShaderMaterialPBR : public RE::BSLightingShaderMaterialBase
{
public:
	struct MaterialExtensions
	{
		TruePBR::PBRTextureSetData* textureSetData = nullptr;
		TruePBR::PBRMaterialObjectData* materialObjectData = nullptr;
		/// FormID of the TESObjectREFR whose Clone3D call last wrote MATO data to this
		/// material.  Used by the fork-before-write check to detect when a pooled material
		/// instance would be overwritten by a different ref, triggering a clone instead.
		RE::FormID lastOwnerRefFormID = 0;
	};

	inline static constexpr auto FEATURE = static_cast<RE::BSShaderMaterial::Feature>(32);

	inline static constexpr auto RmaosTexture = static_cast<RE::BSTextureSet::Texture>(5);
	inline static constexpr auto EmissiveTexture = static_cast<RE::BSTextureSet::Texture>(2);
	inline static constexpr auto DisplacementTexture = static_cast<RE::BSTextureSet::Texture>(3);
	inline static constexpr auto FeaturesTexture0 = static_cast<RE::BSTextureSet::Texture>(7);
	inline static constexpr auto FeaturesTexture1 = static_cast<RE::BSTextureSet::Texture>(6);

	~BSLightingShaderMaterialPBR();

	// override (BSLightingShaderMaterialBase)
	// Called by BSShaderMaterialHashMap::Link to produce the heap-allocated canonical copy.
	// MUST use regular heap (new), NOT Make()/scrap heap. BSLightingShaderProperty::LinkObject
	// calls ScrapHeap::Free() immediately after Link — a scrap-heap canonical would be popped
	// off the stack and freed while property->material still points to it (use-after-free).
	RE::BSShaderMaterial* Create() override;                                                                                      // 01
	void CopyMembers(RE::BSShaderMaterial* that) override;                                                                        // 02
	std::uint32_t ComputeCRC32(uint32_t srcHash) override;                                                                        // 04
	Feature GetFeature() const override;                                                                                          // 06
	void OnLoadTextureSet(std::uint64_t arg1, RE::BSTextureSet* inTextureSet) override;                                           // 08
	void ClearTextures() override;                                                                                                // 09
	void ReceiveValuesFromRootMaterial(bool skinned, bool rimLighting, bool softLighting, bool backLighting, bool MSN) override;  // 0A
	uint32_t GetTextures(RE::NiSourceTexture** textures) override;                                                                // 0B
	void LoadBinary(RE::NiStream& stream) override;                                                                               // 0D

	// Allocates a scrap-heap temp for use during BSLightingShaderProperty::LoadBinary.
	// The temp is direct-assigned to property->material so that BSLightingShaderProperty::LinkObject
	// (NiStream link phase) can find it, call BSShaderMaterialHashMap::Link to produce the canonical,
	// then ScrapHeap::Free() to pop this temp. Never use Make() as the Create() implementation.
	static BSLightingShaderMaterialPBR* Make();

	void ApplyTextureSetData(const TruePBR::PBRTextureSetData& textureSetData);
	void ApplyMaterialObjectData(const TruePBR::PBRMaterialObjectData& materialObjectData);
	/// Resets all projected-material fields to their default values.
	/// Called on references that carry no MATO (or no PBR config for their MATO) to
	/// prevent stale data copied in by CopyMembers from persisting on the material.
	void ClearMaterialObjectData();

	float GetRoughnessScale() const;
	float GetSpecularLevel() const;

	float GetDisplacementScale() const;

	const RE::NiColor& GetSubsurfaceColor() const;
	float GetSubsurfaceOpacity() const;

	const RE::NiColor& GetCoatColor() const;
	float GetCoatStrength() const;
	float GetCoatRoughness() const;
	float GetCoatSpecularLevel() const;

	const std::array<float, 3>& GetProjectedMaterialBaseColorScale() const;
	float GetProjectedMaterialRoughness() const;
	float GetProjectedMaterialSpecularLevel() const;
	const GlintParameters& GetProjectedMaterialGlintParameters() const;

	const RE::NiColor& GetFuzzColor() const;
	float GetFuzzWeight() const;

	const GlintParameters& GetGlintParameters() const;

	inline static std::unordered_map<BSLightingShaderMaterialPBR*, MaterialExtensions> All;

	// members
	RE::BSShaderMaterial::Feature loadedWithFeature = RE::BSShaderMaterial::Feature::kDefault;

	stl::enumeration<PBRFlags> pbrFlags;

	float coatRoughness = 1.f;
	float coatSpecularLevel = 0.04f;

	RE::NiColor fuzzColor;
	float fuzzWeight = 0.f;

	GlintParameters glintParameters;

	// Roughness in r, metallic in g, AO in b, nonmetal reflectance in a
	RE::NiPointer<RE::NiSourceTexture> rmaosTexture;

	// Emission color in rgb
	RE::NiPointer<RE::NiSourceTexture> emissiveTexture;

	// Displacement in r
	RE::NiPointer<RE::NiSourceTexture> displacementTexture;

	// Subsurface map (subsurface color in rgb, thickness in a) / Coat map (coat color in rgb, coat strength in a)
	RE::NiPointer<RE::NiSourceTexture> featuresTexture0;

	// Fuzz map (fuzz color in rgb, fuzz weight in a) / Coat normal map (coat normal in rgb, coat roughness in a)
	RE::NiPointer<RE::NiSourceTexture> featuresTexture1;

	std::array<float, 3> projectedMaterialBaseColorScale = { 1.f, 1.f, 1.f };
	float projectedMaterialRoughness = 1.f;
	float projectedMaterialSpecularLevel = 0.04f;
	GlintParameters projectedMaterialGlintParameters;
	std::string inputFilePath = "";
};
