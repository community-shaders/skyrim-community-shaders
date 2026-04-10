// PhysicalGlare - Point Spread Function generation
// Generates a physically-based PSF combining CIE scattering and aperture diffraction.
// Output is written as complex numbers (real = PSF, imag = 0) centered at FFT origin (DC at [0,0]).

RWTexture2D<float2> RWTexPSF : register(u0);

cbuffer GlareCB : register(b1)
{
	float Threshold : packoffset(c0.x);
	float Intensity : packoffset(c0.y);
	float ScatterStrength : packoffset(c0.z);
	float ChromaticDispersion : packoffset(c0.w);

	int ApertureBlades : packoffset(c1.x);
	float ApertureRotation : packoffset(c1.y);
	float AdaptSpeed : packoffset(c1.z);
	float DeltaTime : packoffset(c1.w);

	uint FFTResolution : packoffset(c2.x);
	float RcpFFTResolution : packoffset(c2.y);
	float ScreenWidth : packoffset(c2.z);
	float ScreenHeight : packoffset(c2.w);

	uint ChannelIndex : packoffset(c3.x);  // 0=R, 1=G, 2=B
};

static const float PI = 3.14159265358979323846;

// CIE 1999 scattering model (simplified)
// Models the broad glow from light scattering in the eye's optical media
float CIEScatter(float r, float wavelengthScale)
{
	float k = 10.0 / max(ScatterStrength * wavelengthScale, 0.01);
	float rk = r * k;
	return 1.0 / pow(1.0 + rk * rk, 1.5);
}

// Aperture diffraction pattern
// For an N-sided regular polygon aperture, the far-field diffraction pattern
// produces 2N rays for even N, N rays for odd N
float ApertureDiffraction(float2 pos, float wavelengthScale)
{
	float r = length(pos);
	if (r < 1e-6)
		return 1.0;

	float theta = atan2(pos.y, pos.x);
	float result = 0.0;
	float invLambda = 1.0 / max(wavelengthScale * 0.001, 1e-6);

	for (int i = 0; i < ApertureBlades; i++) {
		float bladeAngle = ApertureRotation + (float(i) * PI / float(ApertureBlades));
		float sinAngle = sin(theta - bladeAngle);

		// sinc-squared pattern along each blade's perpendicular direction
		float arg = PI * r * sinAngle * invLambda;
		float sincVal = (abs(arg) < 1e-6) ? 1.0 : sin(arg) / arg;
		result += sincVal * sincVal;
	}

	return result / float(ApertureBlades);
}

// Wavelength scale factors for RGB channels (approximate)
// R ~ 650nm, G ~ 550nm, B ~ 450nm
// Normalized relative to green
float GetWavelengthScale(uint channel)
{
	float scales[3] = { 1.18, 1.0, 0.82 };  // R, G, B relative scaling
	return lerp(1.0, scales[channel], ChromaticDispersion);
}

[numthreads(8, 8, 1)] void CS_GeneratePSF(uint2 tid : SV_DispatchThreadID) {
	if (tid.x >= FFTResolution || tid.y >= FFTResolution)
		return;

	// Center the PSF at the DC position for FFT (wrap-around coordinates)
	// After FFT, DC is at [0,0], so PSF center should be at [0,0]
	// Use centered coordinates then wrap
	int2 centered = int2(tid) - int2(FFTResolution / 2, FFTResolution / 2);

	// Wrap to place DC at origin (FFT shift)
	uint2 fftPos;
	fftPos.x = (tid.x + FFTResolution / 2) % FFTResolution;
	fftPos.y = (tid.y + FFTResolution / 2) % FFTResolution;

	float2 pos = float2(centered) * RcpFFTResolution;
	float r = length(pos);

	// Determine which channel this dispatch is for via ChannelIndex in CB
	float wavelengthScale = GetWavelengthScale(ChannelIndex);

	// Combine scattering and diffraction
	float scatter = CIEScatter(r * float(FFTResolution), wavelengthScale);
	float diffraction = ApertureDiffraction(pos * float(FFTResolution) * 0.5, wavelengthScale);

	// Blend: scattering dominates at large radii, diffraction at small
	float psf = scatter * 0.3 + diffraction * 0.7;

	// Ensure non-negative
	psf = max(psf, 0.0);

	// Write PSF value centered at FFT origin
	// The normalization will be approximate (energy conservation)
	RWTexPSF[fftPos] = float2(psf, 0.0);
}
