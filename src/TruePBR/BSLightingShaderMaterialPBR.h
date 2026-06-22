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

/**
 * @brief PBR material class for standard (non-landscape) meshes.
 *
 * Extends BSLightingShaderMaterialBase with physically-based rendering properties
 * including roughness/metallic/AO textures, emissive, displacement, subsurface
 * scattering, clearcoat, fuzz, and glint parameters. Also supports projected
 * material (MATO) overlays for decal-like PBR effects.
 */
class BSLightingShaderMaterialPBR : public RE::BSLightingShaderMaterialBase
{
public:
	struct MaterialExtensions
	{
		TruePBR::PBRTextureSetData* textureSetData = nullptr;
		TruePBR::PBRMaterialObjectData* materialObjectData = nullptr;
		/**
		 * FormID of the TESObjectREFR whose Clone3D call last wrote MATO data to this
		 * material.  Used by the fork-before-write check to detect when a pooled material
		 * instance would be overwritten by a different ref, triggering a clone instead.
		 */
		RE::FormID lastOwnerRefFormID = 0;
	};

	inline static constexpr auto FEATURE = static_cast<RE::BSShaderMaterial::Feature>(32);

	inline static constexpr auto RmaosTexture = static_cast<RE::BSTextureSet::Texture>(5);
	inline static constexpr auto EmissiveTexture = static_cast<RE::BSTextureSet::Texture>(2);
	inline static constexpr auto DisplacementTexture = static_cast<RE::BSTextureSet::Texture>(3);
	inline static constexpr auto FeaturesTexture0 = static_cast<RE::BSTextureSet::Texture>(7);
	inline static constexpr auto FeaturesTexture1 = static_cast<RE::BSTextureSet::Texture>(6);

	/** @brief Destructor that removes this material from the global tracking map. */
	~BSLightingShaderMaterialPBR();

	// override (BSLightingShaderMaterialBase)
	/**
	 * @brief Creates a heap-allocated canonical copy for the shader material hash map.
	 *
	 * MUST use regular heap (new), NOT Make()/scrap heap. BSLightingShaderProperty::LinkObject
	 * calls ScrapHeap::Free() immediately after Link -- a scrap-heap canonical would be popped
	 * off the stack and freed while property->material still points to it (use-after-free).
	 *
	 * @return A new heap-allocated BSLightingShaderMaterialPBR instance.
	 */
	RE::BSShaderMaterial* Create() override;                                                                                      // 01
	/**
	 * @brief Copies all PBR-specific members from another material, including textures and extensions.
	 * @param that Source material to copy from (must be BSLightingShaderMaterialPBR).
	 */
	void CopyMembers(RE::BSShaderMaterial* that) override;                                                                        // 02
	/**
	 * @brief Computes a CRC32 hash incorporating all PBR-specific fields and texture paths.
	 * @param srcHash Initial hash seed.
	 * @return Combined CRC32 hash for material deduplication.
	 */
	std::uint32_t ComputeCRC32(uint32_t srcHash) override;                                                                        // 04
	/**
	 * @brief Returns the material feature type.
	 * @return Always returns kDefault to integrate with the vanilla shader dispatch.
	 */
	Feature GetFeature() const override;                                                                                          // 06
	/**
	 * @brief Loads PBR textures (RMAOS, emissive, displacement, features) from a texture set.
	 *
	 * Also looks up and applies PBR texture set data configuration if available.
	 *
	 * @param arg1 Unused argument passed through from the base class.
	 * @param inTextureSet The texture set to load PBR textures from.
	 */
	void OnLoadTextureSet(std::uint64_t arg1, RE::BSTextureSet* inTextureSet) override;                                           // 08
	/** @brief Releases all PBR texture references in addition to base class textures. */
	void ClearTextures() override;                                                                                                // 09
	/**
	 * @brief Assigns default textures to any PBR texture slots that are still null.
	 * @param skinned Whether the mesh is skinned.
	 * @param rimLighting Whether rim lighting is enabled.
	 * @param softLighting Whether soft lighting is enabled.
	 * @param backLighting Whether back lighting is enabled.
	 * @param MSN Whether model-space normals are used.
	 */
	void ReceiveValuesFromRootMaterial(bool skinned, bool rimLighting, bool softLighting, bool backLighting, bool MSN) override;  // 0A
	/**
	 * @brief Writes all non-null textures (base + PBR) into the output array.
	 * @param textures Output array to fill with texture pointers.
	 * @return The number of textures written.
	 */
	uint32_t GetTextures(RE::NiSourceTexture** textures) override;                                                                // 0B
	/**
	 * @brief Deserializes PBR-specific parameters (coat, fuzz, glint) from a NiStream.
	 * @param stream The binary stream to read from.
	 */
	void LoadBinary(RE::NiStream& stream) override;                                                                               // 0D

	/**
	 * @brief Allocates a scrap-heap temporary for use during BSLightingShaderProperty::LoadBinary.
	 *
	 * The temp is direct-assigned to property->material so that BSLightingShaderProperty::LinkObject
	 * (NiStream link phase) can find it, call BSShaderMaterialHashMap::Link to produce the canonical,
	 * then ScrapHeap::Free() to pop this temp. Never use Make() as the Create() implementation.
	 *
	 * @return A scrap-heap-allocated PBR material, or nullptr on allocation failure.
	 */
	static BSLightingShaderMaterialPBR* Make();

	/**
	 * @brief Applies PBR texture set parameters (roughness, specular, subsurface, coat, fuzz, glint) to this material.
	 * @param textureSetData The texture set configuration to apply.
	 */
	void ApplyTextureSetData(const TruePBR::PBRTextureSetData& textureSetData);
	/**
	 * @brief Applies projected material object parameters (base color scale, roughness, specular, glint) to this material.
	 * @param materialObjectData The material object configuration to apply.
	 */
	void ApplyMaterialObjectData(const TruePBR::PBRMaterialObjectData& materialObjectData);
	/**
	 * @brief Resets all projected-material fields to their default values.
	 *
	 * Called on references that carry no MATO (or no PBR config for their MATO) to
	 * prevent stale data copied in by CopyMembers from persisting on the material.
	 */
	void ClearMaterialObjectData();

	/** @brief Returns the roughness scale factor (stored in specularColorScale). */
	float GetRoughnessScale() const;
	/** @brief Returns the non-metal specular reflectance level (stored in specularPower). */
	float GetSpecularLevel() const;

	/** @brief Returns the displacement/parallax scale factor (stored in rimLightPower). */
	float GetDisplacementScale() const;

	/** @brief Returns the subsurface scattering color (stored in specularColor). */
	const RE::NiColor& GetSubsurfaceColor() const;
	/** @brief Returns the subsurface scattering opacity (stored in subSurfaceLightRolloff). */
	float GetSubsurfaceOpacity() const;

	/** @brief Returns the clearcoat layer color (stored in specularColor). */
	const RE::NiColor& GetCoatColor() const;
	/** @brief Returns the clearcoat layer strength (stored in subSurfaceLightRolloff). */
	float GetCoatStrength() const;
	/** @brief Returns the clearcoat layer roughness. */
	float GetCoatRoughness() const;
	/** @brief Returns the clearcoat layer specular level. */
	float GetCoatSpecularLevel() const;

	/** @brief Returns the RGB base color scale for the projected (MATO) material. */
	const std::array<float, 3>& GetProjectedMaterialBaseColorScale() const;
	/** @brief Returns the roughness value for the projected (MATO) material. */
	float GetProjectedMaterialRoughness() const;
	/** @brief Returns the specular level for the projected (MATO) material. */
	float GetProjectedMaterialSpecularLevel() const;
	/** @brief Returns the glint parameters for the projected (MATO) material. */
	const GlintParameters& GetProjectedMaterialGlintParameters() const;

	/** @brief Returns the fuzz layer color. */
	const RE::NiColor& GetFuzzColor() const;
	/** @brief Returns the fuzz layer weight/intensity. */
	float GetFuzzWeight() const;

	/** @brief Returns the glint rendering parameters for this material's base surface. */
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
