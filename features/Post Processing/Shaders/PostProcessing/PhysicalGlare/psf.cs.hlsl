// PhysicalGlare - Chromatic Blur PSF generation
//
// Reference:
//   Delavennat, J. (2021). Physically-based Real-time Glare.
//   Master's thesis (LIU-ITN-TEK-A--21/068-SE), Linköping University.
//   https://www.diva-portal.org/smash/record.jsf?pid=diva2:1629565
//
// Reads the complex FFT of the aperture (diffraction amplitude), computes |F|² intensity,
// and performs multi-wavelength UV-scaled layering (chromatic blur) to produce a
// per-channel PSF with physically-correct color dispersion.
//
// Pipeline: aperture → FFT → *this shader* → FFT → frequency-domain convolution
//
// The chromatic blur layers copies of the monochromatic diffraction pattern at
// wavelength-dependent UV scales, weighted by CIE spectral→RGB conversion,
// producing the characteristic rainbow fringes of real eye glare.

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
	float RcpFFTResolution;
	float ScreenWidth;
	float ScreenHeight;

	uint ChannelIndex;  // 0=R, 1=G, 2=B
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

static const float PI = 3.14159265358979323846;

// Number of spectral samples for chromatic blur (paper section 3.1: 32 wavelengths)
#define NUM_WAVELENGTHS 32

// ---------------------------------------------------------------------------
// CIE 1931 XYZ colour matching — Gaussian multi-lobe fit
// (Wyman, Sloan, Shirley 2013)
// ---------------------------------------------------------------------------
float3 WavelengthToXYZ(float lambda)
{
	float x =
		1.056 * exp(-0.5 * pow((lambda - 599.8) / 37.9, 2.0)) +
		0.362 * exp(-0.5 * pow((lambda - 442.0) / 16.0, 2.0)) -
		0.065 * exp(-0.5 * pow((lambda - 501.1) / 20.4, 2.0));
	float y =
		0.821 * exp(-0.5 * pow((lambda - 568.8) / 46.9, 2.0)) +
		0.286 * exp(-0.5 * pow((lambda - 530.9) / 16.3, 2.0));
	float z =
		1.217 * exp(-0.5 * pow((lambda - 437.0) / 11.8, 2.0)) +
		0.681 * exp(-0.5 * pow((lambda - 459.0) / 26.0, 2.0));
	return float3(x, y, z);
}

// XYZ → linear sRGB (D65 white point, Rec. 709 primaries)
float3 XYZToLinearSRGB(float3 xyz)
{
	return float3(
		3.2406 * xyz.x - 1.5372 * xyz.y - 0.4986 * xyz.z,
		-0.9689 * xyz.x + 1.8758 * xyz.y + 0.0415 * xyz.z,
		0.0557 * xyz.x - 0.2040 * xyz.y + 1.0570 * xyz.z);
}

// ---------------------------------------------------------------------------
[numthreads(8, 8, 1)] void CS_ChromaticBlur(uint2 tid : SV_DispatchThreadID) {
	if (tid.x >= FFTResolution || tid.y >= FFTResolution)
		return;

	float N = float(FFTResolution);
	float rcpN = RcpFFTResolution;

	// ------------------------------------------------------------------
	// Chromatic blur: for each wavelength, sample the monochromatic
	// diffraction pattern (|F|²) at UV scaled by λ/λ_ref, weighted by
	// the CIE spectral→RGB conversion for the current channel.
	// Reference wavelength: 575 nm (middle of visible spectrum, scale = 1)
	// Longer λ (red) → larger diffraction pattern → divide UV by scale > 1
	// Shorter λ (blue) → smaller pattern → divide UV by scale < 1
	// ------------------------------------------------------------------
	float result = 0.0;

	// Convert pixel to centred frequency (DC at origin).
	// Bins 0..N/2-1 are positive frequencies; N/2..N-1 are negative.
	float2 freq = float2(tid);
	if (freq.x >= N * 0.5)
		freq.x -= N;
	if (freq.y >= N * 0.5)
		freq.y -= N;

	for (int w = 0; w < NUM_WAVELENGTHS; w++) {
		float lambda = 380.0 + float(w) * (770.0 - 380.0) / float(NUM_WAVELENGTHS - 1);

		// UV scale: longer wavelength → larger diffraction pattern
		// Physical scaling per paper section 2.3: λ/575nm
		// ChromaticSpread multiplies the deviation from unity:
		//   1.0 = physical, >1 = more rainbow, 0 = monochrome
		float uvScale = 1.0 + (lambda / 575.0 - 1.0) * ChromaticSpread;

		// Scale frequency around DC, then convert back to UV.
		// WRAP sampler handles the resulting negative UVs correctly.
		float2 sampleUV = (freq / uvScale + 0.5) * rcpN;

		// UV bending for eyelash streak curvature (paper step 4, fig 3.7):
		// "use y = sin(x) for x in [-1;1], and then add that value as a
		//  vertical offset to the UVs based on how far from the center
		//  we are in the x coordinate."
		// sin(PI*x) maps [-1,1] to a full sine period; multiplied by |x|
		// (distance from center) gives a symmetric arch (even function)
		// that curves both sides of the streak the same way.
		if (EnableEyelashes != 0) {
			float x_norm = freq.x / (N * 0.5);                          // [-1, 1]
			float bend = sin(PI * x_norm) * x_norm * EyelashCurvature;  // symmetric arch
			sampleUV.y -= bend * 0.5;
		}

		float2 cval = TexDiffraction.SampleLevel(WrapSampler, sampleUV, 0);

		// Diffraction intensity = |F(u,v)|²
		float intensity = cval.x * cval.x + cval.y * cval.y;

		// Spectral weight for this RGB channel
		float3 xyz = WavelengthToXYZ(lambda);
		float3 rgb = max(XYZToLinearSRGB(xyz), 0.0);

		float channelWeight;
		if (ChannelIndex == 0)
			channelWeight = rgb.r;
		else if (ChannelIndex == 1)
			channelWeight = rgb.g;
		else
			channelWeight = rgb.b;

		// Paper: divide each layer by NUM_WAVELENGTHS to keep output in dynamic range
		result += intensity * channelWeight / float(NUM_WAVELENGTHS);
	}

	// ------------------------------------------------------------------
	// PSF shape compression (paper Table 3.9):
	//   pow(0.45) compresses extreme FFT dynamic range so diffraction
	//   spikes are visible relative to the DC peak.  Without this the
	//   convolution produces only featureless bloom.
	//   threshold(0.0001) removes low-level FFT numerical noise.
	//   Paper's factor(2000) and composite ×10 are LDR-tuned empirical
	//   scalars — omitted here; brightness controlled by Intensity.
	// ------------------------------------------------------------------
	result = pow(max(result, 0.0), 0.45);
	result = max(result - 0.0001, 0.0);

	RWTexPSF[tid] = float2(max(result, 0.0), 0.0);
}
