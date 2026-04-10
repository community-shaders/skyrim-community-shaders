// PhysicalGlare - Frequency domain complex multiplication
// Multiplies the FFT of the scene with the FFT of the PSF per channel.
// Complex multiplication: (a+bi)(c+di) = (ac-bd) + (ad+bc)i

Texture2D<float2> TexSceneFFT : register(t0);  // Scene FFT (one channel)
Texture2D<float2> TexPSF_FFT : register(t1);   // PSF FFT (one channel)

RWTexture2D<float2> RWTexResult : register(u0);  // Output (one channel)

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

	uint ChannelIndex : packoffset(c3.x);
};

[numthreads(8, 8, 1)] void CS_Multiply(uint2 tid : SV_DispatchThreadID) {
	if (tid.x >= FFTResolution || tid.y >= FFTResolution)
		return;

	float2 scene = TexSceneFFT[tid];
	float2 psf = TexPSF_FFT[tid];

	// Complex multiplication: (a+bi)(c+di) = (ac-bd) + (ad+bc)i
	float2 result;
	result.x = scene.x * psf.x - scene.y * psf.y;
	result.y = scene.x * psf.y + scene.y * psf.x;

	RWTexResult[tid] = result;
}
