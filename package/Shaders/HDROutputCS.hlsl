#include "Common/Color.hlsli"

Texture2D<float4> Framebuffer : register(t0);
RWTexture2D<float4> HDROutput : register(u0);

cbuffer PerFrame : register(b0)
{
	// parameters0.x = paperWhite
	// parameters0.y = peakNits
	// parameters0.z = exposure
	// parameters0.w = unused
	float4 parameters0 : packoffset(c0);
	// parameters1.x = highlights
	// parameters1.y = shadows
	// parameters1.z = contrast
	// parameters1.w = saturation
	float4 parameters1 : packoffset(c1);
	// parameters2.x = dechroma
	// parameters2.y = hueCorrectionStrength
	// parameters2.z = 0.f // Currently unused
	// parameters2.w = 0.f // Currently unused
	float4 parameters2 : packoffset(c2);
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID
								: SV_DispatchThreadID) {
	float4 framebuffer = Framebuffer[dispatchID.xy];

#ifdef HDR_INPUT
	// Input is already linear HDR from R16G16B16A16_FLOAT render target
	float3 linearColor = framebuffer.xyz;
#else
	// Input is gamma-encoded SDR, linearize it
	float3 linearColor = Color::GammaToLinearSafe(framebuffer.xyz);
#endif

	// Apply exposure
	float3 exposedColor = linearColor * parameters0.z;

#ifdef SDR_OUTPUT
	// For SDR output, convert back to gamma space
	float3 sdrColor = Color::LinearToGammaSafe(exposedColor);
	HDROutput[dispatchID.xy] = float4(sdrColor, framebuffer.w);
#else
	// HDR10 output encoding:
	// 1. exposedColor is in linear BT.709
	// 2. Convert color space from BT.709 to BT.2020 (in linear space)
	// 3. PQ encode where 1.0 input = paperWhite nits output
	
	float paperWhiteNits = parameters0.x;
	
	// Clamp to valid range before color space conversion to avoid negative values
	float3 linearClamped = max(exposedColor, 0.0);
	
	// Convert to BT.2020 color space (still in linear, normalized 0-1)
	float3 bt2020Linear = Color::BT709ToBT2020(linearClamped);
	
	// PQ encode: scaling parameter means "1.0 input = scaling nits"
	// So with paperWhiteNits, an input of 1.0 (white) will be displayed at paperWhiteNits brightness
	float3 pqColor = Color::pq::Encode(bt2020Linear, paperWhiteNits);
	
	HDROutput[dispatchID.xy] = float4(pqColor, framebuffer.w);
#endif
}
