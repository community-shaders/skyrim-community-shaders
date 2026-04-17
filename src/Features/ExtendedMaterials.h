#pragma once

#include "Buffer.h"

struct ExtendedMaterials : Feature
{
	virtual inline std::string GetName() override { return "Extended Materials"; }
	virtual inline std::string GetShortName() override { return "ExtendedMaterials"; }
	virtual inline std::string_view GetShaderDefineName() override { return "EXTENDED_MATERIALS"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kMaterials; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Extended Materials adds advanced material effects including screen-space displacement mapping and complex material blending.\n"
			"This feature enhances surface detail and depth perception for more realistic textures.",
			{ "Screen-space displacement mapping (SSDM)",
				"Complex material blending",
				"Terrain heightmap support",
				"Height-based texture blending" }
		};
	}

	bool HasShaderDefine(RE::BSShader::Type shaderType) override;

	struct alignas(16) Settings
	{
		uint EnableComplexMaterial = 1;

		uint EnableParallax = 1;
		uint EnableTerrain = 0;
		uint EnableHeightBlending = 1;

		float DisplacementScale = 0.05f;
		float pad[3];
	};
	STATIC_ASSERT_ALIGNAS_16(Settings);

	Settings settings;

	virtual void DataLoaded() override;

	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void RestoreDefaultSettings() override;

	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;

	void DrawSSDM();
	void RegisterDisplacementRT();

	virtual bool SupportsVR() override { return true; };
	virtual bool IsCore() const override { return true; };

	// SSDM resources
	static constexpr int SSDM_MIP_LEVELS = 4;

	struct alignas(16) SSDMCB
	{
		float FullDimX;
		float FullDimY;
		float RcpFullDimX;
		float RcpFullDimY;
		int MipLevel;
		int IsCoarsest;
		int SrcMipLevel;
		int pad;
	};
	STATIC_ASSERT_ALIGNAS_16(SSDMCB);

	eastl::unique_ptr<ConstantBuffer> ssdmCB;

	eastl::unique_ptr<Texture2D> texDisplacement;
	winrt::com_ptr<ID3D11RenderTargetView> rtvDisplacement;
	winrt::com_ptr<ID3D11UnorderedAccessView> uavDisplacement[SSDM_MIP_LEVELS];

	eastl::unique_ptr<Texture2D> texSSDMLevel[SSDM_MIP_LEVELS];

	winrt::com_ptr<ID3D11ComputeShader> ssdmBuildPyramidCS;
	winrt::com_ptr<ID3D11ComputeShader> ssdmDisplaceCS;

	winrt::com_ptr<ID3D11SamplerState> ssdmLinearSampler;

	ID3D11ShaderResourceView* GetSSDMOffsetSRV() const;
	void ClearDisplacementTexture();
};
