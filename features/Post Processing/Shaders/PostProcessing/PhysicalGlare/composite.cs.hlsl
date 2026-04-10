// PhysicalGlare - Composite shader
// Extracts glare from IFFT results, applies temporal smoothing, and composites onto the scene.
// Runs at full screen resolution.

Texture2D<float4> TexScene : register(t0);      // Original scene (full resolution)
Texture2D<float2> TexIFFT_R : register(t1);     // IFFT result, R channel (FFT resolution)
Texture2D<float2> TexIFFT_G : register(t2);     // IFFT result, G channel (FFT resolution)
Texture2D<float2> TexIFFT_B : register(t3);     // IFFT result, B channel (FFT resolution)
Texture2D<float4> TexGlarePrev : register(t4);  // Previous frame glare (FFT resolution)

RWTexture2D<float4> RWTexOutput : register(u0);  // Final composited output (full resolution)
RWTexture2D<float4> RWTexGlare : register(u1);   // Current frame glare for temporal history (FFT resolution)

SamplerState LinearSampler : register(s0);

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

[numthreads(8, 8, 1)] void CS_Composite(uint2 tid : SV_DispatchThreadID) {
	if (tid.x >= (uint)ScreenWidth || tid.y >= (uint)ScreenHeight)
		return;

	float3 scene = TexScene[tid].rgb;

	// Map screen position to FFT UV for bilinear sampling
	float2 uv = (float2(tid) + 0.5) / float2(ScreenWidth, ScreenHeight);

	// Sample IFFT results (take real part only, clamp negative artifacts)
	// FFT textures are at FFT resolution, sample with bilinear filtering
	float2 fftUV = uv;
	uint2 fftPos = uint2(fftUV * float(FFTResolution));
	fftPos = min(fftPos, uint2(FFTResolution - 1, FFTResolution - 1));

	float glareR = max(0, TexIFFT_R[fftPos].x);
	float glareG = max(0, TexIFFT_G[fftPos].x);
	float glareB = max(0, TexIFFT_B[fftPos].x);
	float3 currentGlare = float3(glareR, glareG, glareB);

	// Temporal smoothing at FFT resolution
	// Only write temporal history for pixels within FFT resolution
	if (tid.x < FFTResolution && tid.y < FFTResolution) {
		float3 prevGlare = TexGlarePrev[tid].rgb;
		float blendFactor = saturate(AdaptSpeed * DeltaTime);
		float3 smoothedGlare = lerp(prevGlare, currentGlare, blendFactor);

		RWTexGlare[tid] = float4(smoothedGlare, 1.0);
	}

	// For the composite, sample the temporally smoothed glare at screen resolution
	// Use the previous frame's glare (which was temporally smoothed) for stability
	float2 glareSamplePos = uv * float(FFTResolution);
	uint2 glarePos = uint2(glareSamplePos);
	glarePos = min(glarePos, uint2(FFTResolution - 1, FFTResolution - 1));
	float3 glare = TexGlarePrev[glarePos].rgb;

	// Additive composite
	float3 output = scene + glare * Intensity;

	RWTexOutput[tid] = float4(output, 1.0);
}
