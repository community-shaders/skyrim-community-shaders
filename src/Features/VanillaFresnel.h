#pragma once

struct VanillaFresnel : public Feature
{
	////////////////////////////////////////////////// Boilerplate
	// Metadata
	virtual inline std::string GetName() override { return "Vanilla Fresnel"; }
	virtual inline std::string GetShortName() override { return "VanillaFresnel"; }
	virtual inline std::string_view GetCategory() const override { return "Lighting"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL("999999"); }
	virtual inline std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Add realistic environmental reflections to vanilla materials.",
            {
                "Add environmental reflections to all materials",
                "Supports vanilla and complex materials",
                "Optionally turn vanilla phong specular into GGX",
                "Optionally turn static cubemaps into dynamic reflections"
            }
		};
	}

	// Functionality
	virtual bool inline SupportsVR() override { return true; }
	virtual inline std::string_view GetShaderDefineName() override { return "VANILLA_FRESNEL"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type t) override { return t == RE::BSShader::Type::Lighting; };

	// Settings & UI
	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void DrawSettings() override;

	struct alignas(16) Settings
	{
		uint Enable = true;
        uint EnableGGX = false;
        uint EnableGGXOnGrass = false;
        uint EnableDynamicCubemapsConversion = false;
        float RoughnessMultiplier = 1.0f;
        float BaseF0Multiplier = 0.32f;
        float MinF0 = 0.02f;
        float CubemapToF0Multiplier = 1.0f;
        float ComplexMaterialF0Multiplier = 1.0f;
        float pad[3];
	} settings;
};