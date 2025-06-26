#pragma once

struct TerrainVariation : Feature
{
private:
	static constexpr std::string_view MOD_ID = "148123";

public:
	static TerrainVariation* GetSingleton()
	{
		static TerrainVariation singleton;
		return &singleton;
	}
	virtual inline std::string GetName() override { return "Terrain Variation"; }
	virtual inline std::string GetShortName() override { return "TerrainVariation"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }
	virtual inline std::string_view GetShaderDefineName() override { return "TERRAIN_VARIATION"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type shaderType) override { return shaderType == RE::BSShader::Type::Lighting; }
	virtual bool IsCore() const override { return false; };
	virtual bool SupportsVR() override { return true; }
	virtual std::string_view GetCategory() const override { return "Landscape & Textures"; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Terrain Variation removes the repeating pattern effect on terrain textures.\n"
			"This technique creates more natural-looking terrain by adding variation to texture sampling.",
			{ "A comprehensive fix for terrain tiling issues",
				"Works with any terrain texture mod",
				"Improved terrain visual quality",
				"Compatible with Extended Materials parallax" }
		};
	}

	struct Settings
	{
		uint enableTilingFix = true;
		float startDistance = 200.0f;           // No offset will be applied under this distance
		float maxDistance = 2000.0f;            // Maximum distance that the terrain will blend the stochastic effect to
		float invDistanceRange = 0.00056f;      // Precalculated 1.0f / (maxDistance - startDistance) for shader optimization
		float heightCompensationFactor = 1.0f;  // Compensation for terrain parallax when enabled
		float shadowRayDirFactor = 1.0f;        // Shadow ray direction multiplier for parallax shadows
		int hashQuality = 1;                    // 0 = Low quality hash, 1 = High quality hash
		float pad;
	};

	Settings settings;
	bool showAdvanced = false;

	Settings defaultSettings = {
		true,      // enableTilingFix
		200.0f,    // startDistance
		2000.0f,   // maxDistance
		0.00056f,  // invDistanceRange (precalculated 1.0f / (2000.0f - 200.0f))
		1.0f,      // heightCompensationFactor
		1.0f,      // shadowRayDirFactor
		1          // hashQuality - default to high quality
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
};