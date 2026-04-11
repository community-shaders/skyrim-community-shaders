#pragma once

// Physical Glare — Community Shaders / Post Processing
// Author: Jiaye, 2026
//
// Physically-based glare via FFT convolution with a wavelength-dependent
// point spread function (PSF).  Supports dual aperture modes (eye/lens)
// with chromatic dispersion, wavefront aberrations, and anatomical /
// mechanical optical features.
//
// Core pipeline inspired by:
//   [1] Delavennat, J. (2021). Physically-based Real-time Glare.
//       Master's thesis (LIU-ITN-TEK-A--21/068-SE), Linköping University.
//       https://www.diva-portal.org/smash/record.jsf?pid=diva2:1629565
//   [2] Ritschel, T., Eisemann, E., Ha, I., Kim, J. D. K., & Seidel, H.-P.
//       (2009). Temporal Glare: Real-Time Dynamic Simulation of the
//       Scattering in the Human Eye. Computer Graphics Forum 28(2), 183-192.
//
// Extensions beyond [1] and [2]:
//   - Dual aperture mode: camera lens (N-polygon) and anatomical eye (pupil)
//   - Seidel spherical aberration (r^4 wavefront error)
//   - Tear film phase harmonics with temporal animation
//   - Crystalline lens suture lines (Y-pattern phase modulation)
//   - Fiber cell starburst (radial phase grating)
//   - Lens-mode features: dust particles, blade roughness, surface scratches
//   - CIE 1931 spectral weighting with AP1/ACEScg wide-gamut support
//   - Energy-conserving composite (bright subtraction + glare redistribution)
//   - Catmull-Rom bicubic upsampling from FFT resolution
//   - Configurable zero-padding ratio for wrap-around control

#include "Buffer.h"
#include "PostProcessFeature.h"

struct PhysicalGlare : public PostProcessFeature
{
	virtual inline std::string GetType() const override { return "Physical Glare"; }
	virtual inline std::string GetDesc() const override { return "Physically-based glare from aperture diffraction and ocular scattering. FFT convolution with a wavelength-dependent PSF, dual eye/lens modes, wavefront aberrations, and wide-gamut chromatic dispersion."; }

	static constexpr uint FFT_MIN = 128;
	static constexpr uint FFT_MAX = 1024;

	struct Settings
	{
		// --- Core ---
		float Threshold = 0.9f;  // Paper: 0.9 in linear normalized space
		float Intensity = 0.5f;
		int ApertureMode = 0;  // 0 = Lens (N-polygon), 1 = Pupil (circle)
		int ApertureBlades = 6;
		float ApertureRotation = 0.f;
		float ScatterStrength = 1.f;
		float AdaptSpeed = 3.f;
		int FFTResolution = 512;
		float FresnelExponent = 30.f;
		float ChromaticSpread = 1.f;
		float ApertureSize = 0.35f;
		float SphericalAberration = 0.f;  // Seidel r^4 wavefront error from lens curvature

		// --- PSF shaping ---
		float PSFSharpness = 0.45f;    // pow() exponent (paper Table 3.9: 0.45). Higher = concentrated.
		float PSFNoiseFloor = 0.001f;  // noise floor (paper: 0.001). Higher = cleaner wings.
		float PaddingRatio = 0.1f;     // Zero-padding per side. 0.25=paper(50% effective), 0.1=80%, 0=100%.

		// --- Eye mode ---
		bool EnableEyelashes = false;
		int EyelashCount = 40;
		float EyelashLength = 0.4f;
		float EyelashCurvature = 0.3f;
		int ParticleCount = 200;
		float ParticleSize = 1.5f;
		int GratingCount = 200;
		float GratingStrength = 0.5f;
		float TearFilmStrength = 0.f;
		float TearFilmSpeed = 2.f;
		int TearFilmComplexity = 8;
		int SutureBranches = 3;
		float SutureStrength = 0.5f;
		float SutureWidth = 2.f;
		int StarburstCount = 48;
		float StarburstStrength = 0.8f;
		float StarburstIrregularity = 0.3f;

		// --- Lens mode ---
		int DustCount = 100;
		float DustSize = 1.5f;
		int BladeRoughnessFreq = 20;
		float BladeRoughnessAmp = 0.3f;
		int ScratchCount = 5;
		float ScratchOpacity = 0.3f;
		float ScratchLength = 0.8f;
		float ScratchWidth = 1.5f;
	} settings;

	struct alignas(16) GlareCB
	{
		// --- Row 0: Core controls ---
		float Threshold;
		float Intensity;
		float ScatterStrength;
		uint ApertureMode;

		// --- Row 1: Geometry + temporal ---
		int ApertureBlades;
		float ApertureRotation;
		float AdaptSpeed;
		float DeltaTime;

		// --- Row 2: Resolution + padding ---
		uint FFTResolution;
		float PaddingRatio;
		float ScreenWidth;
		float ScreenHeight;

		// --- Row 3: Channel + optical ---
		uint ChannelIndex;
		float FresnelExponent;
		float ChromaticSpread;
		float ApertureSize;

		// --- Row 4: PSF shaping + eyelash UV bending ---
		float PSFSharpness;
		float PSFNoiseFloor;
		uint EnableEyelashes;
		float EyelashCurvature;

		// --- (threshold/psf/fft/multiply/composite read up to here) ---

		// --- Row 5: Eye - eyelash geometry + scatter particles ---

		uint EyelashCount;
		float EyelashLength;
		uint ParticleCount;
		float ParticleSize;

		// --- Row 6: Eye - gratings + tear film ---
		uint GratingCount;
		float GratingStrength;
		float TearFilmStrength;
		float TearFilmSpeed;

		// --- Row 7: Eye - tear film cont. + sutures ---
		uint TearFilmComplexity;
		float TearFilmTime;
		uint SutureBranches;
		float SutureStrength;

		// --- Row 8: Eye - suture width + starburst ---
		float SutureWidth;
		uint StarburstCount;
		float StarburstStrength;
		float StarburstIrregularity;

		// --- Row 9: Lens - dust + blade roughness ---
		uint DustCount;
		float DustSize;
		uint BladeRoughnessFreq;
		float BladeRoughnessAmp;

		// --- Row 10: Lens - scratches ---
		uint ScratchCount;
		float ScratchOpacity;
		float ScratchLength;
		float ScratchWidth;

		// --- Row 11: Additional optics ---
		float SphericalAberration;
		uint UseAP1;
		float _pad11[2];
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
		float FresnelExponent = 0.f;
		float ChromaticSpread = 0.f;
		float ApertureSize = 0.f;
		float PSFSharpness = 0.f;
		float PSFNoiseFloor = 0.f;
		// Eye mode
		bool EnableEyelashes = false;
		int EyelashCount = 0;
		float EyelashLength = 0.f;
		float EyelashCurvature = 0.f;
		int ParticleCount = 0;
		float ParticleSize = 0.f;
		int GratingCount = 0;
		float GratingStrength = 0.f;
		float TearFilmStrength = 0.f;
		float TearFilmSpeed = 0.f;
		int TearFilmComplexity = 0;
		int SutureBranches = 0;
		float SutureStrength = 0.f;
		float SutureWidth = 0.f;
		int StarburstCount = 0;
		float StarburstStrength = 0.f;
		float StarburstIrregularity = 0.f;
		// Lens mode
		int DustCount = 0;
		float DustSize = 0.f;
		int BladeRoughnessFreq = 0;
		float BladeRoughnessAmp = 0.f;
		int ScratchCount = 0;
		float ScratchOpacity = 0.f;
		float ScratchLength = 0.f;
		float ScratchWidth = 0.f;
		float SphericalAberration = 0.f;
		bool UseAP1 = false;
	} cachedPSFParams;
	bool psfDirty = true;
	float tearFilmTimeAccum = 0.f;

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
