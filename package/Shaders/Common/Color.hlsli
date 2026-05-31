#ifndef __COLOR_DEPENDENCY_HLSL__
#define __COLOR_DEPENDENCY_HLSL__

#include "Common/Math.hlsli"
#include "Common/SharedData.hlsli"

#define ENABLE_LL SharedData::linearLightingSettings.enableLinearLighting

#if defined(PSHADER) && defined(LIGHTING)
cbuffer LLPerGeometry : register(b8)
{
	float emissiveMult;
	float3 pad0;
};
#endif

// Float limits
#define FLT_MIN asfloat(0x00800000)  // 1.175494351e-38f
#define FLT_MAX asfloat(0x7F7FFFFF)  // 3.402823466e+38f

namespace Color
{
	static float GammaCorrectionValue = 2.2;

	// Copyright 2019 Google LLC.
	// SPDX-License-Identifier: Apache-2.0
	// Polynomial approximation in GLSL for the Turbo colormap
	// Original LUT: https://gist.github.com/mikhailov-work/ee72ba4191942acecc03fe6da94fc73f
	// Authors: Anton Mikhailov (mikhailov@google.com), Ruofei Du (ruofei@google.com)
	// https://ai.googleblog.com/2019/08/turbo-improved-rainbow-colormap-for.html
	float3 TurboColormap(float x)
	{
		const float4 kRedVec4 = float4(0.13572138, 4.61539260, -42.66032258, 132.13108234);
		const float4 kGreenVec4 = float4(0.09140261, 2.19418839, 4.84296658, -14.18503333);
		const float4 kBlueVec4 = float4(0.10667330, 12.64194608, -60.58204836, 110.36276771);
		const float2 kRedVec2 = float2(-152.94239396, 59.28637943);
		const float2 kGreenVec2 = float2(4.27729857, 2.82956604);
		const float2 kBlueVec2 = float2(-89.90310912, 27.34824973);

		x = saturate(x);
		float4 v4 = float4(1.0, x, x * x, x * x * x);
		float2 v2 = v4.zw * v4.z;
		return float3(
			dot(v4, kRedVec4) + dot(v2, kRedVec2),
			dot(v4, kGreenVec4) + dot(v2, kGreenVec2),
			dot(v4, kBlueVec4) + dot(v2, kBlueVec2));
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

	float SkyrimGammaToLinear(float color)
	{
		return pow(abs(color), 1.6);
	}

	float LinearToSkyrimGamma(float color)
	{
		return pow(abs(color), 1.0 / 1.6);
	}

	float3 SkyrimGammaToLinear(float3 color)
	{
		return pow(abs(color), 1.6);
	}

	float3 LinearToSkyrimGamma(float3 color)
	{
		return pow(abs(color), 1.0 / 1.6);
	}

	float3 SrgbToLinear(float3 color)
	{
		return pow(abs(color), 2.2);
	}

	float3 LinearToSrgb(float3 color)
	{
		return pow(abs(color), 1.0 / 2.2);
	}

	float3 GammaToLinearSafe(float3 color)
	{
		return sign(color) * pow(abs(color), 2.2);
	}

	float3 LinearToGammaSafe(float3 color)
	{
		return sign(color) * pow(abs(color), 1.0 / 2.2);
	}

	static const float3x3 BT709_2_BT2020 = {
		0.627403914928436279296875f, 0.3292830288410186767578125f, 0.0433130674064159393310546875f,
		0.069097287952899932861328125f, 0.9195404052734375f, 0.011362315155565738677978515625f,
		0.01639143936336040496826171875f, 0.08801330626010894775390625f, 0.895595252513885498046875f
	};

	static const float3x3 BT2020_2_BT709 = {
		1.66049098968505859375f, -0.58764111995697021484375f, -0.072849862277507781982421875f,
		-0.12455047667026519775390625f, 1.13289988040924072265625f, -0.0083494223654270172119140625f,
		-0.01815076358616352081298828125f, -0.100578896701335906982421875f, 1.11872971057891845703125f
	};

	float3 BT709ToBT2020(float3 color)
	{
		return mul(BT709_2_BT2020, color);
	}

	float3 BT2020ToBT709(float3 color)
	{
		return mul(BT2020_2_BT709, color);
	}

	namespace pq
	{
		static const float M1 = 2610.f / 16384.f;           // 0.1593017578125f;
		static const float M2 = 128.f * (2523.f / 4096.f);  // 78.84375f;
		static const float C1 = 3424.f / 4096.f;            // 0.8359375f;
		static const float C2 = 32.f * (2413.f / 4096.f);   // 18.8515625f;
		static const float C3 = 32.f * (2392.f / 4096.f);   // 18.6875f;

		float3 Encode(float3 color, float scaling = 10000.f)
		{
			color *= (scaling / 10000.f);
			float3 y_m1 = pow(max(0.0, color), M1);
			return pow((C1 + C2 * y_m1) / (1.f + C3 * y_m1), M2);
		}

		float3 Decode(float3 color, float scaling = 10000.f)
		{
			float3 e_m12 = pow(color, 1.f / M2);
			float3 out_color = pow(max(0, e_m12 - C1) / (C2 - C3 * e_m12), 1.f / M1);
			return out_color * (10000.f / scaling);
		}

	}  // namespace pq

#if defined(PSHADER) || defined(CSHADER) || defined(COMPUTESHADER)
	// Attempt to match vanilla materials that are darker than PBR
	const static float PBRLightingScale = ENABLE_LL ? 1.0 : 0.65;

	// Attempt to normalise reflection brightness against DALC
	const static float ReflectionNormalisationScale = ENABLE_LL ? 1.0 : 0.65;

	const static float PBRLightingCompensation = ENABLE_LL ? 1.0 : Math::PI;

	// Linear Lighting Functions
	float3 LLGammaToLinear(float3 color)
	{
		return ENABLE_LL ? SkyrimGammaToLinear(color) : color;
	}

	float3 LLLinearToGamma(float3 color)
	{
		return ENABLE_LL ? LinearToSkyrimGamma(color) : color;
	}

	float3 Diffuse(float3 color)
	{
#	if defined(TRUE_PBR)
		return ENABLE_LL ? color : LinearToSrgb(color);
#	else
		return ENABLE_LL ? pow(abs(color), SharedData::linearLightingSettings.colorGamma) * SharedData::linearLightingSettings.vanillaDiffuseColorMult : color;
#	endif
	}

	float3 Light(float3 color, bool isLinear = false)
	{
		color = (ENABLE_LL && !isLinear) ? pow(abs(color), SharedData::linearLightingSettings.lightGamma) : color;
#	if defined(TRUE_PBR)
		return color * PBRLightingCompensation;  // Compensate for traditional Lambertian diffuse
#	else
		return color;
#	endif
	}

	float3 DirectionalLight(float3 color, bool isLinear = false)
	{
		return Light(color, isLinear) *
		       ((ENABLE_LL && !isLinear) ? Math::PI * SharedData::linearLightingSettings.directionalLightMult : 1.0f);
	}

	float3 PointLight(float3 color, bool isLinear = false)
	{
		return Light(color, isLinear) *
		       ((ENABLE_LL && !isLinear) ? Math::PI * SharedData::linearLightingSettings.pointLightMult : 1.0f);
	}
#	if defined(LIGHTING)
	float3 EmitColor(float3 color)
	{
		return ENABLE_LL ? (pow(abs(color / max(emissiveMult, 1e-5)), SharedData::linearLightingSettings.emitColorGamma) * emissiveMult * SharedData::linearLightingSettings.emitColorMult) : color;
	}
#	endif

	float3 Glowmap(float3 color)
	{
#	if defined(TRUE_PBR)
		return ENABLE_LL ? color * SharedData::linearLightingSettings.glowmapMult : LinearToSrgb(color);
#	else
		return ENABLE_LL ? pow(abs(color), SharedData::linearLightingSettings.glowmapGamma) * SharedData::linearLightingSettings.glowmapMult : color;
#	endif
	}

	float3 Ambient(float3 color)
	{
		return ENABLE_LL ? pow(abs(color), SharedData::linearLightingSettings.ambientGamma) * SharedData::linearLightingSettings.ambientMult : color;
	}

	float3 Fog(float3 color)
	{
		return ENABLE_LL ? pow(abs(color), SharedData::linearLightingSettings.fogGamma) : color;
	}

	float FogAlpha(float alpha)
	{
		return ENABLE_LL ? pow(abs(alpha), SharedData::linearLightingSettings.fogAlphaGamma) : alpha;
	}

	float3 Effect(float3 color)
	{
		return ENABLE_LL ? pow(abs(color), SharedData::linearLightingSettings.effectGamma) : color;
	}

	float3 EffectMult(float3 color)
	{
		if (ENABLE_LL) {
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

	float EffectLightingMult()
	{
		return ENABLE_LL ? SharedData::linearLightingSettings.effectLightingMult : 1.0f;
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
		return ENABLE_LL ? color : SkyrimGammaToLinear(color);
	}

	float IrradianceToLinear(float color)
	{
		return ENABLE_LL ? color : SkyrimGammaToLinear(color);
	}

	float IrradianceToGamma(float color)
	{
		return ENABLE_LL ? color : LinearToSkyrimGamma(color);
	}

	float3 IrradianceToLinear(float3 color)
	{
		return ENABLE_LL ? color : SkyrimGammaToLinear(color);
	}

	float3 IrradianceToGamma(float3 color)
	{
		return ENABLE_LL ? color : LinearToSkyrimGamma(color);
	}

	float VanillaNormalization()
	{
		return ENABLE_LL ? 1.0 / Math::PI : 1.0f;
	}

	float VanillaDiffuseColorMult()
	{
		return ENABLE_LL ? SharedData::linearLightingSettings.vanillaDiffuseColorMult : 1.0f;
	}
#else
	const static float PBRLightingScale = 1.0;
	const static float ReflectionNormalisationScale = 1.0;
	const static float PBRLightingCompensation = Math::PI;

	float3 Diffuse(float3 color)
	{
#	if defined(TRUE_PBR)
		return LinearToSrgb(color);
#	else
		return color;
#	endif
	}

	float3 Light(float3 color)
	{
#	if defined(TRUE_PBR)
		return color * Math::PI;  // Compensate for traditional Lambertian diffuse
#	else
		return color;
#	endif
	}
#endif

	namespace Correct
	{
		static const float3x3 BT709_2_OKLABLMS = {
			0.4122214708f, 0.5363325363f, 0.0514459929f,
			0.2119034982f, 0.6806995451f, 0.1073969566f,
			0.0883024619f, 0.2817188376f, 0.6299787005f
		};
		static const float3x3 OKLABLMS_2_OKLAB = {
			0.2104542553f, 0.7936177850f, -0.0040720468f,
			1.9779984951f, -2.4285922050f, 0.4505937099f,
			0.0259040371f, 0.7827717662f, -0.8086757660f
		};
		float3 BT709ToOKLab(float3 bt709)
		{
			float3 lms = mul(BT709_2_OKLABLMS, bt709);
			lms = pow(abs(lms), 1.0 / 3.0) * sign(lms);
			return mul(OKLABLMS_2_OKLAB, lms);
		}

		static const float3x3 OKLAB_2_OKLABLMS = {
			1.f, 0.3963377774f, 0.2158037573f,
			1.f, -0.1055613458f, -0.0638541728f,
			1.f, -0.0894841775f, -1.2914855480f
		};
		static const float3x3 OKLABLMS_2_BT709 = {
			4.0767416621f, -3.3077115913f, 0.2309699292f,
			-1.2684380046f, 2.6097574011f, -0.3413193965f,
			-0.0041960863f, -0.7034186147f, 1.7076147010f
		};
		float3 OkLabToBT709(float3 oklab)
		{
			float3 lms = mul(OKLAB_2_OKLABLMS, oklab);
			lms = lms * lms * lms;
			return mul(OKLABLMS_2_BT709, lms);
		}

		float3 OkLabToOkLCh(float3 oklab)
		{
			float l = oklab.x;
			float a = oklab.y;
			float b = oklab.z;
			return float3(l, sqrt((a * a) + (b * b)), atan2(b, a));
		}

		float3 OkLChToOkLab(float3 oklch)
		{
			float l = oklch.x;
			float c = oklch.y;
			float h = oklch.z;
			return float3(l, c * cos(h), c * sin(h));
		}

		float3 OkLChToBT709(float3 oklch)
		{
			float3 oklab = OkLChToOkLab(oklch);
			return OkLabToBT709(oklab);
		}

		// from Pumbo
		// 0 None
		// 1 Reduce saturation and increase brightness until luminance is >= 0
		// 2 Clip negative colors (makes luminance >= 0)
		// 3 Snap to black
		void FixColorGradingLUTNegativeLuminance(inout float3 col, uint type = 1)
		{
			if (type <= 0) {
				return;
			}

			float luminance = RGBToLuminance(col.xyz);
			if (luminance < -FLT_MIN)  // 1.175494351e-38f
			{
				if (type == 1) {
					// Make the color more "SDR" (less saturated, and thus less beyond Rec.709) until the luminance is not negative anymore (negative luminance means the color was beyond Rec.709 to begin with, unless all components were negative).
					// This is preferrable to simply clipping all negative colors or snapping to black, because it keeps some HDR colors, even if overall it's still "black", luminance wise.
					// This should work even in case "positiveLuminance" was <= 0, as it will simply make the color black.
					float3 positiveColor = max(col.xyz, 0.0);
					float3 negativeColor = min(col.xyz, 0.0);
					float positiveLuminance = RGBToLuminance(positiveColor);
					float negativeLuminance = RGBToLuminance(negativeColor);
#pragma warning(disable: 4008)
					float negativePositiveLuminanceRatio = positiveLuminance / -negativeLuminance;
#pragma warning(default: 4008)
					negativeColor.xyz *= negativePositiveLuminanceRatio;
					col.xyz = positiveColor + negativeColor;
				} else if (type == 2) {
					// This can break gradients as it snaps colors to brighter ones (it depends on how the displays clips HDR10 or scRGB invalid colors)
					col.xyz = max(col.xyz, 0.0);
				} else  // if (type >= 3)
				{
					col.xyz = 0.0;
				}
			}
		}

		float3 Hue(float3 incorrect_color, float3 correct_color, float strength = 1.f)
		{
			if (strength == 0.f)
				return incorrect_color;

			float3 correct_lab = BT709ToOKLab(correct_color);
			float3 correct_lch = OkLabToOkLCh(correct_lab);

			float3 incorrect_lab = BT709ToOKLab(incorrect_color);
			float3 incorrect_lch = OkLabToOkLCh(incorrect_lab);
			if (strength == 1.f) {
				incorrect_lch[2] = correct_lch[2];
			} else {
				float old_chroma = incorrect_lch[1];

				incorrect_lab.yz = lerp(incorrect_lab.yz, correct_lab.yz, strength);
				incorrect_lch = OkLabToOkLCh(incorrect_lab);
				incorrect_lch[1] = old_chroma;
			}

			float3 color = OkLChToBT709(incorrect_lch);

			return color;
		}

	}  // namespace Correct
}

// ============================================================================
// DICE Tonemapping Support
// Adapted from Luma Framework (MIT License - Copyright (c) 2024+ Filippo Tarpini)
// https://github.com/Filoppi/Luma-Framework
// ============================================================================

static const float sRGB_WhiteLevelNits = 80.0;
static const float HDR10_MaxWhiteNits = 10000.0;

#define GCT_NONE 0
#define GCT_POSITIVE 1
#define GCT_SATURATE 2
#define GCT_MIRROR 3
#define GCT_DEFAULT GCT_NONE

#define CS_BT709 0
#define CS_BT2020 1
#define CS_DEFAULT CS_BT709

static const float3 Rec2020_Luminance = float3(0.2627066, 0.6779996, 0.0592938);

static const float3x3 DICE_BT709_2_BT2020 = {
	0.627403914928436279296875f, 0.3292830288410186767578125f, 0.0433130674064159393310546875f,
	0.069097287952899932861328125f, 0.9195404052734375f, 0.011362315155565738677978515625f,
	0.01639143936336040496826171875f, 0.08801330626010894775390625f, 0.895595252513885498046875f
};

static const float3x3 DICE_BT2020_2_BT709 = {
	1.66049098968505859375f, -0.58764111995697021484375f, -0.072849862277507781982421875f,
	-0.12455047667026519775390625f, 1.13289988040924072265625f, -0.0083494223654270172119140625f,
	-0.01815076358616352081298828125f, -0.100578896701335906982421875f, 1.11872971057891845703125f
};

static const float DICE_PQ_M1 = 0.1593017578125;
static const float DICE_PQ_M2 = 78.84375;
static const float DICE_PQ_C1 = 0.8359375;
static const float DICE_PQ_C2 = 18.8515625;
static const float DICE_PQ_C3 = 18.6875;

float max3(float3 v) { return max(v.x, max(v.y, v.z)); }
float min3(float3 v) { return min(v.x, min(v.y, v.z)); }
float average(float3 v) { return (v.x + v.y + v.z) / 3.0; }
float safeDivision(float a, float b, float fallback = 0) { return (b == 0) ? fallback : (a / b); }
float3 safeDivision(float3 a, float3 b, float3 fallback = 0)
{
	return float3(
		(b.x == 0) ? fallback.x : (a.x / b.x),
		(b.y == 0) ? fallback.y : (a.y / b.y),
		(b.z == 0) ? fallback.z : (a.z / b.z));
}
float Sign_Fast(float x) { return (x >= 0) ? 1.0 : -1.0; }
float3 Sign_Fast(float3 x) { return float3(Sign_Fast(x.x), Sign_Fast(x.y), Sign_Fast(x.z)); }

float GetLuminance(float3 color, uint colorSpace = CS_DEFAULT)
{
	return (colorSpace == CS_BT2020) ? dot(color, Rec2020_Luminance) : Color::RGBToLuminance(color);
}

float3 FromColorSpaceToColorSpace(float3 color, uint colorSpaceIn, uint colorSpaceOut)
{
	if (colorSpaceIn == CS_BT709 && colorSpaceOut == CS_BT2020)
		return mul(DICE_BT709_2_BT2020, color);
	if (colorSpaceIn == CS_BT2020 && colorSpaceOut == CS_BT709)
		return mul(DICE_BT2020_2_BT709, color);
	return color;
}

float3 Linear_to_PQ(float3 c, int clampType = GCT_DEFAULT)
{
	float3 s = Sign_Fast(c);
	if (clampType == GCT_POSITIVE)
		c = max(c, 0.0);
	else if (clampType == GCT_SATURATE)
		c = saturate(c);
	else if (clampType == GCT_MIRROR)
		c = abs(c);
	float3 p = pow(c, DICE_PQ_M1);
	float3 pqResult = pow((DICE_PQ_C1 + DICE_PQ_C2 * p) / (1.0 + DICE_PQ_C3 * p), DICE_PQ_M2);
	return (clampType == GCT_MIRROR) ? pqResult * s : pqResult;
}

float3 PQ_to_Linear(float3 c, int clampType = GCT_DEFAULT)
{
	float3 s = Sign_Fast(c);
	if (clampType == GCT_POSITIVE)
		c = max(c, 0.0);
	else if (clampType == GCT_SATURATE)
		c = saturate(c);
	else if (clampType == GCT_MIRROR)
		c = abs(c);
	float3 p = pow(c, 1.0 / DICE_PQ_M2);
	float3 lin = pow(max(p - DICE_PQ_C1, 0.0) / (DICE_PQ_C2 - DICE_PQ_C3 * p), 1.0 / DICE_PQ_M1);
	return (clampType == GCT_MIRROR) ? lin * s : lin;
}

#endif  //__COLOR_DEPENDENCY_HLSL__
