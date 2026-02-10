#pragma once

#include "Feature.h"
#include "HDR.h"

struct HDRDisplay : public Feature
{
	virtual inline std::string GetName() override { return "HDR Display"; }
	virtual inline std::string GetShortName() override { return "HDRDisplay"; }
	virtual inline std::string_view GetCategory() const override { return "Display"; }
	virtual inline bool SupportsVR() override { return true; }
	virtual inline bool IsCore() const override { return false; }

	virtual inline std::string_view GetShaderDefineName() override { return "HDR_OUTPUT"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type shaderType) override
	{
		// HDR pipeline is always active
		return shaderType == RE::BSShader::Type::ImageSpace;
	}

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Real High Dynamic Range output for HDR displays.",
			{
				"HDR10 output support (10-bit) with upgraded HDR buffers (16-Bit).",
				"Upgraded DICE tonemapper for HDR, keeping Skyrim's distinct look while improving highlight handling and color vibrancy.",
				"Configurable paper white and peak brightness",
			}
		};
	}

	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void DrawSettings() override;

	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;

	bool IsHDREnabled() const;
	void ApplyHDR();

	HDR* hdr = nullptr;
};
