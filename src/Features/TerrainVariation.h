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
	virtual inline std::string GetFeatureModLink() override { return "https://www.nexusmods.com/skyrimspecialedition/mods/148123"; }
	virtual inline std::string_view GetShaderDefineName() override { return "TERRAIN_VARIATION"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type shaderType) override { return shaderType == RE::BSShader::Type::Lighting; }
	virtual bool IsCore() const override { return false; };
	virtual bool SupportsVR() override { return true; }

	struct Settings
	{
		bool enableTilingFix = true;
		float startDistance = 200.0f;           // No offset will be applied under this distance
		float maxDistance = 2000.0f;            // Maximum distance that the terrain will blend the stochastic effect to
		float heightCompensationFactor = 1.0f;  // Compensation for terrain parallax when enabled
		float shadowRayDirFactor = 1.0f;        // Shadow ray direction multiplier for parallax shadows
	};

	Settings settings;
	bool showAdvanced = false;

	Settings defaultSettings = {
		true,     // enableTilingFix
		200.0f,   // startDistance
		2000.0f,  // maxDistance
		1.0f,     // heightCompensationFactor
		1.0f      // shadowRayDirFactor
	};

	virtual void DrawSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override
	{
		settings = defaultSettings;
		UpdateShaderSettings();
	}

	virtual void PostPostLoad() override;
	void UpdateShaderSettings();
	virtual void DrawUnloadedUI() override;
};