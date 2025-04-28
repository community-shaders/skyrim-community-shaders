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

	virtual inline std::string GetName() override { return "IBL"; }
	virtual inline std::string GetShortName() override { return "IBL"; }
	virtual inline std::string_view GetShaderDefineName() override { return "IBL"; }
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

	struct Settings
	{
		uint EnableDiffuseIBL = 1;
		float DiffuseIBLScale = 1.0f;
		float DALCAmount = 0.1f;
		uint SampleUnderHorizonFromDynCube = 0;
	} settings;

	ID3D11ComputeShader* GetDiffuseIBLCS();
};
