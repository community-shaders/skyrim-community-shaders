#ifndef __COLOR_DEPENDENCY_HLSL__
#define __COLOR_DEPENDENCY_HLSL__

#include "Common/Math.hlsli"
#include "Common/SharedData.hlsli"

#define ENABLE_LL SharedData::linearLightingSettings.enableLinearLighting

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

#if defined(PSHADER) || defined(CSHADER) || defined(COMPUTESHADER)
	// Attempt to match vanilla materials tha are a darker than PBR
	const static float PBRLightingScale = ENABLE_LL ? 1.0 : 0.666;
	const static float PBRLightingCompensation = ENABLE_LL ? 1.0 : Math::PI;

	float3 GammaToLinearLuminancePreserving(float3 color)
	{
		float originalLuminance = RGBToLuminance(color);
		float3 linearColorRaw = GammaToLinear(color / originalLuminance);
		if (originalLuminance <= 1e-5) {
			return float3(0.0, 0.0, 0.0);
		}
		float scale = GammaToLinear(originalLuminance);
		return linearColorRaw * scale;
	}

	float3 GammaToLinearLuminancePreservingLight(float3 color)
	{
		if (!ENABLE_LL) {
			return color;
		}
		float originalLuminance = RGBToLuminance(color);
		float3 linearColorRaw = pow(color / originalLuminance, SharedData::linearLightingSettings.lightGamma);
		if (originalLuminance <= 1e-5) {
			return float3(0.0, 0.0, 0.0);
		}
		float scale = originalLuminance;
		return linearColorRaw * scale;
	}

	float3 LLGammaToLinear(float3 color)
	{
		return ENABLE_LL ? GammaToLinear(color) : color;
	}

	float3 LLLinearToGamma(float3 color)
	{
		return ENABLE_LL ? LinearToGamma(color) : color;
	}

	float3 Diffuse(float3 color)
	{
#	if defined(TRUE_PBR)
		return ENABLE_LL ? color : LinearToGamma(color);
#	else
		return ENABLE_LL ? pow(abs(color), SharedData::linearLightingSettings.colorGamma) : color;
#	endif
	}

	float3 Light(float3 color)
	{
		color = ENABLE_LL ? pow(abs(color), SharedData::linearLightingSettings.lightGamma) : color;
#	if defined(TRUE_PBR)
		return color * PBRLightingCompensation;  // Compensate for traditional Lambertian diffuse
#	else
		return color;
#	endif
	}

	float3 Ambient(float3 color)
	{
		return ENABLE_LL ? pow(abs(color), SharedData::linearLightingSettings.ambientGamma) : color;
	}

	float3 Fog(float3 color)
	{
		return ENABLE_LL ? pow(abs(color), SharedData::linearLightingSettings.fogGamma) : color;
	}

	float3 Effect(float3 color)
	{
		if (ENABLE_LL) {
			color = pow(abs(color), SharedData::linearLightingSettings.effectGamma);
#	if defined(MEMBRANE)
			color *= SharedData::linearLightingSettings.membraneEffectMult;
#	elif defined(BLOOD)
			color *= SharedData::linearLightingSettings.bloodEffectMult;
#	elif defined(PROJECTED_UV)
			color *= SharedData::linearLightingSettings.projectedEffectMult;
#	elif defined(DEFERRED)
			color *= SharedData::linearLightingSettings.deferredEffectMult;
#	else
			color *= SharedData::linearLightingSettings.otherEffectMult;
#	endif
		}
		return color;
	}

	float EffectAlpha(float alpha)
	{
		return ENABLE_LL ? pow(abs(alpha), SharedData::linearLightingSettings.effectAlphaGamma) : alpha;
	}

	float3 Sky(float3 color)
	{
		return ENABLE_LL ? pow(abs(color), SharedData::linearLightingSettings.skyGamma) : color;
	}

	float3 Water(float3 color)
	{
		return ENABLE_LL ? pow(abs(color), SharedData::linearLightingSettings.waterGamma) : color;
	}

	float3 VolumetricLighting(float3 color)
	{
		return ENABLE_LL ? pow(abs(color), SharedData::linearLightingSettings.vlGamma) : color;
	}

	float3 ColorToLinear(float3 color)
	{
		return ENABLE_LL ? pow(abs(color), SharedData::linearLightingSettings.colorGamma) : color;
	}

	float3 RadianceToLinear(float3 color)
	{
		return ENABLE_LL ? color : GammaToLinear(color);
	}

	float3 IrradianceToLinear(float3 color)
	{
		return ENABLE_LL ? color : GammaToLinear(color);
	}

	float3 IrradianceToGamma(float3 color)
	{
		return ENABLE_LL ? color : LinearToGamma(color);
	}

	float VanillaDiffuseMult()
	{
		return ENABLE_LL ? SharedData::linearLightingSettings.vanillaDiffuseMult : 1.0f;
	}

	float VanillaSpecularMult()
	{
		return ENABLE_LL ? SharedData::linearLightingSettings.vanillaSpecularMult : 1.0f;
	}
#endif
}

#endif  //__COLOR_DEPENDENCY_HLSL__