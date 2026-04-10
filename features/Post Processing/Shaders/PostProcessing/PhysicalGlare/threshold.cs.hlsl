// PhysicalGlare - Threshold extraction & downsample to FFT resolution
// Extracts bright pixels from the scene and writes R/G/B channels as complex (real, 0) into separate textures.

#include "../common.hlsli"

RWTexture2D<float2> RWTexFFT_R : register(u0);
RWTexture2D<float2> RWTexFFT_G : register(u1);
RWTexture2D<float2> RWTexFFT_B : register(u2);

Texture2D<float4> TexColor : register(t0);

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

[numthreads(8, 8, 1)] void CS_Threshold(uint2 tid : SV_DispatchThreadID) {
	if (tid.x >= FFTResolution || tid.y >= FFTResolution)
		return;

	// Map FFT pixel to screen UV
	float2 uv = (float2(tid) + 0.5) * RcpFFTResolution;

	// Sample scene at corresponding screen position (bilinear approximation via integer fetch)
	uint2 screenPos = uint2(uv * float2(ScreenWidth, ScreenHeight));
	screenPos = min(screenPos, uint2(uint(ScreenWidth) - 1, uint(ScreenHeight) - 1));

	float3 color = TexColor[screenPos].rgb;

	// Karis-style weighting for anti-firefly
	float lum = Color::RGBToLuminance(color);

	// Apply threshold (in linear space, threshold is in EV-derived linear units)
	float bright = max(0, lum - Threshold);
	float3 extracted = (lum > 1e-6) ? color * (bright / lum) : float3(0, 0, 0);

	// Anti-firefly weight
	float karisWeight = rcp(1.0 + lum);
	extracted *= karisWeight;

	// Write as complex numbers (real = value, imaginary = 0)
	RWTexFFT_R[tid] = float2(extracted.r, 0);
	RWTexFFT_G[tid] = float2(extracted.g, 0);
	RWTexFFT_B[tid] = float2(extracted.b, 0);
}
