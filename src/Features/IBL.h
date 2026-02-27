#pragma once

struct IBL : Feature
{
public:
	virtual bool SupportsVR() override { return true; };
	virtual bool IsCore() const override { return true; };

	virtual inline std::string GetName() override { return "Image Based Lighting"; }
	virtual inline std::string GetShortName() override { return "ImageBasedLighting"; }
	virtual inline std::string_view GetShaderDefineName() override { return "IBL"; }
	virtual std::string_view GetCategory() const override { return "Lighting"; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Image Based Lighting provides realistic diffuse ambient lighting for exteriors.",
			{ "Realistic diffuse ambient lighting from environment maps",
				"Spherical harmonics-based ambient light calculation",
				"Enhanced exterior ambient lighting quality",
				"Configurable intensity and saturation, mixing with DALC" }
		};
	}

	bool HasShaderDefine(RE::BSShader::Type) override { return true; };

	Texture2D* diffuseIBLTexture = nullptr;
	Texture2D* diffuseSkyIBLTexture = nullptr;
	ID3D11ComputeShader* diffuseIBLCS = nullptr;

	virtual void RestoreDefaultSettings() override;
	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RegisterWeatherVariables() override;

	virtual void ReflectionsPrepass() override;
	virtual void Prepass() override;
	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;

	struct Settings
	{
		uint EnableDiffuseIBL = 0;
		uint PreserveFogLuminance = 0;
		uint UseStaticIBL = 1;
		float DALCAmount = 1.0f;
		float EnvIBLScale = 1.0f;
		float SkyIBLScale = 1.0f;
		float EnvIBLSaturation = 1.0f;
		float SkyIBLSaturation = 1.0f;
		float FogAmount = 0.0f;
		uint DALCMode = 0;  // 0: Luminance Ratio, 1: Color Ratio, 2: DALC + Sky
		float pad0 = 0.0f;
		float pad1 = 0.0f;
	} settings;

	eastl::unique_ptr<Texture2D> staticDiffuseIBLTexture = nullptr;
	eastl::unique_ptr<Texture2D> staticSpecularIBLTexture = nullptr;

	ID3D11ComputeShader* GetDiffuseIBLCS();
};
