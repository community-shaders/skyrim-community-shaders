// Based on the Pixel Shaders from the DirectX Toolkit
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// http://go.microsoft.com/fwlink/?LinkId=248929

#include "Common/Color.hlsli"

Texture2D<float4> Framebuffer : register(t0);
RWTexture2D<float4> HDROutput : register(u0);

cbuffer PerFrame : register(b0)
{
	float linearExposure : packoffset(c0.x);
	float paperWhiteNits : packoffset(c0.y);
	float maxNits : packoffset(c0.z);
	float tonemapSelector : packoffset(c0.w);

	float4x3 colorRotation : packoffset(c1);
}

// sRGB
// https://en.wikipedia.org/wiki/SRGB

// Apply the (approximate) sRGB curve to linear values
static float3 LinearToSRGBEst(float3 color)
{
	return pow(abs(color), 1 / 2.2f);
}

// (Approximate) sRGB to linear
static float3 SRGBToLinearEst(float3 srgb)
{
	return pow(abs(srgb), 2.2f);
}

// HDR10 Media Profile
// https://en.wikipedia.org/wiki/High-dynamic-range_video#HDR10

// Apply the ST.2084 curve to normalized linear values and outputs normalized non-linear values
static float3 LinearToST2084(float3 normalizedLinearValue)
{
	return pow((0.8359375f + 18.8515625f * pow(abs(normalizedLinearValue), 0.1593017578f)) / (1.0f + 18.6875f * pow(abs(normalizedLinearValue), 0.1593017578f)), 78.84375f);
}

// ST.2084 to linear, resulting in a linear normalized value
static float3 ST2084ToLinear(float3 ST2084)
{
	return pow(max(pow(abs(ST2084), 1.0f / 78.84375f) - 0.8359375f, 0.0f) / (18.8515625f - 18.6875f * pow(abs(ST2084), 1.0f / 78.84375f)), 1.0f / 0.1593017578f);
}

// Reinhard tonemap operator
// Reinhard et al. "Photographic tone reproduction for digital images." ACM Transactions on Graphics. 21. 2002.
// http://www.cs.utah.edu/~reinhard/cdrom/tonemap.pdf
static float3 ToneMapReinhard(float3 color)
{
	return color / (1.0f + color);
}

// Reinhard-Jodie
static float3 ToneMapReinhardJodie(float3 color)
{
	float l = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
	float3 tv = color / (1.0f + color);
	return lerp(color / (1.0f + l), tv, tv);
}

// ACES Filmic tonemap operator
// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
static float3 ToneMapACESFilmic(float3 x)
{
	const float a = 2.51f;
	const float b = 0.03f;
	const float c = 2.43f;
	const float d = 0.59f;
	const float e = 0.14f;
	return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// Uncharted2 Filmic
static float3 Uncharted2_ToneMap_Partial(float3 color)
{
	const float A = 0.15f;
	const float B = 0.50f;
	const float C = 0.10f;
	const float D = 0.20f;
	const float E = 0.02f;
	const float F = 0.30f;
	return ((color * (A * color + C * B) + D * E) / (color * (A * color + B) + D * F)) - E / F;
}

static float3 ToneMapUncharted2Filmic(float3 color)
{
	const float exposure_bias = 2.0f;
	float3 curr = Uncharted2_ToneMap_Partial(color * exposure_bias);

	const float3 w = 11.2f;
	float3 white_scale = 1.0f / Uncharted2_ToneMap_Partial(w);
	return curr * white_scale;
}

//--------------------------------------------------------------------------------------
// Pass-through
static float3 CS_Copy(float3 bufferIn)
{
	return bufferIn;
}

// Saturate (clips above 1.0)
static float3 CS_Saturate(float3 bufferIn)
{
	return saturate(bufferIn * linearExposure);
}

// Reinhard operator
static float3 CS_Reinhard(float3 bufferIn)
{
	return ToneMapReinhard(bufferIn * linearExposure);
}

static float3 CS_Reinhard_Jodie(float3 bufferIn)
{
	return ToneMapReinhardJodie(bufferIn * linearExposure);
}

// ACES filmic operator
static float3 CS_ACESFilmic(float3 bufferIn)
{
	return ToneMapACESFilmic(bufferIn * linearExposure);
}

static float3 CS_Uncharted2Filmic(float3 bufferIn)
{
	return ToneMapUncharted2Filmic(bufferIn * linearExposure);
}

//--------------------------------------------------------------------------------------
// SRGB, using Rec.709 color primaries and a gamma 2.2 curve
static float3 CS_SRGB(float3 bufferIn)
{
	return LinearToSRGBEst(bufferIn);
}

// Saturate (clips above 1.0)
static float3 CS_Saturate_SRGB(float3 bufferIn)
{
	float3 sdr = saturate(bufferIn * linearExposure);
	return LinearToSRGBEst(sdr);
}

// Reinhard operator
static float3 CS_Reinhard_SRGB(float3 bufferIn)
{
	float3 sdr = ToneMapReinhard(bufferIn * linearExposure);
	return LinearToSRGBEst(sdr);
}

static float3 CS_Reinhard_Jodie_SRGB(float3 bufferIn)
{
	float3 sdr = ToneMapReinhardJodie(bufferIn * linearExposure);
	return LinearToSRGBEst(sdr);
}

// ACES filmic operator
static float3 CS_ACESFilmic_SRGB(float3 bufferIn)
{
	float3 sdr = ToneMapACESFilmic(bufferIn * linearExposure);
	return LinearToSRGBEst(sdr);
}

static float3 CS_Uncharted2Filmic_SRGB(float3 bufferIn)
{
	float3 sdr = ToneMapUncharted2Filmic(bufferIn * linearExposure);
	return LinearToSRGBEst(sdr);
}

//--------------------------------------------------------------------------------------
// HDR10, using Rec.2020 color primaries and ST.2084 curve
static float3 HDR10(float3 bufferIn)
{
	// ST.2084 spec defines max nits as 10,000 nits
	float3 normalized = bufferIn * paperWhiteNits / min(max(maxNits, 1.f), 10000.f);

	// Apply ST.2084 curve
	return LinearToST2084(normalized);
}

static float3 CS_HDR10(float3 bufferIn)
{
	return HDR10(bufferIn);
}

static float3 CS_HDR10_Saturate(float3 bufferIn)
{
	float3 rgb = HDR10(bufferIn);
	return saturate(rgb * linearExposure);
}

static float3 CS_HDR10_Reinhard(float3 bufferIn)
{
	float3 rgb = HDR10(bufferIn);
	return ToneMapReinhard(rgb * linearExposure);
}

static float3 CS_HDR10_Reinhard_Jodie(float3 bufferIn)
{
	float3 rgb = HDR10(bufferIn);
	return ToneMapReinhardJodie(rgb * linearExposure);
}

static float3 CS_HDR10_ACESFilmic(float3 bufferIn)
{
	float3 rgb = HDR10(bufferIn);
	return ToneMapACESFilmic(rgb * linearExposure);
}

static float3 CS_HDR10_Uncharted2Filmic(float3 bufferIn)
{
	float3 rgb = HDR10(bufferIn);
	return ToneMapUncharted2Filmic(rgb * linearExposure);
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID
								: SV_DispatchThreadID) {
	float4 framebuffer = Framebuffer[dispatchID.xy];

	// Untonemap the incoming HDR buffer
	float exposureBias = 1.0;
	float3 untonemapped = Color::GammaToLinearSafe(framebuffer.xyz) * exposureBias;
	// Apply color rotation
	float3 colorRotated = mul(untonemapped, (float3x3)colorRotation);

	float3 tonemapped;
	switch ((int)tonemapSelector) {
	default:
	case 0:
		tonemapped = CS_Copy(colorRotated);
		break;
	case 1:
		tonemapped = CS_Saturate(colorRotated);
		break;
	case 2:
		tonemapped = CS_Reinhard(colorRotated);
		break;
	case 3:
		tonemapped = CS_Reinhard_Jodie(colorRotated);
		break;
	case 4:
		tonemapped = CS_ACESFilmic(colorRotated);
		break;
	case 5:
		tonemapped = CS_Uncharted2Filmic(colorRotated);
		break;
	case 6:
		tonemapped = CS_SRGB(colorRotated);
		break;
	case 7:
		tonemapped = CS_Saturate_SRGB(colorRotated);
		break;
	case 8:
		tonemapped = CS_Reinhard_SRGB(colorRotated);
		break;
	case 9:
		tonemapped = CS_Reinhard_Jodie_SRGB(colorRotated);
		break;
	case 10:
		tonemapped = CS_ACESFilmic_SRGB(colorRotated);
		break;
	case 11:
		tonemapped = CS_Uncharted2Filmic_SRGB(colorRotated);
		break;
	case 12:
		tonemapped = CS_HDR10(colorRotated);
		break;
	case 13:
		tonemapped = CS_HDR10_Saturate(colorRotated);
		break;
	case 14:
		tonemapped = CS_HDR10_Reinhard(colorRotated);
		break;
	case 15:
		tonemapped = CS_HDR10_Reinhard_Jodie(colorRotated);
		break;
	case 16:
		tonemapped = CS_HDR10_ACESFilmic(colorRotated);
		break;
	case 17:
		tonemapped = CS_HDR10_Uncharted2Filmic(colorRotated);
		break;
	}

	framebuffer = float4(tonemapped, framebuffer.w);

	HDROutput[dispatchID.xy] = framebuffer;
}
