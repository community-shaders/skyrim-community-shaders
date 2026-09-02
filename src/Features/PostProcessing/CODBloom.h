#pragma once

#include "PostProcessFeature.h"

#include "Buffer.h"

struct CODBloom : public PostProcessFeature
{
	virtual inline std::string GetType() const override { return "COD Bloom"; }
	virtual inline std::string GetDisplayName() const override { return T("feature.post_processing.codbloom.name", "COD Bloom"); }
	virtual inline std::string GetDesc() const override { return T("feature.post_processing.codbloom.description", "Bloom effect used in Call of Duty: Advanced Warfare. Expect HDR linear RGB inputs."); }
	virtual bool WritesToMainTexture() const override { return false; }

	TextureInfo GetBloomOutput() const { return { texBloom->resource.get(), texBloomMipSRVs[0].get() }; }

	constexpr static size_t s_BloomMips = 9;

	struct Settings
	{
		// bloom & lens
		float Threshold = 3.f;  // EV100 (0 EV100 = 0.125 linear luminance)
		float UpsampleRadius = 2.f;
		float BlendFactor = .01f;
		std::array<float, s_BloomMips - 1> MipBlendFactor = { 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f };
	} settings;

	struct alignas(16) BloomCB
	{
		// threshold
		float Threshold;
		// upsample
		float UpsampleRadius;
		float UpsampleMult;  // in composite: bloom mult
		float CurrentMipMult;
	};
	std::unique_ptr<ConstantBuffer> bloomCB = nullptr;

	winrt::com_ptr<ID3D11SamplerState> colorSampler = nullptr;

	std::unique_ptr<Texture2D> texBloom = nullptr;
	std::array<winrt::com_ptr<ID3D11ShaderResourceView>, s_BloomMips> texBloomMipSRVs = { nullptr };
	std::array<winrt::com_ptr<ID3D11RenderTargetView>, s_BloomMips> texBloomMipRTVs = { nullptr };

	/// Blend state for the upsample-accumulate passes: PS emits the scaled
	/// upsample contribution, ROP adds it to the destination mip multiplied by
	/// CurrentMipMult (SrcBlend = ONE, DestBlend = BLEND_FACTOR).
	winrt::com_ptr<ID3D11BlendState> upsampleBlendState = nullptr;

	winrt::com_ptr<ID3D11PixelShader> thresholdPS = nullptr;
	winrt::com_ptr<ID3D11PixelShader> downsamplePS = nullptr;
	winrt::com_ptr<ID3D11PixelShader> downsampleFirstMipPS = nullptr;
	winrt::com_ptr<ID3D11PixelShader> upsamplePS = nullptr;
	winrt::com_ptr<ID3D11PixelShader> compositePS = nullptr;

	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	void CompileRasterShaders();

	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json&) override;
	virtual void SaveSettings(json&) override;

	virtual void DrawSettings() override;

	virtual void Draw(TextureInfo&) override;
};
