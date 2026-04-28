#pragma once

#include "Buffer.h"

struct ScreenSpaceShadows : Feature
{
private:
	static constexpr std::string_view MOD_ID = "93209";

public:
	virtual inline std::string GetName() override { return "Screen Space Shadows"; }
	virtual inline std::string GetShortName() override { return "ScreenSpaceShadows"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL(MOD_ID); }
	virtual inline std::string_view GetShaderDefineName() override { return "SCREEN_SPACE_SHADOWS"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }
	virtual bool inline SupportsVR() override { return true; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Screen Space Shadows enhances shadow quality by adding detailed contact shadows and improving shadow accuracy.\n"
			"This technique adds fine-detail shadows that traditional shadow mapping might miss.",
			{ "Enhanced contact shadows",
				"Improved shadow detail",
				"Better shadow accuracy",
				"Fine-scale shadow effects",
				"Configurable shadow contrast" }
		};
	}

	bool HasShaderDefine(RE::BSShader::Type shaderType) override;

	struct Settings
	{
		bool Enabled = true;
		bool BlurDepthPyramid = false;
		float SurfaceThickness = 2.0f;
		float ShadowContrast = 1.0f;
		float RayLength = 100.0f;
		int SampleCount = 16;
	};
	Settings settings;

	static constexpr float kReferenceRayLength = 100.0f;
	static constexpr int kMinMip0Samples = 8;
	static constexpr int kMaxMip0Samples = 128;

	struct alignas(16) SSSCB
	{
		float2 FrameDim;
		float2 RcpTexDim;

		float2 TexDim;
		float2 DynamicRes;

		float SurfaceThickness;
		float ShadowContrast;
		float RayLength;
		uint CurrentMip;

		float3 LightWorldDir;
		float _pad;  // explicit pad to keep struct size a multiple of 16 without relying on alignas (silences C4324)
	};
	STATIC_ASSERT_ALIGNAS_16(SSSCB);

	struct alignas(16) StereoSyncCB
	{
		float FrameDim[2];
		float RcpFrameDim[2];
	};
	STATIC_ASSERT_ALIGNAS_16(StereoSyncCB);

	bool enableStereoSync = true;

	eastl::unique_ptr<ConstantBuffer> sssCB;
	eastl::unique_ptr<ConstantBuffer> stereoSyncCB;

	eastl::unique_ptr<Texture2D> texDepthMipPrefiltered[4]; // R32G32_FLOAT (linearZ, linearZ²); raw output of PrefilterDepthsCS — used for accurate per-pixel position reconstruction.
	eastl::unique_ptr<Texture2D> texDepthMip[4];            // R32G32_FLOAT; optional blurred copy of prefiltered, used for VSM sampling along the ray.
	eastl::unique_ptr<Texture2D> texShadowMip[4];       // R8_UNORM, raymarched visibility (Chebyshev applied inside ShadowsCS)
	eastl::unique_ptr<Texture2D> texShadowWork[4];      // R8_UNORM, blur/upscale working set (mip 0-3)
	eastl::unique_ptr<Texture2D> screenSpaceShadowsTexture;
	eastl::unique_ptr<Texture2D> stereoSyncCopyTex;

	winrt::com_ptr<ID3D11SamplerState> pointClampSampler;
	winrt::com_ptr<ID3D11SamplerState> linearClampSampler;

	winrt::com_ptr<ID3D11ComputeShader> prefilterDepthsCS;
	winrt::com_ptr<ID3D11ComputeShader> blurDepthCS;
	winrt::com_ptr<ID3D11ComputeShader> shadowsCS;
	winrt::com_ptr<ID3D11ComputeShader> shadowsRightCS;
	int compiledMip0SampleCount = -1;
	void CompileShadowsCS(int mip0SampleCount);
	winrt::com_ptr<ID3D11ComputeShader> upscaleCS;
	winrt::com_ptr<ID3D11ComputeShader> blurCS;
	winrt::com_ptr<ID3D11ComputeShader> stereoSyncCS;

	virtual void SetupResources() override;
	virtual void DrawSettings() override;
	virtual void ClearShaderCache() override;
	void CompileComputeShaders();

	virtual void Prepass() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	void DrawShadows();
	void DrawStereoSync();
};
