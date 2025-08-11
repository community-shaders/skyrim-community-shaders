#pragma once

struct IBL : Feature
{
private:
	static constexpr std::string_view MOD_ID = "153478";

public:
	virtual bool SupportsVR() override { return true; };

	virtual inline std::string GetName() override { return "Image Based Lighting"; }
	virtual inline std::string GetShortName() override { return "ImageBasedLighting"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }
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
	ID3D11ComputeShader* diffuseIBLCS = nullptr;

	virtual void RestoreDefaultSettings() override;
	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void EarlyPrepass() override;
	virtual void Prepass() override;
	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;

	struct alignas(16) Settings
	{
		uint EnableDiffuseIBL = 1;
		float DiffuseIBLScale = 0.5f;
		float DALCAmount = 0.5f;
		float IBLSaturation = 0.65f;
		float FogAmount = 0.0f;
		uint SampleUnderHorizonFromDynCube = 0;
		uint pad[2];
	} settings;

	ID3D11ComputeShader* GetDiffuseIBLCS();
};
