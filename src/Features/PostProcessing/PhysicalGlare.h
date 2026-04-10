#pragma once

#include "Buffer.h"
#include "PostProcessFeature.h"

struct PhysicalGlare : public PostProcessFeature
{
	virtual inline std::string GetType() const override { return "Physical Glare"; }
	virtual inline std::string GetDesc() const override { return "Physically-based glare from eye scattering and aperture diffraction (Ritschel et al. 2009). Uses FFT convolution with a wavelength-dependent PSF."; }
	virtual bool DrawBeforeUpscaling() const override { return true; }

	static constexpr uint FFT_MIN = 128;
	static constexpr uint FFT_MAX = 512;

	struct Settings
	{
		float ThresholdEV = -2.f;
		float Intensity = 0.5f;
		int ApertureBlades = 6;
		float ApertureRotation = 0.f;
		float ScatterStrength = 1.f;
		float ChromaticDispersion = 1.f;
		float AdaptSpeed = 3.f;
		int FFTResolution = 256;
	} settings;

	struct alignas(16) GlareCB
	{
		float Threshold;
		float Intensity;
		float ScatterStrength;
		float ChromaticDispersion;

		int ApertureBlades;
		float ApertureRotation;
		float AdaptSpeed;
		float DeltaTime;

		uint FFTResolution;
		float RcpFFTResolution;
		float ScreenWidth;
		float ScreenHeight;

		uint ChannelIndex;  // 0=R, 1=G, 2=B (for per-channel PSF generation)
		float pad[3];
	};
	eastl::unique_ptr<ConstantBuffer> glareCB = nullptr;

	// FFT work textures - RG32F (real, imaginary) per channel, ping-pong pair
	eastl::unique_ptr<Texture2D> texFFT[3][2] = {};  // [R/G/B][ping/pong]

	// PSF FFT cache (regenerated only when parameters change)
	eastl::unique_ptr<Texture2D> texPSF_FFT[3] = {};  // [R/G/B]

	// Glare result (FFT resolution)
	eastl::unique_ptr<Texture2D> texGlareResult = nullptr;
	eastl::unique_ptr<Texture2D> texGlarePrev = nullptr;

	// Full-resolution output
	eastl::unique_ptr<Texture2D> texOutput = nullptr;

	// Sampler for bilinear upsampling
	winrt::com_ptr<ID3D11SamplerState> linearSampler = nullptr;

	// Compute shaders
	winrt::com_ptr<ID3D11ComputeShader> thresholdCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> psfCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> fftRowCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> fftColCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> fftRowInvCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> fftColInvCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> multiplyCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> compositeCS = nullptr;

	// PSF parameter cache - regenerate only when changed
	struct PSFParams
	{
		int ApertureBlades = 0;
		float ApertureRotation = 0.f;
		float ScatterStrength = 0.f;
		float ChromaticDispersion = 0.f;
		int FFTResolution = 0;
	} cachedPSFParams;
	bool psfDirty = true;

	uint currentFFTResolution = 256;

	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	void CompileComputeShaders();
	void CreateFFTTextures(uint resolution);

	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json&) override;
	virtual void SaveSettings(json&) override;

	virtual void DrawSettings() override;

	virtual void Draw(TextureInfo&) override;

	virtual inline void Reset() override;

private:
	void DispatchFFT(ID3D11ComputeShader* shader, Texture2D* input, Texture2D* output, uint resolution);
	void GeneratePSF();
	bool NeedsPSFRegeneration() const;
};
