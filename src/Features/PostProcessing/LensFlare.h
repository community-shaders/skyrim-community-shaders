#pragma once

#include "Buffer.h"
#include "PostProcessFeature.h"

struct LensFlare : public PostProcessFeature
{
	virtual inline std::string GetType() const override { return "Lens Flare"; }
	virtual inline std::string GetDesc() const override { return "Screen-space lens flare with ghosts and halo. Supports FFT bokeh convolution for physically-shaped ghosts."; }
	virtual bool WritesToMainTexture() const override { return false; }

	TextureInfo GetFlareOutput() const;

	static constexpr int NUM_GHOSTS = 8;
	static constexpr uint FFT_MIN = 128;
	static constexpr uint FFT_MAX = 1024;
	static constexpr int MAX_KERNEL_GROUPS = 8;

	enum class GhostMode : int
	{
		Fast = 0,     // Original procedural radial scaling
		Quality = 1,  // FFT convolution with bokeh shape (single kernel)
		Ultra = 2,    // FFT convolution with per-ghost kernel sizes
	};

	struct GhostSettings
	{
		std::array<float, 4> Color = { 1.f, 1.f, 1.f, 1.f };
		float Scale = 1.f;
		bool Enabled = true;
		float KernelScale = 1.0f;  // Multiplier on global KernelScale (Ultra mode only)
	};

	struct Settings
	{
		float Intensity = 0.5f;
		float ThresholdEV = 0.0f;  // EV-based threshold (converted to linear for shader)
		float ThresholdRange = 1.0f;
		float GhostStrength = 0.3f;
		float GhostChromaShift = 0.015f;
		int GhostModeInt = 0;  // 0 = Fast, 1 = Quality, 2 = Ultra
		int BokehShape = 0;    // 0 = None/Circle, 1-6 = built-in, 7+ = custom
		int FFTResolution = 256;
		float KernelScale = 0.25f;  // Fraction of FFT resolution for bokeh kernel size
		float BokehRotation = 0.f;  // Global bokeh rotation in degrees
		float HaloStrength = 0.2f;
		float HaloRadius = 0.5f;
		float HaloWidth = 0.5f;
		float HaloCompression = 0.65f;
		float HaloChromaShift = 0.015f;
		std::array<float, 3> Tint = { 1.0f, 0.85f, 0.7f };
		bool GLocalMask = true;
		uint8_t pad[3]{};
		std::array<GhostSettings, NUM_GHOSTS> Ghosts = { {
			{ { { 1.0f, 0.8f, 0.4f, 1.0f } }, -1.5f, true, 1.0f },
			{ { { 1.0f, 1.0f, 0.6f, 1.0f } }, 2.5f, true, 1.0f },
			{ { { 0.8f, 0.8f, 1.0f, 1.0f } }, -5.0f, true, 1.0f },
			{ { { 0.5f, 1.0f, 0.4f, 1.0f } }, 10.0f, true, 1.0f },
			{ { { 0.5f, 0.8f, 1.0f, 1.0f } }, 0.7f, true, 1.0f },
			{ { { 0.9f, 1.0f, 0.8f, 1.0f } }, -0.4f, true, 1.0f },
			{ { { 1.0f, 0.8f, 0.4f, 1.0f } }, -0.2f, true, 1.0f },
			{ { { 0.9f, 0.7f, 0.7f, 1.0f } }, -0.1f, true, 1.0f },
		} };
		std::string CustomBokehPath;  // User-specified custom bokeh texture path
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
		uint FFTResolution;
		int GLocalMask;

		float Tint[3];
		float KernelScale;

		float AspectRatio;
		int UseBokehTexture;
		float BokehRotation;  // radians
		float PadScale;       // 1.0 - maxKernelScale, for zero-padding

		uint ActiveGhostMask;  // bitmask of enabled ghosts for current pass
		float pad0[3]{};

		// Ghost colors as float4[8] = 128 bytes, matches HLSL float4 array
		float GhostColors[NUM_GHOSTS * 4];
		// Ghost scales packed as 2 × float4 = 32 bytes, matches HLSL float4[2]
		float GhostScalesPacked[8];
		// Per-ghost kernel scales packed as 2 × float4 (Ultra mode)
		float GhostKernelScalesPacked[8];
	};

	struct DebugSettings
	{
		int blurIterations = 1;
		bool disableThreshold = false;
		bool disableGhosts = false;
		bool disableBlur = false;
		uint8_t pad[3]{};
	} debugsettings;

	eastl::unique_ptr<ConstantBuffer> lensFlareCB = nullptr;

	eastl::unique_ptr<Texture2D> texFlare = nullptr;      // full resolution (final output)
	eastl::unique_ptr<Texture2D> texThreshold = nullptr;  // half resolution
	eastl::unique_ptr<Texture2D> texGhostHalo = nullptr;  // half resolution
	eastl::unique_ptr<Texture2D> texBlurTemp = nullptr;   // quarter resolution

	// FFT ghost pipeline textures
	eastl::unique_ptr<Texture2D> texFFT[2] = {};          // RG32F ping-pong (N×N)
	eastl::unique_ptr<Texture2D> texBokehFFT = nullptr;   // RG32F cached bokeh kernel FFT (N×N)
	eastl::unique_ptr<Texture2D> texSceneFFT = nullptr;   // RG32F cached scene FFT (N×N)
	eastl::unique_ptr<Texture2D> texFFTResult = nullptr;  // RGBA16F FFT convolution result (N×N)

	winrt::com_ptr<ID3D11SamplerState> colorSampler = nullptr;
	winrt::com_ptr<ID3D11SamplerState> borderSampler = nullptr;

	// Original pipeline shaders
	winrt::com_ptr<ID3D11ComputeShader> thresholdCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> ghostHaloCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> blurDownCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> blurUpCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> mixCS = nullptr;

	// FFT ghost pipeline shaders (self-contained in lensflare_fft.cs.hlsl)
	winrt::com_ptr<ID3D11ComputeShader> fftRowCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> fftColCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> fftRowInvCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> fftColInvCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> fftMultiplyCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> bokehPrepareCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> fftThresholdCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> fftGhostComposeCS = nullptr;

	uint currentFFTResolution = 256;
	int cachedBokehShape = -1;
	bool bokehFFTDirty = true;

	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	void CompileComputeShaders();
	void CreateFFTTextures(uint resolution);

	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json&) override;
	virtual void SaveSettings(json&) override;

	virtual void DrawSettings() override;

	virtual void Draw(TextureInfo&) override;

	virtual inline void Reset() override { bokehFFTDirty = true; }

private:
	void DispatchFFT(ID3D11ComputeShader* shader, Texture2D* input, Texture2D* output, uint resolution);
	void DrawFast(TextureInfo& inout_tex, LensFlareCB& data);
	void DrawQuality(TextureInfo& inout_tex, LensFlareCB& data);
	void PrepareBokehFFT();
};

inline PostProcessFeature::TextureInfo LensFlare::GetFlareOutput() const { return { texFlare->resource.get(), texFlare->srv.get() }; }
