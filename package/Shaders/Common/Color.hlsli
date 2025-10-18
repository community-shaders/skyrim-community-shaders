#ifndef __COLOR_DEPENDENCY_HLSL__
#define __COLOR_DEPENDENCY_HLSL__

#include "Common/Math.hlsli"

namespace Color
{
	static float GammaCorrectionValue = 2.2;

	// [Jimenez et al. 2016, "Practical Realtime Strategies for Accurate Indirect Occlusion"]
	float3 MultiBounceAO(float3 baseColor, float ao)
	{
		float3 a = 2.0404 * baseColor - 0.3324;
		float3 b = -4.7951 * baseColor + 0.6417;
		float3 c = 2.7552 * baseColor + 0.6903;
		return max(ao, ((ao * a + b) * ao + c) * ao);
	}

	// [Lagarde et al. 2014, "Moving Frostbite to Physically Based Rendering 3.0"]
	float SpecularAOLagarde(float NdotV, float ao, float roughness)
	{
		return saturate(pow(abs(NdotV + ao), exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
	}

	float RGBToLuminance(float3 color)
	{
		return dot(color, float3(0.2125, 0.7154, 0.0721));
	}

	float RGBToLuminanceAlternative(float3 color)
	{
		return dot(color, float3(0.3, 0.59, 0.11));
	}

	float RGBToLuminance2(float3 color)
	{
		return dot(color, float3(0.299, 0.587, 0.114));
	}

	float3 RGBToYCoCg(float3 color)
	{
		float tmp = 0.25 * (color.r + color.b);
		return float3(
			tmp + 0.5 * color.g,        // Y
			0.5 * (color.r - color.b),  // Co
			-tmp + 0.5 * color.g        // Cg
		);
	}

	float3 YCoCgToRGB(float3 color)
	{
		float tmp = color.x - color.z;
		return float3(
			tmp + color.y,
			color.x + color.z,
			tmp - color.y);
	}

	float3 Saturation(float3 color, float saturation)
	{
		float grey = RGBToLuminance(color);
		color.x = max(lerp(grey, color.x, saturation), 0.0f);
		color.y = max(lerp(grey, color.y, saturation), 0.0f);
		color.z = max(lerp(grey, color.z, saturation), 0.0f);
		return color;
	}

	// http://chilliant.blogspot.com/2010/11/rgbhsv-in-hlsl.html
	float3 Hue(float H)
	{
		float R = abs(H * 6 - 3) - 1;
		float G = 2 - abs(H * 6 - 2);
		float B = 2 - abs(H * 6 - 4);
		return saturate(float3(R, G, B));
	}

	float3 HSVtoRGB(in float3 HSV)
	{
		return ((Hue(HSV.x) - 1) * HSV.y + 1) * HSV.z;
	}

	float3 RGBtoHSV(in float3 RGB)
	{
		float3 HSV = 0;
		HSV.z = max(RGB.r, max(RGB.g, RGB.b));
		float M = min(RGB.r, min(RGB.g, RGB.b));
		float C = HSV.z - M;

		if (C != 0) {
			HSV.y = C / HSV.z;
			float3 Delta = (HSV.z - RGB) / C;
			Delta.rgb -= Delta.brg;
			Delta.rg += float2(2, 4);
			if (RGB.r >= HSV.z)
				HSV.x = Delta.b;
			else if (RGB.g >= HSV.z)
				HSV.x = Delta.r;
			else
				HSV.x = Delta.g;
			HSV.x = frac(HSV.x / 6);
		}
		return HSV;
	}

	// Attempt to match vanilla materials that are darker than PBR
	const static float PBRLightingScale = 0.666;

	// Attempt to normalise reflection brightness against DALC
	const static float ReflectionNormalisationScale = 0.666;

	float3 GammaToLinear(float3 color)
	{
		return pow(abs(color), 1.6);
	}

	float3 LinearToGamma(float3 color)
	{
		return pow(abs(color), 1.0 / 1.6);
	}

	float3 GammaToTrueLinear(float3 color)
	{
		return pow(abs(color), 2.2);
	}

	float3 TrueLinearToGamma(float3 color)
	{
		return pow(abs(color), 1.0 / 2.2);
	}

	float3 Diffuse(float3 color)
	{
#if defined(TRUE_PBR)
		return TrueLinearToGamma(color);
#else
		return color;
#endif
	}

	float3 Light(float3 color)
	{
#if defined(TRUE_PBR)
		return color * Math::PI;  // Compensate for traditional Lambertian diffuse
#else
		return color;
#endif
	}
}

#endif  //__COLOR_DEPENDENCY_HLSL__