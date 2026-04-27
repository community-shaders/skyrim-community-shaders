#pragma once

struct TerrainVariation : Feature
{
private:
	static constexpr std::string_view MOD_ID = "148123";

public:
	virtual inline std::string GetName() override { return "Terrain Variation"; }
	virtual inline std::string GetShortName() override { return "TerrainVariation"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }
	virtual inline std::string_view GetShaderDefineName() override { return "TERRAIN_VARIATION"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type shaderType) override
	{
		// Always compile the TERRAIN_VARIATION path when the feature is loaded; runtime on/off uses FeatureData (b6).
		return loaded && shaderType == RE::BSShader::Type::Lighting;
	}
	virtual bool IsCore() const override { return false; };
	virtual bool SupportsVR() override { return true; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLandscapeAndTextures; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Terrain Variation reduces the repeating pattern effect on terrain textures.\n"
			"This technique creates more natural-looking terrain by adding variation to texture sampling.",
			{ "Reduces terrain texture tiling",
				"Adjustable distance-based blending",
				"Improved terrain visual quality",
				"Compatible with Extended Materials parallax" }
		};
	}

	struct alignas(16) Settings
	{
		uint32_t enableTilingFix = 1;
		uint32_t enableLODTerrainTilingFix = 1;
		uint32_t pad[2]{};
	};
	
	STATIC_ASSERT_ALIGNAS_16(Settings);

	Settings settings;

	virtual void DrawSettings() override;
	virtual bool DrawFailLoadMessage() const override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	virtual void PostPostLoad() override;
};