#pragma once

#include "Buffer.h"

struct GrassLighting : Feature
{
public:
	virtual inline std::string GetName() override { return "Grass Lighting"; }
	virtual std::string GetDisplayName() override { return T("feature.grass_lighting.name", "Grass Lighting"); }
	virtual inline std::string GetShortName() override { return "GrassLighting"; }
	virtual inline std::string_view GetShaderDefineName() override { return "GRASS_LIGHTING"; }
	virtual bool HasShaderDefine(RE::BSShader::Type shaderType) override { return shaderType == RE::BSShader::Type::Grass; };
	virtual std::string_view GetCategory() const override { return FeatureCategories::kGrass; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.grass_lighting.description", "Grass Lighting enhances grass rendering with improved lighting, specularity, and subsurface scattering.\nThis makes grass appear more natural and responsive to lighting conditions."),
			{ T("feature.grass_lighting.key_feature_1", "Enhanced grass lighting model"),
				T("feature.grass_lighting.key_feature_2", "Specular highlights on grass"),
				T("feature.grass_lighting.key_feature_3", "Subsurface scattering effects"),
				T("feature.grass_lighting.key_feature_4", "Improved grass visual quality"),
				T("feature.grass_lighting.key_feature_5", "Configurable material properties") } };
	};

	struct alignas(16) Settings
	{
		float Glossiness = 20.0f;
		float SpecularStrength = 0.5f;
		float SubsurfaceScatteringAmount = 1.0f;
		uint OverrideComplexGrassSettings = false;
		float BasicGrassBrightness = 1.0f;
		float ComplexGrassThreshold = 0.03f;
		float2 pad0;
	};
	STATIC_ASSERT_ALIGNAS_16(Settings);

	Settings settings;

	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void RestoreDefaultSettings() override;

};
