#pragma once

// Implementation based on:
//   Delavennat, J. (2021). Physically-based Real-time Glare.
//   Master's thesis (LIU-ITN-TEK-A--21/068-SE), Linköping University,
//   Department of Science and Technology.
//   Supervisor: Mark E Dieckmann. Examiner: Jonas Unger.
//   Available at: https://www.diva-portal.org/smash/record.jsf?pid=diva2:1629565
//
//   Building upon:
//   Ritschel, T., Eisemann, E., Ha, I., Kim, J. D. K., & Seidel, H.-P. (2009).
//   Temporal Glare: Real-Time Dynamic Simulation of the Scattering in the Human Eye.
//   Computer Graphics Forum, 28(2), 183-192.

#include "Buffer.h"
#include "PostProcessFeature.h"

struct PhysicalGlare : public PostProcessFeature
{
	virtual inline std::string GetType() const override { return "Physical Glare"; }
	virtual inline std::string GetDesc() const override { return "Physically-based glare from eye scattering and aperture diffraction (Delavennat 2021 / Ritschel et al. 2009). Uses FFT convolution with a wavelength-dependent PSF."; }

	static constexpr uint FFT_MIN = 128;
	static constexpr uint FFT_MAX = 1024;

	struct Settings
	{
		float Threshold = 0.9f;  // Paper: 0.9 in linear normalized space
		float Intensity = 0.5f;
		int ApertureMode = 0;  // 0 = Lens (N-polygon), 1 = Pupil (circle)
		int ApertureBlades = 6;
		float ApertureRotation = 0.f;
		float ScatterStrength = 1.f;
		float AdaptSpeed = 3.f;
		int FFTResolution = 512;
		bool EnableEyelashes = false;
		int EyelashCount = 40;
		float EyelashLength = 0.4f;
		float EyelashCurvature = 0.3f;
		float FresnelExponent = 30.f;
		float ChromaticSpread = 1.f;
		float ApertureSize = 0.35f;
		int ParticleCount = 200;
		float ParticleSize = 1.5f;
		int GratingCount = 200;
		float GratingStrength = 0.5f;
	} settings;

	struct alignas(16) GlareCB
	{
		float Threshold;
		float Intensity;
		float ScatterStrength;
		uint ApertureMode;

		int ApertureBlades;
		float ApertureRotation;
		float AdaptSpeed;
		float DeltaTime;

		uint FFTResolution;
		float RcpFFTResolution;
		float ScreenWidth;
		float ScreenHeight;

		uint ChannelIndex;  // 0=R, 1=G, 2=B (for per-channel PSF generation)
		uint EnableEyelashes;
		uint EyelashCount;
		float EyelashLength;

		float EyelashCurvature;
		float FresnelExponent;
		float ChromaticSpread;
		float ApertureSize;

		uint ParticleCount;
		float ParticleSize;
		uint GratingCount;
		float GratingStrength;
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

	// Wrap-mode sampler for chromatic blur UV scaling
	winrt::com_ptr<ID3D11SamplerState> wrapSampler = nullptr;

	// Compute shaders
	winrt::com_ptr<ID3D11ComputeShader> thresholdCS = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> apertureCS = nullptr;
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
		int ApertureMode = 0;
		int ApertureBlades = 0;
		float ApertureRotation = 0.f;
		float ScatterStrength = 0.f;
		int FFTResolution = 0;
		bool EnableEyelashes = false;
		int EyelashCount = 0;
		float EyelashLength = 0.f;
		float EyelashCurvature = 0.f;
		float FresnelExponent = 0.f;
		float ChromaticSpread = 0.f;
		float ApertureSize = 0.f;
		int ParticleCount = 0;
		float ParticleSize = 0.f;
		int GratingCount = 0;
		float GratingStrength = 0.f;
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

	virtual inline void Reset() override { psfDirty = true; }

private:
	void DispatchFFT(ID3D11ComputeShader* shader, Texture2D* input, Texture2D* output, uint resolution);
	void GeneratePSF();
	bool NeedsPSFRegeneration() const;
};
