#pragma once

#include "PostProcessFeature.h"

#include "Buffer.h"

struct BloomFlareComposite : public PostProcessFeature
{
	virtual inline std::string GetType() const override { return "Bloom/Flare/Glare Composite"; }
	virtual inline std::string GetDesc() const override { return "Composites Bloom, Lens Flare, and Physical Glare results onto the main image. Automatically enabled when any of them is active."; }
	virtual bool IsVisible() const override { return false; }
	virtual bool IsAutoEnabled() const override { return true; }
	virtual void UpdateAutoEnabled() override;
	virtual bool WritesToMainTexture() const override { return true; }

	eastl::unique_ptr<Texture2D> texOutput = nullptr;

	// Bit flags for shader permutation selection
	enum CompositeFlags : uint
	{
		NONE = 0,
		BLOOM = 1 << 0,
		FLARE = 1 << 1,
		GLARE = 1 << 2,
		FLAG_COUNT = 8  // 2^3 combinations
	};

	// Shader permutations indexed by composite flags (index 0 unused)
	std::array<winrt::com_ptr<ID3D11ComputeShader>, CompositeFlags::FLAG_COUNT> compositeShaders = {};

	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	void CompileComputeShaders();

	virtual void RestoreDefaultSettings() override {}
	virtual void LoadSettings(json&) override {}
	virtual void SaveSettings(json&) override {}
	virtual void DrawSettings() override {}

	virtual void Draw(TextureInfo&) override;
};
