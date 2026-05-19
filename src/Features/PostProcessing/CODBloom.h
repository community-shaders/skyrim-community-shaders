#pragma once

#include "PostProcessFeature.h"

#include "Buffer.h"

struct CODBloom : public PostProcessFeature
{
	virtual inline std::string GetType() const override { return "COD Bloom"; }
	virtual inline std::string GetDesc() const override { return "Bloom effect used in Call of Duty: Advanced Warfare. Expect HDR linear RGB Inputs."; }
	virtual bool WritesToMainTexture() const override { return false; }

	TextureInfo GetBloomOutput() const { return { texBloom->resource.get(), texBloomMipSRVs[0].get() }; }

	constexpr static size_t s_BloomMips = 9;

	struct Settings
	{
		// bloom & lens
		float Threshold = -9.f;  // EV (0 EV = 1.0 linear)
		float UpsampleRadius = 2.f;
		float BlendFactor = .05f;
		std::array<float, s_BloomMips - 1> MipBlendFactor = { 1.f, 1.f, 1.f, 1.f, 1.f, 1.f };
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
	std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, s_BloomMips> texBloomMipUAVs = { nullptr };

	winrt::com_ptr<ID3D11ComputeShader> thresholdCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> downsampleCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> downsampleFirstMipCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> upsampleCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> compositeCS = nullptr;

	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	void CompileComputeShaders();

	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json&) override;
	virtual void SaveSettings(json&) override;

	virtual void DrawSettings() override;

	virtual void Draw(TextureInfo&) override;
};
