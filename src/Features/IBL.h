#pragma once

struct IBL : Feature
{
public:
	static IBL* GetSingleton()
	{
		static IBL singleton;
		return &singleton;
	}

	virtual bool SupportsVR() override { return true; };

	virtual inline std::string GetName() override { return "Image Based Lighting"; }
	virtual inline std::string GetShortName() override { return "ImageBasedLighting"; }
	virtual inline std::string_view GetShaderDefineName() override { return "IBL"; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Image Based Lighting enhances environmental lighting by using precomputed environment maps for realistic reflections and ambient lighting. This technique provides accurate environmental reflections and improved material rendering.",
			{
				"Realistic environmental reflections",
				"Enhanced ambient lighting",
				"Improved material appearance",
				"Physically-based lighting",
				"Dynamic environment integration"
			}
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
		uint SampleUnderHorizonFromDynCube = 0;
		uint pad[3];
	} settings;

	ID3D11ComputeShader* GetDiffuseIBLCS();
};
