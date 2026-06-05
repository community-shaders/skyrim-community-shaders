#pragma once

struct LODBlending : Feature
{
	virtual inline std::string GetName() override { return "LOD Blending"; }
	virtual std::string GetDisplayName() override { return T("feature.lod_blending.name", "LOD Blending"); }
	virtual inline std::string GetShortName() override { return "LODBlending"; }
	virtual inline std::string_view GetShaderDefineName() override { return "LOD_BLENDING"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLandscapeAndTextures; }
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.lod_blending.description", "Provides seamless visual transitions between Level of Detail (LOD) objects and full-detail objects, eliminating harsh transitions and creating smooth visual continuity."),
			{ T("feature.lod_blending.key_feature_1", "Smooth LOD object brightness blending"),
				T("feature.lod_blending.key_feature_2", "Enhanced terrain LOD appearance matching"),
				T("feature.lod_blending.key_feature_3", "Snow-specific LOD brightness adjustment"),
				T("feature.lod_blending.key_feature_4", "Optional terrain vertex color modification"),
				T("feature.lod_blending.key_feature_5", "Seamless transition between detail levels") } };
	};

	virtual inline bool HasShaderDefine(RE::BSShader::Type) override { return true; };

	struct Settings
	{
		float LODTerrainBrightness = 1;
		float LODObjectBrightness = 1;
		float LODObjectSnowBrightness = 1;
		uint DisableTerrainVertexColors = false;
		float LODTerrainGamma = 1;
		float LODObjectGamma = 1;
		float LODObjectSnowGamma = 1;
		float pad;
	};

	Settings settings;

	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void RestoreDefaultSettings() override;

	virtual bool SupportsVR() override { return true; };
	virtual bool IsCore() const override { return true; };
};
