#pragma once

#include "Feature.h"
#include "HDR.h"

struct HDRDisplay : public Feature
{
	virtual inline std::string GetName() override { return "HDR Display"; }
	virtual inline std::string GetShortName() override { return "HDRDisplay"; }
	virtual inline std::string_view GetCategory() const override { return "Display"; }
	virtual inline bool SupportsVR() override { return true; }
	virtual inline bool IsCore() const override { return true; }

	virtual inline std::string_view GetShaderDefineName() override { return "HDR_OUTPUT"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type shaderType) override
	{
		auto* hdrSingleton = HDR::GetSingleton();
		return hdrSingleton && hdrSingleton->hdrDisplayDetected && hdrSingleton->settings.enableHDR && shaderType == RE::BSShader::Type::ImageSpace;
	}

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"High Dynamic Range output for HDR displays",
			{
				"HDR10 output support (R10G10B10A2_UNORM)",
				"Multiple tonemapper options",
				"Configurable paper white and peak brightness",
				"HDR color grading controls",
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
