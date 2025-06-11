#pragma once

struct LODBlending : Feature
{
	static LODBlending* GetSingleton()
	{
		static LODBlending singleton;
		return &singleton;
	}

	virtual inline std::string GetName() override { return "LOD Blending"; }
	virtual inline std::string GetShortName() override { return "LODBlending"; }
	virtual inline std::string_view GetShaderDefineName() override { return "LOD_BLENDING"; }
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Provides seamless visual transitions between Level of Detail (LOD) objects and full-detail objects, eliminating harsh transitions and creating smooth visual continuity.",
			{ "Smooth LOD object brightness blending",
				"Enhanced terrain LOD appearance matching",
				"Snow-specific LOD brightness adjustment",
				"Optional terrain vertex color modification",
				"Seamless transition between detail levels" }
		};
	}
	virtual inline bool HasShaderDefine(RE::BSShader::Type) override { return true; };

	struct Settings
	{
		float LODTerrainBrightness = 1;
		float LODObjectBrightness = 1;
		float LODObjectSnowBrightness = 1;
		uint DisableTerrainVertexColors = false;
	};

	Settings settings;

	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void RestoreDefaultSettings() override;

	virtual bool SupportsVR() override { return true; };
	virtual bool IsCore() const override { return true; };
};
