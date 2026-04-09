#pragma once

#include "PostProcessFeature.h"

#include "Buffer.h"

struct BloomFlareComposite : public PostProcessFeature
{
	virtual inline std::string GetType() const override { return "Bloom/Flare Composite"; }
	virtual inline std::string GetDesc() const override { return "Composites Bloom and Lens Flare results onto the main image. Automatically enabled when either Bloom or Lens Flare is active."; }
	virtual bool IsVisible() const override { return false; }
	virtual bool IsAutoEnabled() const override { return true; }
	virtual void UpdateAutoEnabled() override;
	virtual bool WritesToMainTexture() const override { return true; }

	eastl::unique_ptr<Texture2D> texOutput = nullptr;

	winrt::com_ptr<ID3D11ComputeShader> compositeCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> compositeBloomOnlyCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> compositeFlareOnlyCS = nullptr;

	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	void CompileComputeShaders();

	virtual void RestoreDefaultSettings() override {}
	virtual void LoadSettings(json&) override {}
	virtual void SaveSettings(json&) override {}
	virtual void DrawSettings() override {}

	virtual void Draw(TextureInfo&) override;
};
