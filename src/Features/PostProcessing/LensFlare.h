#pragma once

#include "Buffer.h"
#include "PostProcessFeature.h"

struct LensFlare : public PostProcessFeature
{
	virtual inline std::string GetType() const override { return "Lens Flare"; }
	virtual inline std::string GetDesc() const override { return "Screen-space lens flare with ghosts, halo, and glare."; }
	virtual bool WritesToMainTexture() const override { return false; }

	TextureInfo GetFlareOutput() const;

	static constexpr int NUM_GHOSTS = 8;

	struct GhostSettings
	{
		std::array<float, 4> Color = { 1.f, 1.f, 1.f, 1.f };
		float Scale = 1.f;
	};

	struct Settings
	{
		float Intensity = 0.5f;
		float ThresholdLevel = 1.0f;
		float ThresholdRange = 1.0f;
		float GhostStrength = 0.3f;
		float GhostChromaShift = 0.015f;
		float HaloStrength = 0.2f;
		float HaloRadius = 0.5f;
		float HaloWidth = 0.5f;
		float HaloCompression = 0.65f;
		float HaloChromaShift = 0.015f;
		float GlareIntensity = 0.02f;
		float GlareDivider = 60.0f;
		std::array<float, 3> GlareScale = { 1.f, 1.f, 1.f };
		std::array<float, 3> Tint = { 1.0f, 0.85f, 0.7f };
		bool GLocalMask = true;
		uint8_t pad[3]{};
		std::array<GhostSettings, NUM_GHOSTS> Ghosts = { {
			{ { { 1.0f, 0.8f, 0.4f, 1.0f } }, -1.5f },
			{ { { 1.0f, 1.0f, 0.6f, 1.0f } }, 2.5f },
			{ { { 0.8f, 0.8f, 1.0f, 1.0f } }, -5.0f },
			{ { { 0.5f, 1.0f, 0.4f, 1.0f } }, 10.0f },
			{ { { 0.5f, 0.8f, 1.0f, 1.0f } }, 0.7f },
			{ { { 0.9f, 1.0f, 0.8f, 1.0f } }, -0.4f },
			{ { { 1.0f, 0.8f, 0.4f, 1.0f } }, -0.2f },
			{ { { 0.9f, 0.7f, 0.7f, 1.0f } }, -0.1f },
		} };
	} settings;

	struct alignas(16) LensFlareCB
	{
		// Per-pass dimensions (set before each dispatch)
		float OutputWidth;
		float OutputHeight;
		float InputWidth;
		float InputHeight;

		// Threshold params
		float ThresholdLevel;
		float ThresholdRange;
		float GhostStrength;
		float GhostChromaShift;

		// Halo params
		float HaloStrength;
		float HaloRadius;
		float HaloWidth;
		float HaloCompression;

		float HaloChromaShift;
		float Intensity;
		float GlareIntensity;
		float GlareDivider;

		// Glare params
		float GlareDirection[2];
		float pad0[2]{};

		float GlareScale[3];
		int GLocalMask;

		float Tint[3];
		float pad1{};

		// Ghost colors as float4[8] = 128 bytes, matches HLSL float4 array
		float GhostColors[NUM_GHOSTS * 4];
		// Ghost scales packed as 2 × float4 = 32 bytes, matches HLSL float4[2]
		float GhostScalesPacked[8];
	};

	struct DebugSettings
	{
		int blurIterations = 1;
		bool disableThreshold = false;
		bool disableGhosts = false;
		bool disableGlare = false;
		bool disableBlur = false;
		uint8_t pad[3]{};
	} debugsettings;

	eastl::unique_ptr<ConstantBuffer> lensFlareCB = nullptr;

	eastl::unique_ptr<Texture2D> texFlare = nullptr;      // full resolution (final output)
	eastl::unique_ptr<Texture2D> texThreshold = nullptr;  // half resolution
	eastl::unique_ptr<Texture2D> texGhostHalo = nullptr;  // half resolution
	eastl::unique_ptr<Texture2D> texGlare = nullptr;      // half resolution
	eastl::unique_ptr<Texture2D> texBlurTemp = nullptr;   // quarter resolution

	winrt::com_ptr<ID3D11SamplerState> colorSampler = nullptr;
	winrt::com_ptr<ID3D11SamplerState> borderSampler = nullptr;

	winrt::com_ptr<ID3D11ComputeShader> thresholdCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> ghostHaloCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> blurDownCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> blurUpCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> glareStreakCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> mixCS = nullptr;

	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	void CompileComputeShaders();

	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json&) override;
	virtual void SaveSettings(json&) override;

	virtual void DrawSettings() override;

	virtual void Draw(TextureInfo&) override;
};

inline PostProcessFeature::TextureInfo LensFlare::GetFlareOutput() const { return { texFlare->resource.get(), texFlare->srv.get() }; }
