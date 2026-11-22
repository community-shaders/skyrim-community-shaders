#pragma once

#include "PostProcessFeature.h"

#include "Buffer.h"

struct ColorGrading;

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

	enum class Output : uint
	{
		Composite,
		Graded,
		Converted,
		Count
	};

	struct ConversionTarget
	{
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		ID3D11Buffer* constants = nullptr;
	};

	static constexpr uint ShaderIndex(uint flags, Output output) { return static_cast<uint>(output) * FLAG_COUNT + flags; }
	static constexpr uint kShaderCount = static_cast<uint>(Output::Count) * FLAG_COUNT;
	// Base variants are prewarmed. Fused variants compile on demand for the active effects.
	std::array<winrt::com_ptr<ID3D11PixelShader>, kShaderCount> compositeShaders = {};
	std::array<bool, kShaderCount> shaderRequested = {};

	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	/// Compile the requested flag range. Physical lens variants are requested by Draw.
	void CompileRasterShaders(uint firstFlags = NONE, uint endFlags = PHYSICAL_LENS, Output output = Output::Composite);

	virtual void RestoreDefaultSettings() override {}
	virtual void LoadSettings(json&) override {}
	virtual void SaveSettings(json&) override {}
	virtual void DrawSettings() override {}

	virtual void Draw(TextureInfo&) override;
	/// @brief Applies a requested output transform in the composite draw when the fused variant is ready.
	Output Draw(TextureInfo&, ColorGrading* grading, ConversionTarget conversion);
};
