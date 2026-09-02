// Physical Glare — Frequency-domain complex multiplication
// Community Shaders / Post Processing — Author: Jiaye, 2026
//
// Element-wise complex multiplication of the scene FFT with the PSF FFT
// for one colour channel: (a+bi)(c+di) = (ac−bd) + (ad+bc)i.
// Implements the convolution theorem: IFFT(F·G) = f∗g [1, section 2.1].
//
// References:
//   [1] Delavennat (2021), Physically-based Real-time Glare, LiU.

Texture2D<float2> TexSceneFFT_R : register(t0);
Texture2D<float2> TexPSF_FFT_R : register(t1);
Texture2D<float2> TexSceneFFT_G : register(t2);
Texture2D<float2> TexPSF_FFT_G : register(t3);
Texture2D<float2> TexSceneFFT_B : register(t4);
Texture2D<float2> TexPSF_FFT_B : register(t5);

RWTexture2D<float2> RWTexResult_R : register(u0);
RWTexture2D<float2> RWTexResult_G : register(u1);
RWTexture2D<float2> RWTexResult_B : register(u2);

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

float2 MultiplyAndNormalise(float2 scene, float2 psf, float psfDC)
{
	// DC component F[0,0] of PSF FFT equals its spatial-domain sum.
	// Dividing by it normalises the PSF to unit energy so the convolution
	// preserves the thresholded scene's brightness level.
	float2 result;
	result.x = scene.x * psf.x - scene.y * psf.y;
	result.y = scene.x * psf.y + scene.y * psf.x;
	return result / max(psfDC, 1e-6);
}

[numthreads(8, 8, 1)] void CS_Multiply(uint2 tid : SV_DispatchThreadID) {
	if (tid.x >= FFTResolution || tid.y >= FFTResolution)
		return;

	RWTexResult_R[tid] = MultiplyAndNormalise(TexSceneFFT_R[tid], TexPSF_FFT_R[tid], TexPSF_FFT_R[uint2(0, 0)].x);
	RWTexResult_G[tid] = MultiplyAndNormalise(TexSceneFFT_G[tid], TexPSF_FFT_G[tid], TexPSF_FFT_G[uint2(0, 0)].x);
	RWTexResult_B[tid] = MultiplyAndNormalise(TexSceneFFT_B[tid], TexPSF_FFT_B[tid], TexPSF_FFT_B[uint2(0, 0)].x);
}
