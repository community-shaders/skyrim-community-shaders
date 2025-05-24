#pragma once

#include <winrt/base.h>

struct WaterEffects : Feature
{
public:
	static WaterEffects* GetSingleton()
	{
		static WaterEffects singleton;
		return &singleton;
	}

	winrt::com_ptr<ID3D11ShaderResourceView> causticsView;

	virtual inline std::string GetName() override { return "Water Effects"; }
	virtual inline std::string GetShortName() override { return "WaterEffects"; }
	virtual inline std::string_view GetShaderDefineName() override { return "WATER_EFFECTS"; }

	bool HasShaderDefine(RE::BSShader::Type shaderType) override;

	virtual void SetupResources() override;

	virtual void Prepass() override;

	virtual bool SupportsVR() override { return true; };
};
