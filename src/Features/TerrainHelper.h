#pragma once

struct TerrainHelper : Feature
{
private:
	static constexpr std::string_view MOD_ID = "143149";

public:
	static TerrainHelper* GetSingleton()
	{
		static TerrainHelper singleton;
		return &singleton;
	}

	virtual inline std::string GetName() override { return "Terrain Helper"; }
	virtual inline std::string GetShortName() override { return "TerrainHelper"; }
	virtual inline std::string_view GetShaderDefineName() override { return "TERRAIN_HELPER"; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Provides enhanced terrain material support for terrain mods that require additional texture slots and parallax mapping capabilities.",
			{ "Extended texture slot support for terrain materials",
				"Parallax mapping integration for terrain textures",
				"Automatic terrain material detection and setup",
				"Support for advanced terrain modifications",
				"Compatibility layer for terrain enhancement mods" }
		};
	}

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

	virtual void DataLoaded() override;
	virtual bool SupportsVR() override { return true; };
	virtual std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }

	void SetShaderResouces(ID3D11DeviceContext* a_context);
	bool TESObjectLAND_SetupMaterial(RE::TESObjectLAND* land);
	void BSLightingShader_SetupMaterial(RE::BSLightingShaderMaterialBase const* material);
};