#pragma once

#include "PostProcessFeature.h"

#include "Buffer.h"

struct Composite : public PostProcessFeature
{
	virtual inline std::string GetType() const override { return "Composite"; }
	virtual inline std::string GetDesc() const override { return "Composites Bloom, Lens Flare, Physical Glare, and Auto Exposure onto the main image. "
																 "Applies exposure (SceneColor * Exposure + Bloom * Exposure) before Color Grading. "
																 "Automatically enabled when any contributing feature is active."; }
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
		EXPOSURE = 1 << 3,
		FLAG_COUNT = 16  // 2^4 combinations
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
