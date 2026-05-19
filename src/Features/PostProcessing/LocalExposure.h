#pragma once

#include "PostProcessFeature.h"

#include "Buffer.h"

/// Local Exposure
/// Generates a per-pixel exposure multiplier using exposure-fusion local
/// tonemapping. Runs before Auto Exposure and is consumed by Composite.
/// The main color texture is not modified by this pass.
///
/// Reference:
///   https://bartwronski.com/2022/02/28/exposure-fusion-local-tonemapping-for-real-time-rendering/
///
/// Algorithm:
///   1. Normalize raw HDR input with global exposure when available
///   2. Compute highlight, midtone, and shadow exposure candidates
///   3. Build luminance and weight pyramids
///   4. Reconstruct the fused result across the configured mip range
///   5. Guided-upsample the fused result into a full-resolution multiplier
struct LocalExposure : public PostProcessFeature
{
	virtual inline std::string GetType() const override { return "Local Exposure"; }
	virtual inline std::string GetDesc() const override
	{
		return "Local Exposure that brightens shadows and compresses highlights "
			   "based on local neighborhood luminance. Runs before Auto Exposure and is "
			   "applied in the Composite pass.";
	}
	virtual bool WritesToMainTexture() const override { return false; }
	virtual bool SupportsVR() const override { return true; }
	virtual inline bool DisableInMainLoadingMenu() const override { return true; }

	struct Settings
	{
		float Exposure = 0.7f;                 // Manual input normalization when Auto Exposure is unavailable
		float Shadows = 1.5f;                  // Shadow recovery EV
		float Highlights = 2.0f;               // Highlight recovery EV
		float ExposurePreferenceSigma = 5.0f;  // Exposure selection sharpness
		uint Mip = 6;                          // Coarsest pyramid level used for reconstruction
		uint DisplayMip = 2;                   // Finest reconstructed level before guided upsample
		bool BoostLocalContrast = false;       // Weight Laplacian bands by local contrast
	} settings;

	// Constant buffer for the compute shader
	struct alignas(16) LocalExposureCB
	{
		float ManualExposure;
		float HighlightExposure;
		float ShadowExposure;
		float ExposurePreferenceSigmaSq;
		uint InputWidth;
		uint InputHeight;
		uint MipLevel;
		uint DisplayMip;
		uint CurrentMip;
		uint HasCoarserMip;
		uint BoostLocalContrast;
		uint UseGlobalExposure;
		float ExposureCompensation;
		float AdaptationMin;
		float AdaptationMax;
		float DarkThreshold;
	};
	std::unique_ptr<ConstantBuffer> localExposureCB = nullptr;

	// Textures
	static constexpr uint s_MaxMips = 10;

	eastl::unique_ptr<Texture2D> texExposures = nullptr;  // RGBA16F, RGB = highlights/midtones/shadows
	eastl::unique_ptr<Texture2D> texWeights = nullptr;    // RGBA16F, normalized synthetic exposure weights
	eastl::unique_ptr<Texture2D> texAssemble = nullptr;   // R16F, reconstructed fusion result

	std::array<winrt::com_ptr<ID3D11ShaderResourceView>, s_MaxMips> exposureMipSRVs = {};
	std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, s_MaxMips> exposureMipUAVs = {};
	std::array<winrt::com_ptr<ID3D11ShaderResourceView>, s_MaxMips> weightMipSRVs = {};
	std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, s_MaxMips> weightMipUAVs = {};
	std::array<winrt::com_ptr<ID3D11ShaderResourceView>, s_MaxMips> assembleMipSRVs = {};
	std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, s_MaxMips> assembleMipUAVs = {};
	uint numMips = 0;

	eastl::unique_ptr<Texture2D> texExposure = nullptr;  // R16F, full res - the output exposure map

	// Sampler
	winrt::com_ptr<ID3D11SamplerState> linearSampler = nullptr;

	// Compute shaders
	winrt::com_ptr<ID3D11ComputeShader> setupCS = nullptr;       // Compute synthetic exposure lums and weights
	winrt::com_ptr<ID3D11ComputeShader> downsampleCS = nullptr;  // Iterative mip downsample
	winrt::com_ptr<ID3D11ComputeShader> blendCS = nullptr;       // Gaussian/Laplacian exposure-fusion reconstruction
	winrt::com_ptr<ID3D11ComputeShader> computeExpCS = nullptr;  // Guided upsample to full-res multiplier

	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	void CompileComputeShaders();

	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json&) override;
	virtual void SaveSettings(json&) override;
	virtual void DrawSettings() override;

	virtual void Draw(TextureInfo&) override;

	/// Get the local exposure texture SRV (R16F, full resolution, per-pixel multiplier).
	/// Consumed by the Composite pass.
	ID3D11ShaderResourceView* GetExposureSRV() const { return texExposure ? texExposure->srv.get() : nullptr; }
};
