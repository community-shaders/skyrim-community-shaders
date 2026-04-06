
#ifndef __DISPLAY_MAPPING_DEPENDENCY_HLSL__
#define __DISPLAY_MAPPING_DEPENDENCY_HLSL__

#include "Common/Color.hlsli"

namespace DisplayMapping
{
	// =============================================================================
	// DICE Tonemapper (Simplified for DICE_TYPE_BY_LUMINANCE_PQ)
	// Licenced under MIT
	// Copyright (c) 2024+ Filippo Tarpini
	// https://github.com/Filoppi/Luma-Framework/blob/main/Shaders/Includes/DICE.hlsl
	// =============================================================================

	namespace DICE
	{
		// Applies exponential ("Photographic") luminance compression.
		float3 RangeCompress(float3 X, float Max = FLT_MAX)
		{
			float3 compression = 1.f - exp(-X);
			if (Max == FLT_MAX)
				return compression;

			const float maxCompression = 1.f - exp(-Max);
			return compression / maxCompression;
		}

		float RangeCompress(float X, float Max = FLT_MAX)
		{
			return RangeCompress(X.xxx, Max).x;
		}

		// DICE HDR tonemapper per luminance.
		float3 LuminanceCompress(
			float3 InValue,
			float OutMaxValue,
			float ShoulderStart = 0.f,
			bool ConsiderMaxValue = false,
			float InMaxValue = FLT_MAX)
		{
			const float3 compressableValue = InValue - ShoulderStart;
			const float compressableRange = InMaxValue - ShoulderStart;
			const float compressedRange = OutMaxValue - ShoulderStart;
			return ShoulderStart + compressedRange * RangeCompress(compressableValue / compressedRange, ConsiderMaxValue ? (compressableRange / compressedRange) : FLT_MAX);
		}

		float LuminanceCompress(
			float InValue,
			float OutMaxValue,
			float ShoulderStart = 0.f,
			bool ConsiderMaxValue = false,
			float InMaxValue = FLT_MAX)
		{
			return LuminanceCompress(InValue.xxx, OutMaxValue, ShoulderStart, ConsiderMaxValue, InMaxValue).x;
		}
	}

	// Tonemapper inspired from DICE. Works by luminance to maintain hue.
	// Takes BT.709 colors with a white level of 80 nits (sRGB).
	// Paper white is expected to have already been multiplied in the color.
	// ShoulderStart: 0.0-1.0 ratio of PeakWhite where compression starts (e.g., 0.5 = start at 50% of peak)
	float3 DICETonemap(float3 Color, float PeakWhite, float ShoulderStart, uint InOutColorSpace, uint ProcessingColorSpace)
	{
		const float sourceLuminance = average(FromColorSpaceToColorSpace(Color, InOutColorSpace, ProcessingColorSpace));
		const float shoulderStart = ShoulderStart * PeakWhite;

		static const float HDR10_MaxWhite = HDR10_MaxWhiteNits / sRGB_WhiteLevelNits;

		const float shoulderStartPerceptual = Linear_to_PQ((shoulderStart / HDR10_MaxWhite).xxx, GCT_DEFAULT).x;
		const float peakWhitePerceptual = Linear_to_PQ((PeakWhite / HDR10_MaxWhite).xxx, GCT_DEFAULT).x;

		Color = FromColorSpaceToColorSpace(Color, InOutColorSpace, ProcessingColorSpace);

		const float sourceLuminanceNormalized = sourceLuminance / HDR10_MaxWhite;
		const float sourceLuminancePerceptual = Linear_to_PQ(sourceLuminanceNormalized.xxx, GCT_POSITIVE).x;

		if (sourceLuminancePerceptual > shoulderStartPerceptual) {
			const float compressedLuminancePerceptual = DICE::LuminanceCompress(sourceLuminancePerceptual, peakWhitePerceptual, shoulderStartPerceptual, false, FLT_MAX);
			const float compressedLuminanceNormalized = PQ_to_Linear(compressedLuminancePerceptual.xxx, GCT_DEFAULT).x;
			Color *= compressedLuminanceNormalized / sourceLuminanceNormalized;

			// Slight desaturation in compressed highlights (PQ-distance–weighted toward white).
			const float highlightT =
				saturate((sourceLuminancePerceptual - shoulderStartPerceptual) / max(peakWhitePerceptual - shoulderStartPerceptual, 1e-6));
			const float maxHighlightDesat = 0.5;
			float desat = highlightT * highlightT * maxHighlightDesat;
			float luma = Color::RGBToLuminance(max(Color, 0.0));
			Color = lerp(Color, luma.xxx, desat);
		}

		return FromColorSpaceToColorSpace(Color, ProcessingColorSpace, InOutColorSpace);
	}

	// =============================================================================
	// End DICE Tonemapper
	// =============================================================================

	// https://www.ea.com/frostbite/news/high-dynamic-range-color-grading-and-display-in-frostbite

	// Applies exponential ("Photographic") luma compression
	float RangeCompress(float x)
	{
		return 1.0 - exp(-x);
	}

    float RangeCompress(float val, float threshold, float maxValue)
    {
        if (val < threshold)
            return val;
        if (maxValue <= threshold)
            return threshold;

        float range = maxValue - threshold;
        return threshold + range * RangeCompress((val - threshold) / range);
    }

	float RangeCompress(float val, float threshold)
	{
		float v1 = val;
		float v2 = threshold + (1 - threshold) * RangeCompress((val - threshold) / (1 - threshold));
		return val < threshold ? v1 : v2;
	}

	float3 RangeCompress(float3 val, float threshold)
	{
		return float3(
			RangeCompress(val.x, threshold),
			RangeCompress(val.y, threshold),
			RangeCompress(val.z, threshold));
	}

    float3 RangeCompress(float3 val, float threshold, float maxValue)
    {
        return float3(
            RangeCompress(val.x, threshold, maxValue),
            RangeCompress(val.y, threshold, maxValue),
            RangeCompress(val.z, threshold, maxValue));
    }

    static const float PQ_constant_N = (2610.0 / 4096.0 / 4.0);
	static const float PQ_constant_M = (2523.0 / 4096.0 * 128.0);
	static const float PQ_constant_C1 = (3424.0 / 4096.0);
	static const float PQ_constant_C2 = (2413.0 / 4096.0 * 32.0);
	static const float PQ_constant_C3 = (2392.0 / 4096.0 * 32.0);

	// PQ (Perceptual Quantiser; ST.2084) encode/decode used for HDR TV and grading
	float3 LinearToPQ(float3 linearCol, const float maxPqValue)
	{
		linearCol /= maxPqValue;

		float3 colToPow = pow(linearCol, PQ_constant_N);
		float3 numerator = PQ_constant_C1 + PQ_constant_C2 * colToPow;
		float3 denominator = 1.0 + PQ_constant_C3 * colToPow;
		float3 pq = pow(numerator / denominator, PQ_constant_M);

		return pq;
	}

	float3 PQtoLinear(float3 linearCol, const float maxPqValue)
	{
		float3 colToPow = pow(abs(linearCol), 1.0 / PQ_constant_M);
		float3 numerator = max(colToPow - PQ_constant_C1, 0.0);
		float3 denominator = PQ_constant_C2 - (PQ_constant_C3 * colToPow);
		float3 linearColor = pow(abs(numerator / denominator), 1.0 / PQ_constant_N);

		linearColor *= maxPqValue;

		return linearColor;
	}

	// RGB with sRGB/Rec.709 primaries to CIE XYZ
	float3 RGBToXYZ(float3 c)
	{
		float3x3 mat = float3x3(
			0.4124564, 0.3575761, 0.1804375,
			0.2126729, 0.7151522, 0.0721750,
			0.0193339, 0.1191920, 0.9503041);
		return mul(mat, c);
	}
	float3 XYZToRGB(float3 c)
	{
		float3x3 mat = float3x3(
			3.24045483602140870, -1.53713885010257510, -0.49853154686848090,
			-0.96926638987565370, 1.87601092884249100, 0.04155608234667354,
			0.05564341960421366, -0.20402585426769815, 1.05722516245792870);
		return mul(mat, c);
	}
	// Converts XYZ tristimulus values into cone responses for the three types of cones in the human visual system, matching long, medium, and short wavelengths.
	// Note that there are many LMS color spaces; this one follows the ICtCp color space specification.
	float3 XYZToLMS(float3 c)
	{
		float3x3 mat = float3x3(
			0.3592, 0.6976, -0.0358,
			-0.1922, 1.1004, 0.0755,
			0.0070, 0.0749, 0.8434);
		return mul(mat, c);
	}
	float3 LMSToXYZ(float3 c)
	{
		float3x3 mat = float3x3(
			2.07018005669561320, -1.32645687610302100, 0.206616006847855170,
			0.36498825003265756, 0.68046736285223520, -0.045421753075853236,
			-0.04959554223893212, -0.04942116118675749, 1.187995941732803400);
		return mul(mat, c);
	}

	// RGB with sRGB/Rec.709 primaries to ICtCp
	float3 RGBToICtCp(float3 col)
	{
		col = RGBToXYZ(col);
		col = XYZToLMS(col);
		// 1.0f = 100 nits, 100.0f = 10k nits
		col = LinearToPQ(max(0.0.xxx, col), 100.0);

		// Convert PQ-LMS into ICtCp. Note that the "S" channel is not used,
		// but overlap between the cone responses for long, medium, and short wavelengths
		// ensures that the corresponding part of the spectrum contributes to luminance.

		float3x3 mat = float3x3(
			0.5000, 0.5000, 0.0000,
			1.6137, -3.3234, 1.7097,
			4.3780, -4.2455, -0.1325);

		return mul(mat, col);
	}

	float3 ICtCpToRGB(float3 col)
	{
		float3x3 mat = float3x3(
			1.0, 0.00860514569398152, 0.11103560447547328,
			1.0, -0.00860514569398152, -0.11103560447547328,
			1.0, 0.56004885956263900, -0.32063747023212210);
		col = mul(mat, col);

		// 1.0f = 100 nits, 100.0f = 10k nits
		col = PQtoLinear(col, 100.0);
		col = LMSToXYZ(col);
		return XYZToRGB(col);
	}

#if defined(PSHADER) && defined(BLEND)
	float3 HuePreservingHejlBurgessDawson(float3 col, float3 bloomCol, bool isHDR = false)
	{
		float3 ictcp = RGBToICtCp(col);

		// Hue-preserving range compression requires desaturation in order to achieve a natural look. We adaptively desaturate the input based on its luminance.
		float saturationAmount = pow(smoothstep(1.0, 0.3, ictcp.x), 1.3);
		col = ICtCpToRGB(ictcp * float3(1, saturationAmount.xx));

		// Non-hue preserving mapping
		float3 perChannelCompressed = GetTonemapFactorHejlBurgessDawson(col, isHDR);
		perChannelCompressed += saturate(Param.x - perChannelCompressed) * bloomCol;

		col = perChannelCompressed;

		float3 ictcpMapped = RGBToICtCp(col);

		// Smoothly ramp off saturation as brightness increases, but keep some even for very bright input
		float postCompressionSaturationBoost = 0.3 * smoothstep(1.0, 0.5, ictcp.x);

		// Re-introduce some hue from the pre-compression color. Something similar could be accomplished by delaying the luma-dependent desaturation before range compression.
		// Doing it here however does a better job of preserving perceptual luminance of highly saturated colors. Because in the hue-preserving path we only range-compress the max channel,
		// saturated colors lose luminance. By desaturating them more aggressively first, compressing, and then re-adding some saturation, we can preserve their brightness to a greater extent.
		ictcpMapped.yz = lerp(ictcpMapped.yz, ictcp.yz * ictcpMapped.x / max(1e-3, ictcp.x), postCompressionSaturationBoost);

		col = ICtCpToRGB(ictcpMapped);

		return col;
	}

#endif

} // namespace DisplayMapping

#endif  // __DISPLAY_MAPPING_DEPENDENCY_HLSL__