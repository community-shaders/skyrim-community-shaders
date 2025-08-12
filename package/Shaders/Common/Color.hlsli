#ifndef __COLOR_DEPENDENCY_HLSL__
#define __COLOR_DEPENDENCY_HLSL__

#include "Common/Math.hlsli"

#define LUM_601 float3(0.299, 0.587, 0.114)
#define LUM_709 float3(0.212, 0.715, 0.072)
#define LUM_202 float3(0.262, 0.678, 0.059)

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

	float RGBToChrominance(float3 color)
	{
    	return Math::max3(color.rgb) - Math::min3(color.rgb);
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

	float3 RGBtoHSV(float3 c)
	{
		float4 K = float4(0.0, -1.0/3.0, 2.0/3.0, -1.0);
		float4 p = (c.g < c.b) ? float4(c.b, c.g, K.w, K.z) : float4(c.g, c.b, K.x, K.y);
		float4 q = (c.r < p.x) ? float4(p.x, p.y, p.z, c.r) : float4(c.r, p.y, p.z, p.x);
		float d = q.x - min(q.w, q.y); float e = 1.0e-10;
		return float3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
	}

	float3 HSVtoRGB(float3 c)
	{
		float4 K = float4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
		float3 p = abs(frac(c.xxx + K.xyz) * 6.0 - K.www);
		return c.z * lerp(K.xxx, saturate(p - K.xxx), c.y);
	}

	float3 Saturation(float3 color, float saturation)
	{
		float grey = RGBToLuminance(color);
		color.x = max(lerp(grey, color.x, saturation), 0.0f);
		color.y = max(lerp(grey, color.y, saturation), 0.0f);
		color.z = max(lerp(grey, color.z, saturation), 0.0f);
		return color;
	}

	// Attempt to match vanilla materials that are darker than PBR
	const static float PBRLightingScale = 0.65;

	// Attempt to normalise reflection brightness against DALC
	const static float ReflectionNormalisationScale = 0.65;

	float GammaToLinear(float color)
	{
		return pow(abs(color), 1.6);
	}

	float LinearToGamma(float color)
	{
		return pow(abs(color), 1.0 / 1.6);
	}

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