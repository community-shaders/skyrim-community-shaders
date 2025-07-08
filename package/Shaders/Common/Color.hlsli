#ifndef __COLOR_DEPENDENCY_HLSL__
#define __COLOR_DEPENDENCY_HLSL__

#include "Common/Math.hlsli"
#define FLT_MIN asfloat(0x00800000)  // 1.175494351e-38f

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
		return pow(color, 1.8);
	}

	float3 LinearToGamma(float3 color)
	{
		return pow(color, 1.0 / 1.8);
	}

	float3 GammaToTrueLinear(float3 color)
	{
		return pow(color, 2.2);
	}

	float3 TrueLinearToGamma(float3 color)
	{
		return pow(color, 1.0 / 2.2);
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
			float3 y_m1 = pow(color, M1);
			return pow((C1 + C2 * y_m1) / (1.f + C3 * y_m1), M2);
		}

		float3 Decode(float3 color, float scaling = 10000.f)
		{
			float3 e_m12 = pow(color, 1.f / M2);
			float3 out_color = pow(max(0, e_m12 - C1) / (C2 - C3 * e_m12), 1.f / M1);
			return out_color * (10000.f / scaling);
		}

	}  // namespace pq

	float3 Diffuse(float3 color)
	{
#if defined(TRUE_PBR)
		return pow(abs(color), 1.0 / 2.2);
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

	namespace Correct
	{
		float3 returncolor(float3 color)
		{
			return color;
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

#endif  //__COLOR_DEPENDENCY_HLSL__
