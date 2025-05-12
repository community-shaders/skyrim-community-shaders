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
	virtual inline std::string GetFeatureModLink() override { return "https://www.nexusmods.com/skyrimspecialedition/mods/86502"; }
	virtual inline std::string GetFeatureDescription() override { return "Grass Lighting replaces grass shaders to add more advanced shading including Complex Grass support, proper shadows, and light transmission."; }
	virtual bool HasShaderDefine(RE::BSShader::Type shaderType) override { return shaderType == RE::BSShader::Type::Grass; };

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
	virtual bool DrawFailLoadMessage() const override;
	virtual void DrawUnloadedUI() override;

	virtual bool SupportsVR() override { return true; };
};
