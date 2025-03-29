// Based on the Pixel Shaders from the DirectX Toolkit
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// http://go.microsoft.com/fwlink/?LinkId=248929

Texture2D<float4> Framebuffer : register(t0);
RWTexture2D<float4> HDROutput : register(u0);

cbuffer PerFrame : register(b0)
{
	float linearExposure : packoffset(c0.x);
	float paperWhiteNits : packoffset(c0.y);
	float tonemapSelector : packoffset(c0.z);
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
float3 ToneMapReinhard(float3 color)
{
	return color / (1.0f + color);
}

// ACES Filmic tonemap operator
// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
float3 ToneMapACESFilmic(float3 x)
{
	float a = 2.51f;
	float b = 0.03f;
	float c = 2.43f;
	float d = 0.59f;
	float e = 0.14f;
	return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

//--------------------------------------------------------------------------------------
// Pass-through
float4 CS_Copy(float4 bufferIn)
{
	return bufferIn;
}

// Saturate (clips above 1.0)
float4 CS_Saturate(float4 bufferIn)
{
	float3 sdr = saturate(bufferIn.xyz * linearExposure);
	return float4(sdr, bufferIn.a);
}

// Reinhard operator
float4 CS_Reinhard(float4 bufferIn)
{
	float3 sdr = ToneMapReinhard(bufferIn.xyz * linearExposure);
	return float4(sdr, bufferIn.a);
}

// ACES filmic operator
float4 CS_ACESFilmic(float4 bufferIn)
{
	float3 sdr = ToneMapACESFilmic(bufferIn.xyz * linearExposure);
	return float4(sdr, bufferIn.a);
}

//--------------------------------------------------------------------------------------
// SRGB, using Rec.709 color primaries and a gamma 2.2 curve

// sRGB
float4 CS_SRGB(float4 bufferIn)
{
	float3 srgb = LinearToSRGBEst(bufferIn.xyz);
	return float4(srgb, bufferIn.a);
}

// Saturate (clips above 1.0)
float4 CS_Saturate_SRGB(float4 bufferIn)
{
	float3 sdr = saturate(bufferIn.xyz * linearExposure);
	float3 srgb = LinearToSRGBEst(sdr);
	return float4(srgb, bufferIn.a);
}

// Reinhard operator
float4 CS_Reinhard_SRGB(float4 bufferIn)
{
	float3 sdr = ToneMapReinhard(bufferIn.xyz * linearExposure);
	float3 srgb = LinearToSRGBEst(sdr);
	return float4(srgb, bufferIn.a);
}

// ACES filmic operator
float4 CS_ACESFilmic_SRGB(float4 bufferIn)
{
	float3 sdr = ToneMapACESFilmic(bufferIn.xyz * linearExposure);
	float3 srgb = LinearToSRGBEst(sdr);
	return float4(srgb, bufferIn.a);
}

//--------------------------------------------------------------------------------------
// HDR10, using Rec.2020 color primaries and ST.2084 curve

float3 HDR10(float3 bufferIn)
{
	// ST.2084 spec defines max nits as 10,000 nits
	float3 normalized = bufferIn.xyz * paperWhiteNits / 10000.f;

	// Apply ST.2084 curve
	return LinearToST2084(normalized);
}

float4 CS_HDR10(float4 bufferIn)
{
	float3 rgb = HDR10(bufferIn.xyz);
	return float4(rgb, bufferIn.a);
}

float4 CS_HDR10_Saturate(float4 bufferIn)
{
	float3 rgb = HDR10(bufferIn.xyz);
	float3 sdr = saturate(rgb * linearExposure);
	return float4(sdr, bufferIn.a);
}

float4 CS_HDR10_Reinhard(float4 bufferIn)
{
	float3 rgb = HDR10(bufferIn.xyz);
	float3 sdr = ToneMapReinhard(rgb * linearExposure);
	return float4(sdr, bufferIn.a);
}

float4 CS_HDR10_ACESFilmic(float4 bufferIn)
{
	float3 rgb = HDR10(bufferIn.xyz);
	float3 sdr = ToneMapACESFilmic(rgb * linearExposure);
	return float4(sdr, bufferIn.a);
}

float4 CS_HDR10_SRGB(float4 bufferIn)
{
	float3 rgb = HDR10(bufferIn.xyz);
	return float4(LinearToSRGBEst(rgb), bufferIn.a);
}

float4 CS_HDR10_Saturate_SRGB(float4 bufferIn)
{
	float3 rgb = HDR10(bufferIn.xyz);
	float3 sdr = saturate(rgb * linearExposure);
	return float4(LinearToSRGBEst(sdr), bufferIn.a);
}

float4 CS_HDR10_Reinhard_SRGB(float4 bufferIn)
{
	float3 rgb = HDR10(bufferIn.xyz);
	float3 sdr = ToneMapReinhard(rgb * linearExposure);
	return float4(LinearToSRGBEst(sdr), bufferIn.a);
}

float4 CS_HDR10_ACESFilmic_SRGB(float4 bufferIn)
{
	float3 rgb = HDR10(bufferIn.xyz);
	float3 sdr = ToneMapACESFilmic(rgb * linearExposure);
	return float4(LinearToSRGBEst(sdr), bufferIn.a);
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	float4 framebuffer = Framebuffer[dispatchID.xy];

	switch ((int)tonemapSelector) {
	default:
	case 0:
		framebuffer = CS_Copy(float4(mul(framebuffer.xyz, (float3x3)colorRotation), framebuffer.a));
		break;
	case 1:
		framebuffer = CS_Saturate(float4(mul(framebuffer.xyz, (float3x3)colorRotation), framebuffer.a));
		break;
	case 2:
		framebuffer = CS_Reinhard(float4(mul(framebuffer.xyz, (float3x3)colorRotation), framebuffer.a));
		break;
	case 3:
		framebuffer = CS_ACESFilmic(float4(mul(framebuffer.xyz, (float3x3)colorRotation), framebuffer.a));
		break;
	case 4:
		framebuffer = CS_SRGB(float4(mul(framebuffer.xyz, (float3x3)colorRotation), framebuffer.a));
		break;
	case 5:
		framebuffer = CS_Saturate_SRGB(float4(mul(framebuffer.xyz, (float3x3)colorRotation), framebuffer.a));
		break;
	case 6:
		framebuffer = CS_Reinhard_SRGB(float4(mul(framebuffer.xyz, (float3x3)colorRotation), framebuffer.a));
		break;
	case 7:
		framebuffer = CS_ACESFilmic_SRGB(float4(mul(framebuffer.xyz, (float3x3)colorRotation), framebuffer.a));
		break;
	case 8:
		framebuffer = CS_HDR10(float4(mul(framebuffer.xyz, (float3x3)colorRotation), framebuffer.a));
		break;
	case 9:
		framebuffer = CS_HDR10_Saturate(float4(mul(framebuffer.xyz, (float3x3)colorRotation), framebuffer.a));
		break;
	case 10:
		framebuffer = CS_HDR10_Reinhard(float4(mul(framebuffer.xyz, (float3x3)colorRotation), framebuffer.a));
		break;
	case 11:
		framebuffer = CS_HDR10_ACESFilmic(float4(mul(framebuffer.xyz, (float3x3)colorRotation), framebuffer.a));
		break;
	case 12:
		framebuffer = CS_HDR10_SRGB(float4(mul(framebuffer.xyz, (float3x3)colorRotation), framebuffer.a));
		break;
	case 13:
		framebuffer = CS_HDR10_Saturate_SRGB(float4(mul(framebuffer.xyz, (float3x3)colorRotation), framebuffer.a));
		break;
	case 14:
		framebuffer = CS_HDR10_Reinhard_SRGB(float4(mul(framebuffer.xyz, (float3x3)colorRotation), framebuffer.a));
		break;
	case 15:
		framebuffer = CS_HDR10_ACESFilmic_SRGB(float4(mul(framebuffer.xyz, (float3x3)colorRotation), framebuffer.a));
		break;
	}

	HDROutput[dispatchID.xy] = framebuffer;
}
