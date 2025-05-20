#ifndef __COLOR_DEPENDENCY_HLSL__
#define __COLOR_DEPENDENCY_HLSL__

#include "Common/Math.hlsli"
#include "Common/SharedData.hlsli"

namespace Color
{
	static float GammaCorrectionValue = 2.2;

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

	// Attempt to match vanilla materials tha are a darker than PBR
	const static float PBRLightingScale = 0.666;

	float3 GammaToLinear(float3 color)
	{
		return pow(max(color, 0), 1.8);
	}

	float3 LinearToGamma(float3 color)
	{
		return pow(max(color, 0), 1.0 / 1.8);
	}

	float3 GammaToTrueLinear(float3 color)
	{
		return pow(max(color, 0), 2.2);
	}

	float3 TrueLinearToGamma(float3 color)
	{
		return pow(max(color, 0), 1.0 / 2.2);
	}

	float3 GammaToLinearLuminancePreserving(float3 color)
	{
		float originalLuminance = RGBToLuminance(color);
		float3 linearColorRaw = GammaToLinear(color / originalLuminance);
		float scale = 1.0;
		if (originalLuminance > 1e-5) {
			scale = GammaToLinear(originalLuminance);
		} else if (originalLuminance <= 1e-5) {
			return float3(0.0, 0.0, 0.0);
		}
		float3 finalLinearColor = linearColorRaw * scale;
		return finalLinearColor;
	}

	float3 GammaToLinearLuminancePreservingLight(float3 color)
	{
		float originalLuminance = RGBToLuminance(color);
		float3 linearColorRaw = GammaToLinear(color / originalLuminance);
		float scale = 1.0;
		if (originalLuminance > 1e-5) {
			scale = originalLuminance;
		} else if (originalLuminance <= 1e-5) {
			return float3(0.0, 0.0, 0.0);
		}
		float3 finalLinearColor = linearColorRaw * scale;
		return finalLinearColor;
	}

#if defined(PSHADER) || defined(CSHADER) || defined(COMPUTESHADER)
	float3 Diffuse(float3 color)
	{
		if (!SharedData::linearLightingSettings.enableLinearLighting) {
#	if defined(TRUE_PBR)
			return pow(abs(color), 1.0 / 2.2);
#	else
			return color;
#	endif
		} else {
#	if defined(TRUE_PBR)
			return color;
#	else
			return pow(abs(color), 2.2);
#	endif
		}
	}

	float3 Light(float3 color)
	{
		if (SharedData::linearLightingSettings.enableLinearLighting) {
			color = GammaToLinearLuminancePreservingLight(color);
		}
#	if defined(TRUE_PBR)
		return color * Math::PI;  // Compensate for traditional Lambertian diffuse
#	else
		return color;
#	endif
	}
#endif
}

#endif  //__COLOR_DEPENDENCY_HLSL__