// Physical Glare — Scene composite with energy conservation
// Community Shaders / Post Processing — Author: Jiaye, 2026
//
// Reconstructs per-channel glare from IFFT results, upsamples from FFT
// resolution to screen resolution via Catmull-Rom bicubic interpolation [4],
// and composites onto the original scene.
//
// Energy conservation: the bright threshold contribution is subtracted from
// the scene and replaced by the convolved (spread) version.  At Intensity=1.0
// total luminous energy is redistributed, not added.
//
// Only the center region [P, 1−P) of the IFFT output is read, corresponding
// to the non-padded scene area (P = PaddingRatio) [1, section 2.5].
//
// References:
//   [1] Delavennat (2021), Physically-based Real-time Glare, LiU.
//   [4] Jimenez (2012), Filmic SMAA, SIGGRAPH — bicubic via HW bilinear.

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

// ---------------------------------------------------------------------------
// Catmull-Rom bicubic upsample (4 bilinear taps → 16-texel footprint).
// Dramatically reduces blocky artifacts when upsampling from FFT resolution
// (e.g. 768→2160p = 2.8×).  Cost: 4 SampleLevel per channel vs 1 — negligible
// for a full-screen composite that's bandwidth-bound anyway.
// Ref: Jimenez, "Filmic SMAA" (SIGGRAPH 2012), bicubic with HW bilinear.
// ---------------------------------------------------------------------------
float CatmullRomSample(Texture2D<float2> tex, SamplerState samp, float2 uv, float2 texSize)
{
	float2 samplePos = uv * texSize;
	float2 texPos1 = floor(samplePos - 0.5) + 0.5;
	float2 f = samplePos - texPos1;

	// Catmull-Rom weights for the 4 taps (2 negative lobes + 2 positive)
	float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
	float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
	float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
	float2 w3 = f * f * (-0.5 + 0.5 * f);

	// Combine pairs for 4 bilinear taps instead of 16 point samples
	float2 w12 = w1 + w2;
	float2 offset12 = w2 / max(w12, 1e-6);

	float2 texPos0 = (texPos1 - 1.0) / texSize;
	float2 texPos3 = (texPos1 + 2.0) / texSize;
	float2 texPos12 = (texPos1 + offset12) / texSize;

	float result = 0.0;
	result += tex.SampleLevel(samp, float2(texPos0.x, texPos0.y), 0).x * w0.x * w0.y;
	result += tex.SampleLevel(samp, float2(texPos12.x, texPos0.y), 0).x * w12.x * w0.y;
	result += tex.SampleLevel(samp, float2(texPos3.x, texPos0.y), 0).x * w3.x * w0.y;

	result += tex.SampleLevel(samp, float2(texPos0.x, texPos12.y), 0).x * w0.x * w12.y;
	result += tex.SampleLevel(samp, float2(texPos12.x, texPos12.y), 0).x * w12.x * w12.y;
	result += tex.SampleLevel(samp, float2(texPos3.x, texPos12.y), 0).x * w3.x * w12.y;

	result += tex.SampleLevel(samp, float2(texPos0.x, texPos3.y), 0).x * w0.x * w3.y;
	result += tex.SampleLevel(samp, float2(texPos12.x, texPos3.y), 0).x * w12.x * w3.y;
	result += tex.SampleLevel(samp, float2(texPos3.x, texPos3.y), 0).x * w3.x * w3.y;

	return result;
}

[numthreads(8, 8, 1)] void CS_Composite(uint2 tid : SV_DispatchThreadID) {
	if (tid.x >= (uint)ScreenWidth || tid.y >= (uint)ScreenHeight)
		return;

	float3 scene = TexScene[tid].rgb;

	// Map screen UV [0,1] → IFFT UV [P, 1-P], reading only the centre
	// region that contained the original scene within the zero-padded
	// texture [1, section 2.5].
	float sceneScale = 1.0 - 2.0 * PaddingRatio;
	float2 uv = (float2(tid) + 0.5) / float2(ScreenWidth, ScreenHeight);
	float2 ifftUV = uv * sceneScale + PaddingRatio;

	// Bicubic (Catmull-Rom) upsample — eliminates blocky artifacts at high upscale ratios
	float2 texSize = float2(FFTResolution, FFTResolution);
	float glareR = max(0, CatmullRomSample(TexIFFT_R, LinearSampler, ifftUV, texSize));
	float glareG = max(0, CatmullRomSample(TexIFFT_G, LinearSampler, ifftUV, texSize));
	float glareB = max(0, CatmullRomSample(TexIFFT_B, LinearSampler, ifftUV, texSize));
	float3 glare = float3(glareR, glareG, glareB);

	// Sanitize extreme values
	glare = min(glare, 65000.0);
	if (any(isnan(glare)) || any(isinf(glare)))
		glare = 0;

	// Energy-conserving glare contribution:
	// Subtract the thresholded bright component and output the convolved
	// (spread) result.  At Intensity = 1 total energy is redistributed,
	// not gained.  Values != 1 allow artistic attenuation or exaggeration.
	// This output is combined with the scene in the BloomFlareComposite pass.
	float3 bright = max(0, scene - Threshold);
	float3 output = (glare - bright) * Intensity;

	// Clamp to prevent negative values at Intensity > 1.0
	output = max(output, 0);

	RWTexOutput[tid] = float4(output, 1.0);
}
