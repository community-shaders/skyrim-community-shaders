#pragma once

#include "Buffer.h"

struct ExtendedMaterials : Feature
{
	virtual inline std::string GetName() override { return "Extended Materials"; }
	virtual inline std::string GetShortName() override { return "ExtendedMaterials"; }
	virtual inline std::string_view GetShaderDefineName() override { return "EXTENDED_MATERIALS"; }
	virtual std::string_view GetCategory() const override { return "Materials"; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Extended Materials adds advanced material effects including parallax occlusion mapping and complex material blending.\n"
			"This feature enhances surface detail and depth perception for more realistic textures.",
			{ "Parallax occlusion mapping for depth",
				"Complex material blending",
				"Terrain heightmap support",
				"Parallax shadows",
				"Height-based texture blending" }
		};
	}

	bool HasShaderDefine(RE::BSShader::Type shaderType) override;

	struct alignas(16) Settings
	{
		uint EnableComplexMaterial = 1;

		uint EnableParallax = 1;
		uint EnableTerrain = 0;
		uint EnableHeightBlending = 1;

		uint EnableShadows = 1;
		uint ExtendShadows = 0;
		uint EnableParallaxWarpingFix = 1;

		float pad[1];
	};
	STATIC_ASSERT_ALIGNAS_16(Settings);

	Settings settings;

	virtual void DataLoaded() override;

	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void RestoreDefaultSettings() override;

	virtual bool SupportsVR() override { return true; };
	virtual bool IsCore() const override { return true; };
};
