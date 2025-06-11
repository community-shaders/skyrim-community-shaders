#pragma once

struct GrassLighting : Feature
{
	static GrassLighting* GetSingleton()
	{
		static GrassLighting singleton;
		return &singleton;
	}

	virtual inline std::string GetName() override { return "Grass Lighting"; }
	virtual inline std::string GetShortName() override { return "GrassLighting"; }
	virtual inline std::string_view GetShaderDefineName() override { return "GRASS_LIGHTING"; }
	virtual bool HasShaderDefine(RE::BSShader::Type shaderType) override { return shaderType == RE::BSShader::Type::Grass; };

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Grass Lighting enhances grass rendering with improved lighting, specularity, and subsurface scattering.\n"
			"This makes grass appear more natural and responsive to lighting conditions.",
			{ "Enhanced grass lighting model",
				"Specular highlights on grass",
				"Subsurface scattering effects",
				"Improved grass visual quality",
				"Configurable material properties" }
		};
	}

	struct alignas(16) Settings
	{
		float Glossiness = 20.0f;
		float SpecularStrength = 0.5f;
		float SubsurfaceScatteringAmount = 0.5f;
		uint OverrideComplexGrassSettings = false;
		float BasicGrassBrightness = 1.0f;
		uint pad[3];
	};

	Settings settings;

	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void RestoreDefaultSettings() override;

	virtual bool SupportsVR() override { return true; };
};
