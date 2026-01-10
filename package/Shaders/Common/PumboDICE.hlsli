#define FLT16_MAX 65504.f
#define FLT_MAX asfloat(0x7F7FFFFF)  // 3.402823466e+38f
#define FLT_MIN asfloat(0x00800000)  // 1.175494351e-38f

/* MATH */

// Returns 1 or FLT_MAX if "dividend" is 0
float SafeDivision(float quotient, float dividend)
{
	return (dividend == 0.f) ? FLT_MAX * sign(quotient) : (quotient / dividend);
}

float SafeDivision(float quotient, float dividend, float fallback)
{
	return (dividend == 0.f) ? fallback : (quotient / dividend);
}

float3 SafeDivision(float3 quotient, float3 dividend)
{
	return float3(SafeDivision(quotient.x, dividend.x, FLT_MAX * sign(quotient.x)),
		SafeDivision(quotient.y, dividend.y, FLT_MAX * sign(quotient.y)),
		SafeDivision(quotient.z, dividend.z, FLT_MAX * sign(quotient.z)));
}

float3 SafeDivision(float3 quotient, float3 dividend, float3 fallback)
{
	return float3(SafeDivision(quotient.x, dividend.x, fallback.x),
		SafeDivision(quotient.y, dividend.y, fallback.y),
		SafeDivision(quotient.z, dividend.z, fallback.z));
}

/* COLOR */

static const float3x3 BT709_2_BT2020 = float3x3(
	0.627401924722236, 0.329291971755002, 0.0433061035227622,
	0.0690954897392608, 0.919544281267395, 0.0113602289933443,
	0.0163937090881632, 0.0880281623979006, 0.895578128513936);

static const float3x3 BT2020_2_BT709 = float3x3(
	1.66049621914783, -0.587656444131135, -0.0728397750166941,
	-0.124547095586012, 1.13289510924730, -0.00834801366128445,
	-0.0181536813870718, -0.100597371685743, 1.11875105307281);

static const float3x3 BT709_2_XYZ_MAT = float3x3(
	0.4123907993f, 0.3575843394f, 0.1804807884f,
	0.2126390059f, 0.7151686788f, 0.0721923154f,  // luminance bt709
	0.0193308187f, 0.1191947798f, 0.9505321522f);

static const float3x3 BT2020_2_XYZ_MAT = float3x3(
	0.6369580483f, 0.1446169036f, 0.1688809752f,
	0.2627002120f, 0.6779980715f, 0.0593017165f,  // luminance bt2020
	0.0000000000f, 0.0280726930f, 1.0609850577f);

float3 BT709_to_BT2020(float3 bt709Color)
{
	return mul(BT709_2_BT2020, bt709Color);
}

float3 BT2020_to_BT709(float3 bt2020Color)
{
	return mul(BT2020_2_BT709, bt2020Color);
}

float BT709_to_Luminance(float3 bt709Color)
{
	return dot(bt709Color, BT709_2_XYZ_MAT[1].rgb);
}

float BT2020_to_Luminance(float3 bt2020Color)
{
	return dot(bt2020Color, BT2020_2_XYZ_MAT[1].rgb);
}

float3 BT709_Clamp_BT2020(float3 bt709Color)
{
	float3 bt2020Color = max(0, BT709_to_BT2020(bt709Color));

	return BT2020_to_BT709(bt2020Color);
}

float max3(float a, float b, float c)
{
	return max(a, max(b, c));
}

float max3(float3 v)
{
	return max3(v.x, v.y, v.z);
}

static const float PQ_constant_M1 = 0.1593017578125f;
static const float PQ_constant_M2 = 78.84375f;
static const float PQ_constant_C1 = 0.8359375f;
static const float PQ_constant_C2 = 18.8515625f;
static const float PQ_constant_C3 = 18.6875f;

// PQ (Perceptual Quantizer - ST.2084) encode/decode used for HDR10 BT.2100.
// Clamp type:
// 0 None
// 1 Remove negative numbers
// 2 Remove numbers beyond 0-1
// 3 Mirror negative numbers
float3 Linear_to_PQ(float3 LinearColor, int clampType = 0)
{
	float3 LinearColorSign = sign(LinearColor);
	if (clampType == 1) {
		LinearColor = max(LinearColor, 0.f);
	} else if (clampType == 2) {
		LinearColor = saturate(LinearColor);
	} else if (clampType == 3) {
		LinearColor = abs(LinearColor);
	}
	float3 colorPow = pow(LinearColor, PQ_constant_M1);
	float3 numerator = PQ_constant_C1 + PQ_constant_C2 * colorPow;
	float3 denominator = 1.f + PQ_constant_C3 * colorPow;
	float3 pq = pow(numerator / denominator, PQ_constant_M2);
	if (clampType == 3) {
		return pq * LinearColorSign;
	}
	return pq;
}

float3 PQ_to_Linear(float3 ST2084Color, int clampType = 0)
{
	float3 ST2084ColorSign = sign(ST2084Color);
	if (clampType == 1) {
		ST2084Color = max(ST2084Color, 0.f);
	} else if (clampType == 2) {
		ST2084Color = saturate(ST2084Color);
	} else if (clampType == 3) {
		ST2084Color = abs(ST2084Color);
	}
	float3 colorPow = pow(ST2084Color, 1.f / PQ_constant_M2);
	float3 numerator = max(colorPow - PQ_constant_C1, 0.f);
	float3 denominator = PQ_constant_C2 - (PQ_constant_C3 * colorPow);
	float3 linearColor = pow(numerator / denominator, 1.f / PQ_constant_M1);
	if (clampType == 3) {
		return linearColor * ST2084ColorSign;
	}
	return linearColor;
}

// Aplies exponential ("Photographic") luminance/luma compression.
// The pow can modulate the curve without changing the values around the edges.
// The max is the max possible range to compress from, to not lose any output range if the input range was limited.
float rangeCompress(float X, float Max = asfloat(0x7F7FFFFF))
{
	// Branches are for static parameters optimizations
	if (Max == FLT16_MAX) {
		// This does e^X. We expect X to be between 0 and 1.
		return 1.f - exp(-X);
	}
	const float lostRange = exp(-Max);
	const float restoreRangeScale = 1.f / (1.f - lostRange);
	return (1.f - exp(-X)) * restoreRangeScale;
}

// Refurbished DICE HDR tonemapper (per channel or luminance).
// Expects "InValue" to be >= "ShoulderStart" and "OutMaxValue" to be > "ShoulderStart".
float luminanceCompress(
	float InValue,
	float OutMaxValue,
	float ShoulderStart = 0.f,
	bool considerMaxValue = false,
	float InMaxValue = asfloat(0x7F7FFFFF))
{
	const float compressableValue = InValue - ShoulderStart;
	const float compressableRange = InMaxValue - ShoulderStart;
	const float compressedRange = OutMaxValue - ShoulderStart;
	const float possibleOutValue = ShoulderStart + compressedRange * rangeCompress(compressableValue / compressedRange, considerMaxValue ? (compressableRange / compressedRange) : FLT16_MAX);
#if 1
	return possibleOutValue;
#else  // Enable this branch if "InValue" can be smaller than "ShoulderStart"
	return (InValue <= ShoulderStart) ? InValue : possibleOutValue;
#endif
}

#define DICE_TYPE_BY_LUMINANCE_RGB 0
// Doing the DICE compression in PQ (either on luminance or each color channel) produces a curve that is closer to our "perception" and leaves more detail highlights without overly compressing them
#define DICE_TYPE_BY_LUMINANCE_PQ 1
// Modern HDR displays clip individual rgb channels beyond their "white" peak brightness,
// like, if the peak brightness is 700 nits, any r g b color beyond a value of 700/80 will be clipped (not acknowledged, it won't make a difference).
// Tonemapping by luminance, is generally more perception accurate but can then generate rgb colors "out of range". This setting fixes them up,
// though it's optional as it's working based on assumptions on how current displays work, which might not be true anymore in the future.
// Note that this can create some steep (rough, quickly changing) gradients on very bright colors.
#define DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE 2
// This might look more like classic SDR tonemappers and is closer to how modern TVs and Monitors play back colors (usually they clip each individual channel to the peak brightness value, though in their native panel color space, or current SDR/HDR mode color space).
// Overall, this seems to handle bright gradients more smoothly, even if it shifts hues more (and generally desaturating).
#define DICE_TYPE_BY_CHANNEL_PQ 3

struct DICESettings
{
	uint Type;
	// Determines where the highlights curve (shoulder) starts.
	// Values between 0.25 and 0.5 are good with DICE by PQ (any type).
	// With linear/rgb DICE this barely makes a difference, zero is a good default but (e.g.) 0.5 would also work.
	// This should always be between 0 and 1.
	float ShoulderStart;

	// For "Type == DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE" only:
	// The sum of these needs to be <= 1, both within 0 and 1.
	// The closer the sum is to 1, the more each color channel will be containted within its peak range.
	float DesaturationAmount;
	float DarkeningAmount;
};

DICESettings DefaultDICESettings()
{
	DICESettings Settings;
	Settings.Type = DICE_TYPE_BY_CHANNEL_PQ;
	Settings.ShoulderStart = (Settings.Type >= DICE_TYPE_BY_LUMINANCE_RGB) ? (1.f / 4.f) : 0.f;
	Settings.DesaturationAmount = 1.0 / 3.0;
	Settings.DarkeningAmount = 1.0 / 3.0;
	return Settings;
}

// Tonemapper inspired from DICE. Can work by luminance to maintain hue.
// Takes scRGB colors with a white level (the value of 1 1 1) of 80 nits (sRGB) (to not be confused with paper white).
// Paper white is expected to have already been multiplied in.
float3 DICETonemap(
	float3 Color,
	float PeakWhite,
	const DICESettings Settings)
{
	const float sourceLuminance = BT709_to_Luminance(Color);

	if (Settings.Type != DICE_TYPE_BY_LUMINANCE_RGB) {
		static const float HDR10_MaxWhite = 10000.f / 80.f;

		const float shoulderStartPQ = Linear_to_PQ((Settings.ShoulderStart * PeakWhite) / HDR10_MaxWhite).x;
		if (Settings.Type == DICE_TYPE_BY_LUMINANCE_PQ || Settings.Type == DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE) {
			const float sourceLuminanceNormalized = sourceLuminance / HDR10_MaxWhite;
			const float sourceLuminancePQ = Linear_to_PQ(sourceLuminanceNormalized, 1).x;

			if (sourceLuminancePQ > shoulderStartPQ)  // Luminance below the shoulder (or below zero) don't need to be adjusted
			{
				const float peakWhitePQ = Linear_to_PQ(PeakWhite / HDR10_MaxWhite).x;

				const float compressedLuminancePQ = luminanceCompress(sourceLuminancePQ, peakWhitePQ, shoulderStartPQ);
				const float compressedLuminanceNormalized = PQ_to_Linear(compressedLuminancePQ).x;
				Color *= compressedLuminanceNormalized / sourceLuminanceNormalized;

				if (Settings.Type == DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE) {
					float3 Color_BT2020 = BT709_to_BT2020(Color);
					if (any(Color_BT2020 > PeakWhite))  // Optional "optimization" branch
					{
						float colorLuminance = BT2020_to_Luminance(Color_BT2020);
						float colorLuminanceInExcess = colorLuminance - PeakWhite;
						float maxColorInExcess = max3(Color_BT2020) - PeakWhite;                                                         // This is guaranteed to be >= "colorLuminanceInExcess"
						float brightnessReduction = saturate(SafeDivision(PeakWhite, max3(Color_BT2020), 1));                            // Fall back to one in case of division by zero
						float desaturateAlpha = saturate(SafeDivision(maxColorInExcess, maxColorInExcess - colorLuminanceInExcess, 0));  // Fall back to zero in case of division by zero
						Color_BT2020 = lerp(Color_BT2020, colorLuminance, desaturateAlpha * Settings.DesaturationAmount);
						Color_BT2020 = lerp(Color_BT2020, Color_BT2020 * brightnessReduction, Settings.DarkeningAmount);  // Also reduce the brightness to partially maintain the hue, at the cost of brightness
						Color = BT2020_to_BT709(Color_BT2020);
					}
				}
			}
		} else  // DICE_TYPE_BY_CHANNEL_PQ
		{
			const float peakWhitePQ = Linear_to_PQ(PeakWhite / HDR10_MaxWhite).x;

			// Tonemap in BT.2020 to more closely match the primaries of modern displays
			const float3 sourceColorNormalized = BT709_to_BT2020(Color) / HDR10_MaxWhite;
			const float3 sourceColorPQ = Linear_to_PQ(sourceColorNormalized, 1);

			for (uint i = 0; i < 3; i++)  //TODO LUMA: optimize? will the shader compile already convert this to float3? Or should we already make a version with no branches that works in float3?
			{
				if (sourceColorPQ[i] > shoulderStartPQ)  // Colors below the shoulder (or below zero) don't need to be adjusted
				{
					const float compressedColorPQ = luminanceCompress(sourceColorPQ[i], peakWhitePQ, shoulderStartPQ);
					const float compressedColorNormalized = PQ_to_Linear(compressedColorPQ).x;
					Color[i] = BT2020_to_BT709(Color[i] * (compressedColorNormalized / sourceColorNormalized[i])).x;
				}
			}
		}
	} else  // DICE_TYPE_BY_LUMINANCE_RGB
	{
		if (sourceLuminance > Settings.ShoulderStart)  // Luminance below the shoulder (or below zero) don't need to be adjusted
		{
			const float compressedLuminance = luminanceCompress(sourceLuminance, PeakWhite, PeakWhite * Settings.ShoulderStart);
			Color *= compressedLuminance / sourceLuminance;
		}
	}

	return Color;
}

/// Applies DICE tonemapper to the untonemapped HDR color.
///
/// @param untonemapped - The untonemapped color.
/// @param gameNits - The user set paper white.
/// @param peakNits - The user set peak white.
/// @param shoulder - The shoulder start of DICE.
/// @return The HDR color tonemapped with DICE.
float3 applyPumboDICE(float3 untonemapped, float gameNits = 203.0, float peakNits = 203.0, float shoulder = 0.5)
{
	// Declare DICE parameters
	DICESettings config = DefaultDICESettings();
	config.Type = 3;
	config.ShoulderStart = shoulder;
	const float dicePaperWhite = gameNits / 80.f;
	const float dicePeakWhite = peakNits / 80.f;

	// multiply paper white in for tonemapping and out for output
	return DICETonemap(untonemapped * dicePaperWhite, dicePeakWhite, config) / dicePaperWhite;
}
