#pragma once

#include "PostProcessFeature.h"

#include "Buffer.h"

/// Local Exposure
/// Separates scene luminance into an edge-aware base layer and a detail layer.
/// Luminance analysis leaves scene color unchanged; Composite resolves the base
/// layer and applies the local adjustment with the current global exposure.
struct LocalExposure : public PostProcessFeature
{
	bool outputReady = false;
	virtual inline std::string GetType() const override { return "Local Exposure"; }
	virtual inline std::string GetDisplayName() const override { return T("feature.post_processing.local_exposure.name", "Local Exposure"); }
	virtual inline std::string GetDesc() const override
	{
		return T("feature.post_processing.local_exposure.description",
			"Local Exposure balances bright and dark regions while retaining edge detail. Its luminance analysis is combined with Auto Exposure in the Composite pass.");
	}
	virtual bool WritesToMainTexture() const override { return false; }
	virtual inline bool DisableInMainLoadingMenu() const override { return true; }

	struct Settings
	{
		float Exposure = 0.7f;
		float Strength = 1.0f;
		float HighlightContrast = 0.75f;
		float ShadowContrast = 0.8f;
		float DetailStrength = 1.0f;
		float BaseBlend = 0.6f;
		float BlurredLuminanceKernelSize = 50.0f;
		float MiddleGreyBias = 0.0f;
		float HighlightThreshold = 1.0f;
		float ShadowThreshold = 1.0f;
		float HighlightThresholdStrength = 1.0f;
		float ShadowThresholdStrength = 1.0f;
	} settings;

	// Constant buffer shared by luminance analysis and Composite.
	struct alignas(16) LocalExposureCB
	{
		float ManualExposure;
		float Strength;
		float HighlightContrast;
		float ShadowContrast;

		float DetailStrength;
		float BaseBlend;
		float BlurRadius;
		float MiddleGreyBias;

		float HighlightThreshold;
		float ShadowThreshold;
		float HighlightThresholdStrength;
		float ShadowThresholdStrength;

		uint InputWidth;
		uint InputHeight;
		uint BlurredWidth;
		uint BlurredHeight;

		float LogLuminanceMin;
		float LogLuminanceMax;
		float Padding1[2];
	};

	std::unique_ptr<ConstantBuffer> localExposureCB = nullptr;

	// Textures
	static constexpr uint s_MaxMips = 10;
	static constexpr uint s_BlurMip = 5;
	static constexpr uint s_MaxBlurRadius = 64;
	static constexpr uint s_GridDepth = 32;
	static constexpr uint s_GridTileSize = 64;

	eastl::unique_ptr<Texture2D> texLogLuminance = nullptr;
	eastl::unique_ptr<Texture3D> texLuminanceGrid = nullptr;
	eastl::unique_ptr<Texture2D> texBlurTemp = nullptr;
	eastl::unique_ptr<Texture2D> texBlurredLuminance = nullptr;

	std::array<winrt::com_ptr<ID3D11ShaderResourceView>, s_MaxMips> logLuminanceMipSRVs = {};
	std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, s_MaxMips> logLuminanceMipUAVs = {};
	uint numMips = 0;

	// Sampler
	winrt::com_ptr<ID3D11SamplerState> linearSampler = nullptr;
	winrt::com_ptr<ID3D11SamplerState> mirrorSampler = nullptr;

	// Compute shaders
	winrt::com_ptr<ID3D11ComputeShader> setupCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> downsampleCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> blurHorizontalCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> blurVerticalCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> gridCS = nullptr;

	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	void CompileComputeShaders();

	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json&) override;
	virtual void SaveSettings(json&) override;
	virtual void DrawSettings() override;

	virtual void Draw(TextureInfo&) override;

	/// Get the original scene luminance used to resolve the base in Composite.
	ID3D11ShaderResourceView* GetLogLuminanceSRV() const { return outputReady && texLogLuminance ? logLuminanceMipSRVs[0].get() : nullptr; }
	/// Get the edge-aware grid consumed by Composite.
	ID3D11ShaderResourceView* GetLuminanceGridSRV() const { return outputReady && texLuminanceGrid ? texLuminanceGrid->srv.get() : nullptr; }
	/// Get the broad base consumed by Composite.
	ID3D11ShaderResourceView* GetBlurredLuminanceSRV() const { return outputReady && texBlurredLuminance ? texBlurredLuminance->srv.get() : nullptr; }
	ID3D11Buffer* GetConstantBuffer() const { return localExposureCB ? localExposureCB->CB() : nullptr; }
};
