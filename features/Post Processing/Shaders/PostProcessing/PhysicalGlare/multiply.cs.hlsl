// Physical Glare — Frequency-domain complex multiplication
// Community Shaders / Post Processing — Author: Jiaye, 2026
//
// Element-wise complex multiplication of the scene FFT with the PSF FFT
// for one colour channel: (a+bi)(c+di) = (ac−bd) + (ad+bc)i.
// Implements the convolution theorem: IFFT(F·G) = f∗g [1, section 2.1].
//
// References:
//   [1] Delavennat (2021), Physically-based Real-time Glare, LiU.

Texture2D<float2> TexSceneFFT : register(t0);  // Scene FFT (one channel)
Texture2D<float2> TexPSF_FFT : register(t1);   // PSF FFT (one channel)

RWTexture2D<float2> RWTexResult : register(u0);  // Output (one channel)

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

	uint ChannelIndex;
	float FresnelExponent;
	float ChromaticSpread;
	float ApertureSize;

	float PSFSharpness;
	float PSFNoiseFloor;
	uint EnableEyelashes;
	float EyelashCurvature;
};

[numthreads(8, 8, 1)] void CS_Multiply(uint2 tid : SV_DispatchThreadID) {
	if (tid.x >= FFTResolution || tid.y >= FFTResolution)
		return;

	float2 scene = TexSceneFFT[tid];
	float2 psf = TexPSF_FFT[tid];

	// DC component F[0,0] of PSF FFT equals its spatial-domain sum.
	// Dividing by it normalises the PSF to unit energy so the convolution
	// preserves the thresholded scene's brightness level.
	float psfDC = max(TexPSF_FFT[uint2(0, 0)].x, 1e-6);

	// Complex multiplication: (a+bi)(c+di) = (ac-bd) + (ad+bc)i
	float2 result;
	result.x = scene.x * psf.x - scene.y * psf.y;
	result.y = scene.x * psf.y + scene.y * psf.x;

	result /= psfDC;

	RWTexResult[tid] = result;
}
