#pragma once

/** @brief Provides extended terrain texture slot support for parallax mapping in terrain mods. */
struct TerrainHelper : Feature
{
private:
	static constexpr std::string_view MOD_ID = "143149";

public:
	virtual inline std::string GetName() override { return "Terrain Helper"; }
	virtual std::string GetDisplayName() override { return T("feature.terrain_helper.name", "Terrain Helper"); }
	/** @brief Returns the short identifier name. */
	virtual inline std::string GetShortName() override { return "TerrainHelper"; }
	virtual inline std::string_view GetShaderDefineName() override { return "TERRAIN_HELPER"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLandscapeAndTextures; }

	/** @brief Returns a description and list of key features for the UI summary. */
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.terrain_helper.description", "Provides enhanced terrain material support for terrain mods that require additional texture slots and parallax mapping capabilities."),
			{ T("feature.terrain_helper.key_feature_1", "Extended texture slot support for terrain materials"),
				T("feature.terrain_helper.key_feature_2", "Parallax mapping integration for terrain textures"),
				T("feature.terrain_helper.key_feature_3", "Automatic terrain material detection and setup"),
				T("feature.terrain_helper.key_feature_4", "Support for advanced terrain modifications"),
				T("feature.terrain_helper.key_feature_5", "Compatibility layer for terrain enhancement mods") } };
	};

	struct Settings
	{
	} settings;

	struct ExtendedSlots
	{
		std::array<RE::NiSourceTexturePtr, 6> parallax;
	};

	std::shared_mutex extendedSlotsMutex;
	std::unordered_map<uint32_t, ExtendedSlots> extendedSlots;
	RE::BGSTextureSet* defaultLandTexture;
	bool enabled = false;

	/** @brief Installs the TESObjectLAND material setup hook early for compatibility with TruePBR. */
	virtual void Load() override;
	/** @brief Looks up the default landscape texture set and enables the feature if found. */
	virtual void DataLoaded() override;
	/** @brief Installs the BSLightingShader material setup hook after plugin load. */
	virtual void PostPostLoad() override;
	virtual std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }

	/**
	 * @brief Binds extended parallax texture SRVs to pixel shader resource slots.
	 * @param a_context The D3D11 device context to set shader resources on.
	 */
	void SetShaderResources(ID3D11DeviceContext* a_context);

	/**
	 * @brief Extracts parallax textures from a land object's texture sets and caches them by material hash.
	 * @param land The land object whose quad textures are processed.
	 * @return True if the land object was valid and processed, false otherwise.
	 */
	bool TESObjectLAND_SetupMaterial(RE::TESObjectLAND* land);

	/**
	 * @brief Sets up extended parallax texture slots for a terrain lighting material during rendering.
	 * @param material The lighting shader material to look up cached parallax textures for.
	 */
	void BSLightingShader_SetupMaterial(RE::BSLightingShaderMaterialBase const* material);
};