#pragma once

#include "PBRTextureSlots.h"
#include "TruePBR.h"

/** @brief Material capabilities stored in the PBR v1 payload. */
enum class PBRFlags : uint32_t
{
	Subsurface = 1 << 0,
	TwoLayer = 1 << 1,
	ColoredCoat = 1 << 2,
	InterlayerParallax = 1 << 3,
	CoatNormal = 1 << 4,
	Fuzz = 1 << 5,
	HairMarschner = 1 << 6,
	Glint = 1 << 7,
};

/** @brief GPU capability bits in the existing PBR constant-buffer layout. */
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

/** @brief Independent PBR surface parameters, in linear space and authoring units. */
struct PBRParameters
{
	float roughnessScale = 1.f;                    ///< Base roughness multiplier.
	float specularLevel = .04f;                    ///< Dielectric reflectance in [0, 1].
	float displacementScale = 1.f;                 ///< Displacement multiplier.
	RE::NiColor subsurfaceColor{ 1.f, 1.f, 1.f };  ///< Linear subsurface RGB multiplier.
	float subsurfaceOpacity = 0.f;                 ///< Subsurface thickness multiplier in [0, 1].
	RE::NiColor coatColor{ 1.f, 1.f, 1.f };        ///< Linear coat RGB multiplier.
	float coatStrength = 1.f;                      ///< Coat strength in [0, 1].
	float coatRoughness = 1.f;                     ///< Coat roughness in [0, 1].
	float coatSpecularLevel = .04f;                ///< Coat dielectric reflectance in [0, 1].
	RE::NiColor fuzzColor{ 1.f, 1.f, 1.f };        ///< Linear fuzz RGB multiplier.
	float fuzzWeight = 0.f;                        ///< Fuzz weight in [0, 1].
	float glintScreenSpaceScale = 1.5f;            ///< Glint screen-space scale, at least 1.
	float glintLogMicrofacetDensity = 40.f;        ///< Authoring log density in [1, 40]; inverted only during GPU upload.
	float glintMicrofacetRoughness = .015f;        ///< Glint roughness in [0.005, 0.3].
	float glintDensityRandomization = 2.f;         ///< Glint density randomization in [0, 5].

	/** @brief Returns the 21 scalar values in PBR v1 wire order, without structure padding. */
	std::array<float, 21> Values() const;
	/** @brief Assigns scalar values in PBR v1 wire order. */
	void SetValues(const std::array<float, 21>& values);
	/** @brief Checks all parameter ranges, including inactive layers and finite values. */
	bool IsValid() const;
};

/**
 * @brief Lighting material with an independent NIF type and PBR parameter payload.
 *
 * Feature 21 identifies the material throughout loading, pooling, cloning, and rendering.
 * Common UV, alpha, refraction, and clamp fields retain the lighting-base ABI.
 */
class BSLightingShaderMaterialPBR : public RE::BSLightingShaderMaterialBase
{
public:
	/** @brief Runtime record bindings; these do not identify the material. */
	struct MaterialExtensions
	{
		TruePBR::PBRTextureSetData* textureSetData = nullptr;
		TruePBR::PBRMaterialObjectData* materialObjectData = nullptr;
	};

	inline static constexpr auto FEATURE = static_cast<RE::BSShaderMaterial::Feature>(21);
	inline static constexpr uint32_t Version = 1;
	inline static constexpr uint32_t PayloadSize = 92;
	inline static constexpr auto RmaosTexture = PBRTextureIndex(PBRTextureSlot::Rmaos);
	inline static constexpr auto EmissiveTexture = PBRTextureIndex(PBRTextureSlot::Emissive);
	inline static constexpr auto DisplacementTexture = PBRTextureIndex(PBRTextureSlot::Displacement);
	inline static constexpr auto FeaturesTexture0 = PBRTextureIndex(PBRTextureSlot::Features0);
	inline static constexpr auto FeaturesTexture1 = PBRTextureIndex(PBRTextureSlot::Features1);

	/** @brief Initializes an unpooled material with deterministic defaults. */
	BSLightingShaderMaterialPBR();
	/** @brief Removes extension tracking before the material is destroyed. */
	~BSLightingShaderMaterialPBR() override;
	/** @brief Allocates a game-heap material for the engine material pool. */
	RE::BSShaderMaterial* Create() override;
	/** @brief Copies render state and extension bindings from a PBR source; pool ownership is not copied. */
	void CopyMembers(RE::BSShaderMaterial* that) override;
	/** @brief Compares every render-affecting field after checking the source type. */
	bool DoIsCopy(RE::BSShaderMaterial* that) override;
	/** @brief Hashes the equality key and the engine's uniqueness seed, without padding or diagnostic paths. */
	uint32_t ComputeCRC32(uint32_t srcHash) override;
	/** @brief Returns the process-lifetime default PBR material. */
	RE::BSShaderMaterial* GetDefault() override;
	/** @brief Returns the independent PBR feature, 21. */
	Feature GetFeature() const override;
	/** @brief Loads the seven PBR texture roles and applies a matching TXST override. */
	void OnLoadTextureSet(uint64_t arg1, RE::BSTextureSet* inTextureSet) override;
	/** @brief Releases common and PBR texture references. */
	void ClearTextures() override;
	/** @brief Supplies neutral textures for unbound PBR resources. */
	void ReceiveValuesFromRootMaterial(bool skinned, bool rimLighting, bool softLighting, bool backLighting, bool MSN) override;
	/** @brief Enumerates the material's bound textures for engine visitors. */
	uint32_t GetTextures(RE::NiSourceTexture** textures) override;
	/** @brief Writes the common material scalars and complete PBR v1 payload. */
	void SaveBinary(RE::NiStream& stream) override;
	/** @brief Reads the common material scalars and complete PBR v1 payload. */
	void LoadBinary(RE::NiStream& stream) override;

	/** @brief Checks both Lighting family and PBR feature before downcasting. */
	static bool IsPBR(const RE::BSShaderMaterial* material);
	/** @brief Checks known capability bits and combinations supported by the GPU layout. */
	static bool ValidFeatures(uint32_t features);
	/** @brief Allocates a scrap-heap load temporary; the engine links, destroys, and frees it. */
	static BSLightingShaderMaterialPBR* Make();
	/** @brief Checks required texture paths, reserved slots, and feature dependencies. */
	bool ValidateTextureSet() const;
	/** @brief Applies a valid TXST override as a whole; invalid overrides leave the material unchanged. */
	void ApplyTextureSetData(const TruePBR::PBRTextureSetData& data);
	/** @brief Applies a valid projected MATO override as a whole. */
	void ApplyMaterialObjectData(const TruePBR::PBRMaterialObjectData& data);
	/** @brief Restores neutral projected-material parameters. */
	void ClearMaterialObjectData();

	/** @brief Returns the PBR roughness multiplier. */
	float GetRoughnessScale() const { return parameters.roughnessScale; }
	/** @brief Returns the dielectric specular reflectance. */
	float GetSpecularLevel() const { return parameters.specularLevel; }
	/** @brief Returns the PBR displacement multiplier. */
	float GetDisplacementScale() const { return parameters.displacementScale; }
	/** @brief Returns the linear subsurface color. */
	const RE::NiColor& GetSubsurfaceColor() const { return parameters.subsurfaceColor; }
	/** @brief Returns the subsurface thickness multiplier. */
	float GetSubsurfaceOpacity() const { return parameters.subsurfaceOpacity; }
	/** @brief Returns the linear coat color. */
	const RE::NiColor& GetCoatColor() const { return parameters.coatColor; }
	/** @brief Returns the coat layer strength. */
	float GetCoatStrength() const { return parameters.coatStrength; }
	/** @brief Returns the coat roughness. */
	float GetCoatRoughness() const { return parameters.coatRoughness; }
	/** @brief Returns the coat dielectric reflectance. */
	float GetCoatSpecularLevel() const { return parameters.coatSpecularLevel; }
	/** @brief Returns the linear fuzz color. */
	const RE::NiColor& GetFuzzColor() const { return parameters.fuzzColor; }
	/** @brief Returns the fuzz layer weight. */
	float GetFuzzWeight() const { return parameters.fuzzWeight; }
	/** @brief Returns base-surface glint parameters and the PBR capability's enabled state. */
	GlintParameters GetGlintParameters() const;
	/** @brief Returns the projected material's linear RGB multiplier. */
	const std::array<float, 3>& GetProjectedMaterialBaseColorScale() const { return projectedMaterialBaseColorScale; }
	/** @brief Returns the projected material's roughness. */
	float GetProjectedMaterialRoughness() const { return projectedMaterialRoughness; }
	/** @brief Returns the projected material's dielectric reflectance. */
	float GetProjectedMaterialSpecularLevel() const { return projectedMaterialSpecularLevel; }
	/** @brief Returns projected glint parameters. */
	const GlintParameters& GetProjectedMaterialGlintParameters() const { return projectedMaterialGlintParameters; }

	/** @brief Record bindings for live TXST and MATO updates. */
	inline static std::unordered_map<BSLightingShaderMaterialPBR*, MaterialExtensions> All;
	/** @brief Protects extension tracking during asynchronous model loading. */
	inline static std::mutex AllLock;
	stl::enumeration<PBRFlags> pbrFlags;                     ///< PBR capabilities; never aliases of BSShaderProperty flags.
	PBRParameters parameters;                                ///< Independent base-surface parameters.
	RE::NiPointer<RE::NiSourceTexture> rmaosTexture;         ///< Roughness, metallic, AO, and specular channels.
	RE::NiPointer<RE::NiSourceTexture> emissiveTexture;      ///< Linear emission RGB.
	RE::NiPointer<RE::NiSourceTexture> displacementTexture;  ///< Displacement height.
	RE::NiPointer<RE::NiSourceTexture> featuresTexture0;     ///< Subsurface color/thickness or coat color/strength.
	RE::NiPointer<RE::NiSourceTexture> featuresTexture1;     ///< Fuzz color/weight or coat normal/roughness.
	std::array<float, 3> projectedMaterialBaseColorScale{ 1.f, 1.f, 1.f };
	float projectedMaterialRoughness = 1.f;
	float projectedMaterialSpecularLevel = .04f;
	GlintParameters projectedMaterialGlintParameters;
	std::string inputFilePath;  ///< Diagnostic source path, excluded from material identity.
	bool valid = true;          ///< False for rejected input; all rendering passes must reject this material.

private:
	/** @brief Builds a canonical key shared by equality and hashing. */
	std::vector<uint8_t> MaterialKey() const;
};
