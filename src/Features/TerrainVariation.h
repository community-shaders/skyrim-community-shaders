#pragma once

struct TerrainVariation : Feature
{
	static TerrainVariation* GetSingleton()
	{
		static TerrainVariation singleton;
		return &singleton;
	}

	virtual inline std::string GetName() override { return "Terrain Variation"; }
	virtual inline std::string GetShortName() override { return "TerrainVariation"; }
	virtual inline std::string_view GetShaderDefineName() override { return "TERRAIN_VARIATION"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type shaderType) override { return shaderType == RE::BSShader::Type::Lighting; }
	virtual bool IsCore() const override { return false; };
	virtual bool SupportsVR() override { return true; }

	struct Settings
	{
		bool enabled = true;
		float startDistance = 200.0f;
		float maxDistance = 2000.0f;
		float heightCompensationFactor = 1.15f; // Compensation for terrain parallax when enabled
		float shadowRayDirFactor = 1.5f; // Shadow ray direction multiplier for parallax shadows
	};

	Settings settings;
	bool showAdvanced = false;

	virtual void DrawSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;
	virtual void PostPostLoad() override;
	void UpdateShaderSettings();
};