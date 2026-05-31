#pragma once

#include "Buffer.h"

struct VolumetricShadows : Feature
{
public:
	virtual inline std::string GetName() override { return "Volumetric Shadows"; }
	virtual inline std::string GetShortName() override { return "VolumetricShadows"; }
	virtual inline std::string_view GetShaderDefineName() override { return "VOLUMETRIC_SHADOWS"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }
	virtual bool IsCore() const override { return true; }
	virtual bool IsInMenu() const override { return true; }

	static constexpr uint32_t kSharedShadowMapShaderSlot = 18;

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Volumetric Shadows provides downsampled VSM shadow maps for use by effects like particles and decals.\n"
			"This improves shadow quality on transparent objects with minimal performance impact.",
			{ "Downsampled VSM shadows",
				"Gaussian blur filtering",
				"Multi-cascade support",
				"Optimized for effects rendering" }
		};
	}

	bool HasShaderDefine(RE::BSShader::Type shaderType) override;

	// Compute shaders
	ID3D11ComputeShader* downsampleShadowMip0CS = nullptr;
	ID3D11ComputeShader* downsampleShadowMip1CS = nullptr;
	ID3D11ComputeShader* blurShadowHorizontalCS = nullptr;
	ID3D11ComputeShader* blurShadowVerticalCS = nullptr;

	ID3D11ShaderResourceView* shadowView = nullptr;

	// Downsampled shadow texture with 2 mip levels
	ID3D11Texture2D* shadowCopyTexture = nullptr;
	ID3D11ShaderResourceView* shadowCopySRV = nullptr;
	ID3D11ShaderResourceView* shadowCopyMip0SRV = nullptr;
	ID3D11ShaderResourceView* shadowCopyMip1SRV = nullptr;
	ID3D11UnorderedAccessView* shadowCopyMip0UAV = nullptr;
	ID3D11UnorderedAccessView* shadowCopyMip1UAV = nullptr;
	uint32_t shadowCopyWidth = 0;
	uint32_t shadowCopyHeight = 0;

	// Temporary texture for blur intermediate result
	ID3D11Texture2D* shadowBlurTempTexture = nullptr;
	ID3D11ShaderResourceView* shadowBlurTempMip0SRV = nullptr;
	ID3D11ShaderResourceView* shadowBlurTempMip1SRV = nullptr;
	ID3D11UnorderedAccessView* shadowBlurTempMip0UAV = nullptr;
	ID3D11UnorderedAccessView* shadowBlurTempMip1UAV = nullptr;

	// Samplers
	ID3D11SamplerState* linearSampler = nullptr;

	virtual void DrawSettings() override;
	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;

	void CopyShadowLightData();

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	virtual bool SupportsVR() override { return true; }

	virtual void PostPostLoad() override;

private:
	static void SetSharedShadowMapSRV(ID3D11DeviceContext* a_context, ID3D11ShaderResourceView* a_srv);
};
