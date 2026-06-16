#pragma once

#include "Buffer.h"

struct ExtendedMaterials : Feature
{
	virtual inline std::string GetName() override { return "Extended Materials"; }
	virtual std::string GetDisplayName() override { return T("feature.extended_materials.name", "Extended Materials"); }
	virtual inline std::string GetShortName() override { return "ExtendedMaterials"; }
	virtual inline std::string_view GetShaderDefineName() override { return "EXTENDED_MATERIALS"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kMaterials; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.extended_materials.description", "Extended Materials adds advanced material effects including parallax occlusion mapping and complex material blending.\nThis feature enhances surface detail and depth perception for more realistic textures."),
			{ T("feature.extended_materials.key_feature_1", "Parallax occlusion mapping for depth"),
				T("feature.extended_materials.key_feature_2", "Complex material blending"),
				T("feature.extended_materials.key_feature_3", "Terrain heightmap support"),
				T("feature.extended_materials.key_feature_4", "Parallax shadows"),
				T("feature.extended_materials.key_feature_5", "Height-based texture blending") } };
	};

	/** @brief Returns true only for Lighting shader type. */
	bool HasShaderDefine(RE::BSShader::Type shaderType) override;

	struct alignas(16) Settings
	{
		uint EnableComplexMaterial = 1;

		uint EnableParallax = 1;
		uint EnableTerrain = 0;
		uint EnableHeightBlending = 1;

		uint EnableShadows = 1;
		uint ExtendShadows = 1;
		uint EnableParallaxWarpingFix = 1;

		float pad[1];
	};
	STATIC_ASSERT_ALIGNAS_16(Settings);

	Settings settings;

	/** @brief Enables bLandSpecular INI setting when terrain parallax is active. */
	virtual void DataLoaded() override;

	/** @brief Draws the ImGui settings UI for complex material, parallax, and shadow options. */
	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;
	virtual bool IsCore() const override { return true; };
};
