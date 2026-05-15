#pragma once

#include "PostProcessFeature.h"

#include "Buffer.h"

/// Local Exposure
/// Generates a per-pixel exposure multiplier texture by comparing each pixel's luminance
/// to its local neighborhood average (from a low-res blurred luminance mip).
/// Runs BEFORE Auto Exposure and is consumed by the Composite pass.
/// Does NOT affect the main texture directly - only produces an exposure map.
///
/// Based on the local tonemapping / exposure fusion technique by Bart Wronski:
///   https://bartwronski.com/2022/02/28/exposure-fusion-local-tonemapping-for-real-time-rendering/
///
/// Algorithm:
///   1. Compute log-luminance of the scene at reduced resolution (1/4 x 1/4)
///   2. Build a mip chain via iterative downsampling (Gaussian blur approximation)
///   3. Compute per-pixel local exposure by comparing pixel luminance to blurred average
///   4. Bilateral upsample the result to full resolution for halo-free application
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
		float HighlightContrast = 0.8f;  // How much to compress highlights (0 = no effect, 1 = full)
		float ShadowContrast = 0.8f;     // How much to boost shadows (0 = no effect, 1 = full)
		float DetailStrength = 1.0f;     // Overall effect intensity multiplier
		float BilateralSigma = 2.0f;     // Edge-aware sigma in EV (lower = more edge-aware, less halos)
		uint MipBias = 5;                // Which mip level to use as "local average" (higher = larger radius)
	} settings;

	// Constant buffer for the compute shader
	struct alignas(16) LocalExposureCB
	{
		float HighlightContrast;
		float ShadowContrast;
		float DetailStrength;
		float BilateralSigma;
		uint InputWidth;
		uint InputHeight;
		uint LowResWidth;
		uint LowResHeight;
		uint MipLevel;
		float pad[3];
	};
	std::unique_ptr<ConstantBuffer> localExposureCB = nullptr;

	// Textures
	static constexpr uint s_MaxMips = 8;

	eastl::unique_ptr<Texture2D> texLuminance = nullptr;  // R16F, 1/4 res, with mip chain
	std::array<winrt::com_ptr<ID3D11ShaderResourceView>, s_MaxMips> lumMipSRVs = {};
	std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, s_MaxMips> lumMipUAVs = {};
	uint numMips = 0;

	eastl::unique_ptr<Texture2D> texExposure = nullptr;  // R16F, full res - the output exposure map

	// Sampler
	winrt::com_ptr<ID3D11SamplerState> linearSampler = nullptr;

	// Compute shaders
	winrt::com_ptr<ID3D11ComputeShader> luminanceCS = nullptr;   // Downsample scene to 1/4 res log-luminance
	winrt::com_ptr<ID3D11ComputeShader> downsampleCS = nullptr;  // Iterative mip downsample
	winrt::com_ptr<ID3D11ComputeShader> computeExpCS = nullptr;  // Compute local exposure multiplier (bilateral upsample + exposure calc)

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
