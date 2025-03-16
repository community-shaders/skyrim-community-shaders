#pragma once

struct TerrainHelper : Feature
{
	static TerrainHelper* GetSingleton()
	{
		static TerrainHelper singleton;
		return &singleton;
	}

	virtual inline std::string GetName() override { return "Terrain Helper"; }
	virtual inline std::string GetShortName() override { return "TerrainHelper"; }
	virtual inline std::string_view GetShaderDefineName() override { return "TERRAIN_HELPER"; }

	struct Settings
	{
	} settings;

	struct ExtendedSlots
	{
		std::array<RE::NiSourceTexturePtr, 6> parallax;
	};

	std::unordered_map<uint32_t, ExtendedSlots> extendedSlots;
	RE::BGSTextureSet *defaultLandTexture;

	virtual void DataLoaded() override;
	virtual bool SupportsVR() override { return true; };

	void SetShaderResouces(ID3D11DeviceContext* a_context);
	bool TESObjectLAND_SetupMaterial(RE::TESObjectLAND* land);
	void BSLightingShader_SetupMaterial(RE::BSLightingShaderMaterialBase const* material);
};