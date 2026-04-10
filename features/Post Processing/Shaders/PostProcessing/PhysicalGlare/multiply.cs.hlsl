// PhysicalGlare - Frequency domain complex multiplication
//
// Reference:
//   Delavennat, J. (2021). Physically-based Real-time Glare.
//   Master's thesis (LIU-ITN-TEK-A--21/068-SE), Linköping University.
//   https://www.diva-portal.org/smash/record.jsf?pid=diva2:1629565
//
// Multiplies the FFT of the scene with the FFT of the PSF per channel.
// Complex multiplication: (a+bi)(c+di) = (ac-bd) + (ad+bc)i

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
	float RcpFFTResolution;
	float ScreenWidth;
	float ScreenHeight;

	uint ChannelIndex;
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
