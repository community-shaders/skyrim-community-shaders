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
		float Color[4] = { 1.f, 1.f, 1.f, 1.f };
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
		float GlareScale[3] = { 1.f, 1.f, 1.f };
		float Tint[3] = { 1.0f, 0.85f, 0.7f };
		bool GLocalMask = true;
		uint8_t pad[3]{};
		GhostSettings Ghosts[NUM_GHOSTS] = {
			{ { 1.0f, 0.8f, 0.4f, 1.0f }, -1.5f },
			{ { 1.0f, 1.0f, 0.6f, 1.0f }, 2.5f },
			{ { 0.8f, 0.8f, 1.0f, 1.0f }, -5.0f },
			{ { 0.5f, 1.0f, 0.4f, 1.0f }, 10.0f },
			{ { 0.5f, 0.8f, 1.0f, 1.0f }, 0.7f },
			{ { 0.9f, 1.0f, 0.8f, 1.0f }, -0.4f },
			{ { 1.0f, 0.8f, 0.4f, 1.0f }, -0.2f },
			{ { 0.9f, 0.7f, 0.7f, 1.0f }, -0.1f },
		};
	} settings;

	struct alignas(16) LensFlareCB
	{
		// Threshold params
		float ThresholdLevel;
		float ThresholdRange;
		float ScreenWidth;
		float ScreenHeight;

		// Ghost params
		float GhostStrength;
		float GhostChromaShift;
		float HaloStrength;
		float HaloRadius;

		float HaloWidth;
		float HaloCompression;
		float HaloChromaShift;
		float Intensity;

		// Glare params
		float GlareIntensity;
		float GlareDivider;
		float GlareDirection[2];

		float GlareScale[3];
		int DownsizeScale;

		float Tint[3];
		int GLocalMask;

		// Ghost colors (RGBA) and scales packed: float4 color + float scale per ghost
		// GhostData[i] = { R, G, B, A(intensity) }
		float GhostColors[NUM_GHOSTS * 4];
		float GhostScales[NUM_GHOSTS];
		uint8_t pad[32]{};
	};

	struct DebugSettings
	{
		int downsampleTimes = 2;
		int upsampleTimes = 6;
		bool disableThreshold = false;
		bool disableGhosts = false;
		bool disableGlare = false;
		bool disableBlur = false;
		uint8_t pad[2]{};
	} debugsettings;

	eastl::unique_ptr<ConstantBuffer> lensFlareCB = nullptr;

	eastl::unique_ptr<Texture2D> texFlare = nullptr;
	eastl::unique_ptr<Texture2D> texThreshold = nullptr;
	eastl::unique_ptr<Texture2D> texGlare = nullptr;
	eastl::unique_ptr<Texture2D> texGlareScratch = nullptr;
	eastl::unique_ptr<Texture2D> texBlurScratch = nullptr;

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

inline TextureInfo LensFlare::GetFlareOutput() const { return { texFlare->resource.get(), texFlare->srv.get() }; }
