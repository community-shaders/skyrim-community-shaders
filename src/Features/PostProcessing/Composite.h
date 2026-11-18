#pragma once

#include "PostProcessFeature.h"

#include "Buffer.h"

struct Composite : public PostProcessFeature
{
	virtual inline std::string GetType() const override { return "Composite"; }
	virtual inline std::string GetDisplayName() const override { return T("feature.post_processing.composite.name", "Composite"); }
	virtual inline std::string GetDesc() const override
	{
		return T("feature.post_processing.composite.description",
			"Composites Bloom, Lens Flare, Physical Glare, and Auto Exposure onto the main image. "
			"Applies exposure (SceneColor * Exposure + Bloom * Exposure) before Color Grading. "
			"Automatically enabled when any contributing feature is active.");
	}
	virtual bool IsVisible() const override { return false; }
	virtual bool IsAutoEnabled() const override { return true; }
	virtual void UpdateAutoEnabled() override;
	virtual inline bool DisableInMainLoadingMenu() const override { return true; }
	virtual bool WritesToMainTexture() const override { return true; }

	eastl::unique_ptr<Texture2D> texOutput = nullptr;

	struct alignas(16) CompositeCB
	{
		float OutputSize[2];
		float FlareSize[2];
		float FlareTint[3];
		float FlareIntensity;
		float BloomRadius;
		float BloomIntensity;
		float HasPhysicalFlare;
		float HasPhysicalLens;
	};
	std::unique_ptr<ConstantBuffer> compositeCB = nullptr;
	std::unique_ptr<ConstantBuffer> emptyWaveCB;

	// Bit flags for shader permutation selection
	enum CompositeFlags : uint
	{
		NONE = 0,
		BLOOM = 1 << 0,
		FLARE = 1 << 1,
		GLARE = 1 << 2,
		EXPOSURE = 1 << 3,
		LOCAL_EXPOSURE = 1 << 4,
		PHYSICAL_LENS = 1 << 5,
		FLAG_COUNT = 64  // 2^6 combinations
	};

	// Shader permutations indexed by composite flags (0 resolves pre-exposure).
	std::array<winrt::com_ptr<ID3D11PixelShader>, CompositeFlags::FLAG_COUNT> compositeShaders = {};
	std::array<bool, CompositeFlags::FLAG_COUNT> shaderRequested = {};

	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	/// Compile the requested flag range. Physical lens variants are requested by Draw.
	void CompileRasterShaders(uint firstFlags = NONE, uint endFlags = PHYSICAL_LENS);

	virtual void RestoreDefaultSettings() override {}
	virtual void LoadSettings(json&) override {}
	virtual void SaveSettings(json&) override {}
	virtual void DrawSettings() override {}

	virtual void Draw(TextureInfo&) override;
};
