#pragma once

#include <winrt/base.h>

struct WaterEffects : Feature
{
private:
	static constexpr std::string_view MOD_ID = "112762";

public:
	winrt::com_ptr<ID3D11ShaderResourceView> causticsView;
	virtual inline std::string GetName() override { return "Water Effects"; }
	virtual inline std::string GetShortName() override { return "WaterEffects"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }
	virtual inline std::string_view GetShaderDefineName() override { return "WATER_EFFECTS"; }
	virtual std::string_view GetCategory() const override { return "Water"; }

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
	virtual bool IsCore() const override { return true; };
};
