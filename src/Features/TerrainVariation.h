#pragma once

struct TerrainVariation : Feature
{
	static TerrainVariation* GetSingleton()
	{
		static TerrainVariation singleton;
		return &singleton;
	}

	virtual inline std::string GetName() override { return "Terrain Tiling Fix"; }
	virtual inline std::string GetShortName() override { return "TerrainVariation"; }
	virtual inline std::string_view GetShaderDefineName() override { return "TERRAIN_VARIATION"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type shaderType) override { return shaderType == RE::BSShader::Type::Lighting; }
	virtual bool IsCore() const override { return false; };
	virtual bool SupportsVR() override { return true; }

	struct Settings
	{
		bool enabled = true;
	};

	Settings settings;

	virtual void DrawSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;
	
	// Add missing method declarations
	virtual void PostPostLoad() override;
	void UpdateShaderSettings();
};