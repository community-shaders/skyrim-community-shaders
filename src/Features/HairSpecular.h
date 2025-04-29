#pragma once

struct HairSpecular : Feature
{
	static HairSpecular* GetSingleton()
	{
		static HairSpecular singleton;
		return &singleton;
	}

	virtual inline std::string GetName() override { return "Hair Specular"; }
	virtual inline std::string GetShortName() override { return "HairSpecular"; }
	virtual inline std::string_view GetShaderDefineName() override { return "CS_HAIR"; }
	virtual bool HasShaderDefine(RE::BSShader::Type shaderType) override { return shaderType == RE::BSShader::Type::Lighting; };

	virtual void Prepass() override;

	virtual void SetupResources() override;

	struct alignas(16) Settings
	{
		uint Enabled = true;
		float HairGlossiness = 70.0f;
		float SpecularMult = 1.0f;
		float DiffuseMult = 1.0f;
		uint EnableTangentShift = true;
		float PrimaryTangentShift = 0.5f;
		float SecondaryTangentShift = -0.25f;
		float HairSaturation = 1.25f;
		float SpecularIndirectMult = 1.0f;
		float DiffuseIndirectMult = 1.0f;
		float BaseColorMult = 1.5f;
		float pad;
	} settings;

	eastl::unique_ptr<Texture2D> texTangentShift = nullptr;

	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void RestoreDefaultSettings() override;

	virtual bool SupportsVR() override { return true; };
};