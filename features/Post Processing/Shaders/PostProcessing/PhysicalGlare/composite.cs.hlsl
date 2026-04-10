// PhysicalGlare - Composite shader
//
// Reference:
//   Delavennat, J. (2021). Physically-based Real-time Glare.
//   Master's thesis (LIU-ITN-TEK-A--21/068-SE), Linköping University.
//   https://www.diva-portal.org/smash/record.jsf?pid=diva2:1629565
//
// Extracts glare from IFFT results, bilinearly upsamples from FFT resolution,
// and additively composites onto the scene.  Runs at full screen resolution.
// Reads only from the center [N/4, 3N/4) region where the zero-padded scene was placed.

Texture2D<float4> TexScene : register(t0);   // Original scene (full resolution)
Texture2D<float2> TexIFFT_R : register(t1);  // IFFT result, R channel (FFT resolution)
Texture2D<float2> TexIFFT_G : register(t2);  // IFFT result, G channel (FFT resolution)
Texture2D<float2> TexIFFT_B : register(t3);  // IFFT result, B channel (FFT resolution)

RWTexture2D<float4> RWTexOutput : register(u0);  // Final composited output (full resolution)

SamplerState LinearSampler : register(s0);

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

[numthreads(8, 8, 1)] void CS_Composite(uint2 tid : SV_DispatchThreadID) {
	if (tid.x >= (uint)ScreenWidth || tid.y >= (uint)ScreenHeight)
		return;

	float3 scene = TexScene[tid].rgb;

	// Map screen UV [0,1] → IFFT UV [0.25, 0.75] to read only the center
	// region where the zero-padded scene was placed (paper section 2.5).
	// The surrounding border absorbed convolution overflow.
	float2 uv = (float2(tid) + 0.5) / float2(ScreenWidth, ScreenHeight);
	float2 ifftUV = uv * 0.5 + 0.25;

	// Bilinear upsample IFFT results (real part only, clamp negative artifacts)
	float glareR = max(0, TexIFFT_R.SampleLevel(LinearSampler, ifftUV, 0).x);
	float glareG = max(0, TexIFFT_G.SampleLevel(LinearSampler, ifftUV, 0).x);
	float glareB = max(0, TexIFFT_B.SampleLevel(LinearSampler, ifftUV, 0).x);
	float3 glare = float3(glareR, glareG, glareB);

	// Sanitize extreme values
	glare = min(glare, 65000.0);
	if (any(isnan(glare)) || any(isinf(glare)))
		glare = 0;

	// Additive composite with user-controlled Intensity.
	// Paper's empirical ×10 factor omitted — our HDR pipeline has a
	// proper tonemapper downstream, so Intensity alone controls strength.
	float3 output = scene + glare * Intensity;

	RWTexOutput[tid] = float4(output, 1.0);
}
