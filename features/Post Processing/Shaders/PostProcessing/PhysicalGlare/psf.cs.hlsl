// Physical Glare — Chromatic PSF generation
// Community Shaders / Post Processing — Author: Jiaye, 2026
//
// Computes per-channel point spread function via multi-wavelength diffraction
// intensity sampling.  For each of 32 spectral samples across 380–770 nm,
// the monochromatic diffraction pattern |F(u,v)|² is sampled at a
// wavelength-scaled UV offset and weighted by CIE 1931 colour matching
// functions [3].  Spectral weights are converted to the working colour
// space (Rec. 709 or ACEScg/AP1) depending on pipeline configuration.
//
// In eye (pupil) mode, eyelash streak curvature is applied via sinusoidal
// UV bending [1, section 3.1, fig. 3.7].
//
// Pipeline position: aperture → FFT → [this shader] → FFT → convolution
//
// References:
//   [1] Delavennat (2021), Physically-based Real-time Glare, LiU.
//   [2] Ritschel et al. (2009), Temporal Glare, CGF 28(2).
//   [3] Wyman, Sloan, Shirley (2013), Simple Analytic Approximations
//       to the CIE XYZ Color Matching Functions, JCGT 2(2).

Texture2D<float2> TexDiffraction : register(t0);  // Complex FFT of aperture (RG32F)
SamplerState WrapSampler : register(s0);          // Wrap-mode bilinear sampler

RWTexture2D<float2> RWTexPSF : register(u0);  // Output: per-channel PSF (real, 0)

cbuffer GlareCB : register(b1)
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
	float PaddingRatio;
	float ScreenWidth;
	float ScreenHeight;

	uint ChannelIndex;  // 0=R, 1=G, 2=B
	float FresnelExponent;
	float ChromaticSpread;
	float ApertureSize;

	float PSFSharpness;
	float PSFNoiseFloor;
	uint EnableEyelashes;
	float EyelashCurvature;

	// Rows 5-10 (used by aperture shader, not this shader)
	float4 _cbpad5;
	float4 _cbpad6;
	float4 _cbpad7;
	float4 _cbpad8;
	float4 _cbpad9;
	float4 _cbpad10;

	// Row 11: Additional optics
	float SphericalAberration;
	uint UseAP1;
	float2 _pad11;
};

static const float PI = 3.14159265358979323846;

// Number of spectral samples for chromatic blur (paper section 3.1: 32 wavelengths)
#define NUM_WAVELENGTHS 32

// ---------------------------------------------------------------------------
// CIE 1931 XYZ colour matching — Gaussian multi-lobe fit
// (Wyman, Sloan, Shirley 2013)
// ---------------------------------------------------------------------------
float3 WavelengthToXYZ(float lambda)
{
	// Use intermediate variables to prevent fxc from constant-folding
	// the entire expression in double precision (X4122 warnings).
	float dx1 = (lambda - 599.8f) / 37.9f;
	float dx2 = (lambda - 442.0f) / 16.0f;
	float dx3 = (lambda - 501.1f) / 20.4f;
	float x =
		1.056f * exp(-0.5f * dx1 * dx1) +
		0.362f * exp(-0.5f * dx2 * dx2) -
		0.065f * exp(-0.5f * dx3 * dx3);

	float dy1 = (lambda - 568.8f) / 46.9f;
	float dy2 = (lambda - 530.9f) / 16.3f;
	float y =
		0.821f * exp(-0.5f * dy1 * dy1) +
		0.286f * exp(-0.5f * dy2 * dy2);

	float dz1 = (lambda - 437.0f) / 11.8f;
	float dz2 = (lambda - 459.0f) / 26.0f;
	float z =
		1.217f * exp(-0.5f * dz1 * dz1) +
		0.681f * exp(-0.5f * dz2 * dz2);

	return float3(x, y, z);
}

// XYZ → linear sRGB (D65 white point, Rec. 709 primaries)
float3 XYZToLinearSRGB(float3 xyz)
{
	return float3(
		3.2406f * xyz.x - 1.5372f * xyz.y - 0.4986f * xyz.z,
		-0.9689f * xyz.x + 1.8758f * xyz.y + 0.0415f * xyz.z,
		0.0557f * xyz.x - 0.2040f * xyz.y + 1.0570f * xyz.z);
}

// XYZ → ACEScg / AP1 (ACES D60 white point, AP1 primaries)
// Matrix from colour-science.org via ColourSpace.h
float3 XYZToAP1(float3 xyz)
{
	return float3(
		1.64102338f * xyz.x - 0.32480329f * xyz.y - 0.2364247f * xyz.z,
		-0.66366286f * xyz.x + 1.61533159f * xyz.y + 0.01675635f * xyz.z,
		0.01172189f * xyz.x - 0.00828444f * xyz.y + 0.98839486f * xyz.z);
}

// ---------------------------------------------------------------------------
[numthreads(8, 8, 1)] void CS_ChromaticBlur(uint2 tid : SV_DispatchThreadID) {
	if (tid.x >= FFTResolution || tid.y >= FFTResolution)
		return;

	float N = float(FFTResolution);
	float rcpN = 1.0 / N;

	// ------------------------------------------------------------------
	// Multi-wavelength chromatic blur [1, section 3.1]:
	// For each spectral sample, the monochromatic diffraction pattern
	// |F(u,v)|² is sampled at UV scaled by λ/λ_ref, weighted by
	// CIE 1931 spectral → RGB conversion for the current channel.
	// Reference wavelength: 575 nm (scale = 1).
	// Longer λ → larger diffraction pattern; shorter λ → smaller.
	// ------------------------------------------------------------------
	float result = 0.0;

	// Centred frequency coordinates (DC at origin).
	// Bins [0, N/2) = positive frequencies; [N/2, N) = negative.
	float2 freq = float2(tid);
	if (freq.x >= N * 0.5)
		freq.x -= N;
	if (freq.y >= N * 0.5)
		freq.y -= N;

	for (int w = 0; w < NUM_WAVELENGTHS; w++) {
		float lambda = 380.0 + float(w) * (770.0 - 380.0) / float(NUM_WAVELENGTHS - 1);

		// UV scale: physical scaling λ/575nm [1, section 2.3].
		// ChromaticSpread controls deviation from unity:
		//   1.0 = physically correct, >1 = exaggerated, 0 = monochrome.
		float uvScale = 1.0 + (lambda / 575.0 - 1.0) * ChromaticSpread;

		// Scale frequency around DC, convert to UV.
		// Wrap-mode sampler handles out-of-range coordinates.
		float2 sampleUV = (freq / uvScale + 0.5) * rcpN;

		// Eyelash streak curvature via sinusoidal UV bending
		// [1, section 3.1, fig. 3.7]:  sin(π·x) · |x| produces a
		// symmetric arch that curves both sides of the streak.
		// Eye mode only.
		if (ApertureMode == 1 && EnableEyelashes != 0) {
			float x_norm = freq.x / (N * 0.5);                          // [-1, 1]
			float bend = sin(PI * x_norm) * x_norm * EyelashCurvature;  // symmetric arch
			sampleUV.y -= bend * 0.5;
		}

		float2 cval = TexDiffraction.SampleLevel(WrapSampler, sampleUV, 0);

		// Diffraction intensity = |F(u,v)|²
		float intensity = cval.x * cval.x + cval.y * cval.y;

		// Spectral weight for this RGB channel
		float3 xyz = WavelengthToXYZ(lambda);
		float3 rgb = UseAP1 ? max(XYZToAP1(xyz), 0.0) : max(XYZToLinearSRGB(xyz), 0.0);

		float channelWeight;
		if (ChannelIndex == 0)
			channelWeight = rgb.r;
		else if (ChannelIndex == 1)
			channelWeight = rgb.g;
		else
			channelWeight = rgb.b;

		// Normalise by NUM_WAVELENGTHS to preserve dynamic range [1, section 3.1].
		result += intensity * channelWeight / float(NUM_WAVELENGTHS);
	}

	// ------------------------------------------------------------------
	// PSF dynamic range compression [1, Table 3.9]:
	//   pow(PSFSharpness) compresses the extreme FFT dynamic range so
	//   diffraction spikes remain visible relative to the DC peak.
	//   threshold(PSFNoiseFloor) suppresses low-level numerical noise.
	//   The LDR-tuned empirical factors from [1] (×2000, ×10) are omitted;
	//   brightness is governed by the Intensity parameter instead.
	// ------------------------------------------------------------------
	result = pow(max(result, 0.0), PSFSharpness);
	result = max(result - PSFNoiseFloor, 0.0);

	RWTexPSF[tid] = float2(max(result, 0.0), 0.0);
}
