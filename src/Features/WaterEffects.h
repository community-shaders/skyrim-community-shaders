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

	winrt::com_ptr<ID3D11ShaderResourceView> causticsView;	virtual inline std::string GetName() const override { return "Water Effects"; }
	virtual inline std::string GetShortName() const override { return "WaterEffects"; }
	virtual inline std::string GetFeatureModLink() const override { return "https://www.nexusmods.com/skyrimspecialedition/mods/112762"; }
	virtual inline std::string_view GetShaderDefineName() const override { return "WATER_EFFECTS"; }

	bool HasShaderDefine(RE::BSShader::Type shaderType) override;

	virtual void SetupResources() override;
	virtual void Prepass() override;
	
	virtual void DrawUnloadedUI() override;

	virtual bool SupportsVR() override { return true; };
};
