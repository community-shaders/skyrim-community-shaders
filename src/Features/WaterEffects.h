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
	virtual inline std::string GetFeatureModLink() override { return "https://www.nexusmods.com/skyrimspecialedition/mods/112762"; }
	virtual inline std::string_view GetShaderDefineName() override { return "WATER_EFFECTS"; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Water Effects enhances water rendering with realistic caustics and underwater lighting effects.\n"
			"This feature adds dynamic light patterns and improved water visual quality.",
			{ "Realistic water caustics",
				"Enhanced underwater lighting",
				"Dynamic light patterns on water surfaces",
				"Improved water visual fidelity",
				"Atmospheric underwater effects" }
		};
	}

	bool HasShaderDefine(RE::BSShader::Type shaderType) override;

	virtual void SetupResources() override;

	virtual void Prepass() override;

	virtual bool SupportsVR() override { return true; };
};
