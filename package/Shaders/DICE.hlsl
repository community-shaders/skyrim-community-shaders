// Licenced under MIT
// Copyright (c) 2024+ Filippo Tarpini
// https://github.com/Filoppi/Luma-Framework/blob/main/Shaders/Includes/DICE.hlsl

#ifndef SRC_DICE_HLSL
#define SRC_DICE_HLSL

#include "Common/Color.hlsli"

// Compresses by luminance in rgb linear space. Highlights compression is too weak and stuff clips.
#define DICE_TYPE_BY_LUMINANCE_RGB 0
// Doing the DICE compression in PQ (either on luminance or each color channel) produces a curve that is closer to our "perception" and leaves more detail highlights without overly compressing them
// This is almost identical to doing it in log space, but might be slightly more accurate to our perception (https://www.desmos.com/calculator/886c46d2ef).
#define DICE_TYPE_BY_LUMINANCE_PQ 1
// Modern HDR displays clip individual rgb channels beyond their "white" peak brightness,
// like, if the peak brightness is 700 nits, any r g b color beyond a value of 700/80 will be clipped (not acknowledged, it won't make a difference).
// Tonemapping by luminance, is generally more perception accurate but can then generate rgb colors "out of range". This setting fixes them up,
// though it's optional as it's working based on assumptions on how current displays work, which might not be true anymore in the future.
// Note that this can create some steep (rough, quickly changing) gradients on very bright colors.
#define DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE 2
// Same as "DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE" but before that, it also calculates a tonemap by channel
// and restores the chrominance of that on the luminance tonemap, this simultaneously avoids hue shifts,
// while keeping a nice highlights rolloff (desaturation), that looks natural.
// The correction is still useful to avoid hue shifts from channels that go outside the display range.
#define DICE_TYPE_BY_LUMINANCE_PQ_WITH_BY_CHANNEL_CHROMINANCE_PLUS_CORRECT_CHANNELS_BEYOND_PEAK_WHITE 3
// Similar to "DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE" but with a gamma pow instead of PQ.
// Theoretically PQ better follow the minimum perceivable different in brightness, but pow (gamma) better tracks
// the doubling of brightness in our perception (e.g. gammaColor*2 is roughly equal to doubling in our perception).
// Hence it makes it the perfect space to do highlights compression in.
#define DICE_TYPE_BY_LUMINANCE_GAMMA_CORRECT_CHANNELS_BEYOND_PEAK_WHITE 4
// This might look more like classic SDR tonemappers and is closer to how modern TVs and Monitors play back colors (usually they clip each individual channel to the peak brightness value, though in their native panel color space, or current SDR/HDR mode color space).
// Overall, this seems to handle bright gradients more smoothly, even if it shifts hues more (and generally desaturating).
#define DICE_TYPE_BY_CHANNEL_PQ 5
// TODO: split these into different settings, given almost all combinations are possible? And try "DICE_TYPE_BY_LUMINANCE_GAMMA_CORRECT_CHANNELS_BEYOND_PEAK_WHITE" more

struct DICESettings
{
  uint Type;

  // The max value the input could ever had, or anyway the max we want to clip it to.
  // This can help either discarding too high values that would be garbage (depending on the rendering math),
  // or to make sure we don't waste any output range allocated to input values that are never reached by the game rendering.
  float InputMax;
  // Determines where the highlights curve (shoulder) starts, relatively to the peak (this is a 0-1 value). The main property of this tonemapper.
  // Values between 0.25 and 0.5 are good with PQ types (any type).
  // It automatically scales by your peak in PQ types, though you might want to increase it in SDR to clip more, instead of keeping a large range for soft highlights.
  // With linear/rgb types this barely makes a difference, zero is a good default but (e.g.) 0.5 would also work.
  // Set to "PaperWhite/PeakWhite" to make it start from "paper white", meaning we'd leave the original SDR range untouched.
  float ShoulderStart;

  uint InOutColorSpace;
  // Best set to the display color space, like BT.2020 in HDR
  uint ProcessingColorSpace;

  // Tonemap negative colors as well (depending on the mode). It might better compress out of gamut colors.
  // This also forces gamut mapping of negative colors.
  bool Mirrored;

  // Controls the amount of desaturation (1) vs darkening (0) to contain the final RGB color within the peak, when tonemapping by luminance.
  // Darkening can generally flatten detail so it's not that suggested unless needed for a specific reason.
  // For types "DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE", "DICE_TYPE_BY_LUMINANCE_PQ_WITH_BY_CHANNEL_CHROMINANCE_PLUS_CORRECT_CHANNELS_BEYOND_PEAK_WHITE" and "DICE_TYPE_BY_LUMINANCE_GAMMA_CORRECT_CHANNELS_BEYOND_PEAK_WHITE" only.
  float DesaturationVsDarkeningRatio;
  // If we do gamut mapping, we need to kind of smoothing threshold to start it before it becomes necessary, to avoid steps in gradients (e.g. we compress 1.01 to 1, then we also compress 0.99 to 0.985 or something).
  // Set to <0 to set it automatically based on the highlights shoulder.
  float GamutMappingSmoothing;

  // Perceptual exponent for "DICE_TYPE_BY_LUMINANCE_GAMMA_CORRECT_CHANNELS_BEYOND_PEAK_WHITE" (values between 2 and 3 are generally good)
  float Gamma;

  // 0 to 1 for desat. Lower than 1 for sat.
  // Often also called "blowout".
  // Only applies for some types.
  float HighlightsDesaturation;
};

namespace DICE
{
  // Applies exponential ("Photographic") luminance/luma compression.
  // The max is the max possible range to compress from, to not lose any output range if the input range was limited. Anything above is forcefully clipped as it's unexpected.
  float3 RangeCompress(float3 X, float Max = FLT_MAX)
  {
    // This does e^X. We expect X to be between 0 and 1.
	  float3 compression = 1.f - exp(-X);

    // Branches are for static parameters optimizations
    if (Max == FLT_MAX)
    {
      return compression;
    }

    const float maxCompression = 1.f - exp(-Max);
  #if 1
    return compression / maxCompression;
  #else // Smoother blend to avoid fast curve changes around the shoulder start (note: this isn't necessary until proven otherwise)
    return lerp(compression, compression / maxCompression, saturate(compression));
  #endif
  }
  float RangeCompress(float X, float Max = FLT_MAX)
  {
    return RangeCompress(X.xxx, Max).x;
  }

  // Refurbished DICE HDR tonemapper (per channel or luminance).
  // Expects "InValue" to be >= "ShoulderStart" and "OutMaxValue" to be > "ShoulderStart".
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
    const float3 possibleOutValue = ShoulderStart + compressedRange * RangeCompress(compressableValue / compressedRange, ConsiderMaxValue ? (compressableRange / compressedRange) : FLT_MAX);
#if 1
    return possibleOutValue;
#else // Enable this branch if "InValue" can be smaller than "ShoulderStart"
    return (InValue <= ShoulderStart) ? InValue : possibleOutValue;
#endif
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

  float3 ToPerceptual(float3 color, DICESettings Settings, int clampType = GCT_DEFAULT)
  {
    if (Settings.Type == DICE_TYPE_BY_LUMINANCE_PQ || Settings.Type == DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE || Settings.Type == DICE_TYPE_BY_LUMINANCE_PQ_WITH_BY_CHANNEL_CHROMINANCE_PLUS_CORRECT_CHANNELS_BEYOND_PEAK_WHITE || Settings.Type == DICE_TYPE_BY_CHANNEL_PQ)
    {
      return Linear_to_PQ(color, clampType); // "color" is expected to already be normalized to 0-1
    }
    else if (Settings.Type == DICE_TYPE_BY_LUMINANCE_GAMMA_CORRECT_CHANNELS_BEYOND_PEAK_WHITE)
    {
      return linear_to_gamma(color, clampType, Settings.Gamma);
    }
    return color;
  }

  float3 FromPerceptual(float3 color, DICESettings Settings, int clampType = GCT_DEFAULT)
  {
    if (Settings.Type == DICE_TYPE_BY_LUMINANCE_PQ || Settings.Type == DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE || Settings.Type == DICE_TYPE_BY_LUMINANCE_PQ_WITH_BY_CHANNEL_CHROMINANCE_PLUS_CORRECT_CHANNELS_BEYOND_PEAK_WHITE || Settings.Type == DICE_TYPE_BY_CHANNEL_PQ)
    {
      return PQ_to_Linear(color, clampType); // "color" is expected to be denormalized from 0-1 externally
    }
    else if (Settings.Type == DICE_TYPE_BY_LUMINANCE_GAMMA_CORRECT_CHANNELS_BEYOND_PEAK_WHITE)
    {
      return gamma_to_linear(color, clampType, Settings.Gamma);
    }
    return color;
  }
}

DICESettings DefaultDICESettings(uint Type = DICE_TYPE_BY_CHANNEL_PQ)
{
  DICESettings Settings;
  Settings.Type = Type;
  Settings.InputMax = FLT_MAX; // Ignored by default
  Settings.ShoulderStart = (Settings.Type > DICE_TYPE_BY_LUMINANCE_RGB) ? (1.0 / 3.0) : 0.0; // Setting it higher than 1/3 might cause highlights clipping as detail is too compressed. Setting it lower than 1/4 would probably look dynamic range. 1/3 seems like the best compromise. There's usually no need to start compressing from paper white just to keep the SDR range unchanged.
  Settings.GamutMappingSmoothing = 0.2;
  Settings.Mirrored = true;
  Settings.InOutColorSpace = CS_BT709;
  Settings.ProcessingColorSpace = CS_BT2020; // Work in BT.2020 by default, to match HDR displays primaries. Set to BT.709 for SDR.
  Settings.DesaturationVsDarkeningRatio = 1.0; // TODOFT5: make it 0 for all when doing highlights containment around the code!!! (actually 1, that should be the full desat value)
  Settings.Gamma = 1.0 / 2.5;
  Settings.HighlightsDesaturation = 0.0;
  return Settings;
}

// Tonemapper inspired from DICE. Can work by luminance to maintain hue.
// Takes scRGB/BT.2020 colors with a white level (the value of 1 1 1) of 80 nits (sRGB) (to not be confused with paper white).
// Paper white is expected to have already been multiplied in the color.
float3 DICETonemap(
  float3 Color,
  float PeakWhite,
  const DICESettings Settings /*= DefaultDICESettings()*/)
{
#if 0 // TODO: add these as separate modes. For now we TM by average, given that it should not ignore blue compression, preventing it from getting too bright and too white (either desaturated to be contained in the rgb peak, or clipped). Note that "CorrectOutOfRangeColor" isn't necessary when doing TM by peak.
  const float sourceLuminance = max3(FromColorSpaceToColorSpace(Color, Settings.InOutColorSpace, Settings.ProcessingColorSpace));
#elif 1 // This looks best! it's the most balanced
  const float sourceLuminance = average(FromColorSpaceToColorSpace(Color, Settings.InOutColorSpace, Settings.ProcessingColorSpace));
#else
  const float sourceLuminance = GetLuminance(Color, Settings.InOutColorSpace);
#endif
  const bool clipInputMax = Settings.InputMax != FLT_MAX;
  const float shoulderStart = Settings.ShoulderStart * PeakWhite; // From alpha to linear range

  if (Settings.Type != DICE_TYPE_BY_LUMINANCE_RGB)
  {
    const bool useGamma = Settings.Type == DICE_TYPE_BY_LUMINANCE_GAMMA_CORRECT_CHANNELS_BEYOND_PEAK_WHITE;
    static const float HDR10_MaxWhite = HDR10_MaxWhiteNits / sRGB_WhiteLevelNits;
    const float normalizationRange = useGamma ? 1.0 : HDR10_MaxWhite; // PQ requires normalization from a value of 1 being 80 nits (sRGB_WhiteLevelNits) to a value of 1 meaning 10000 nits.

    // We could first convert the peak white to PQ/gamma and then apply the "shoulder start" alpha to it (in PQ/gamma),
    // but tests showed scaling it in linear actually produces a better curve and more consistently follows the peak across different values
    const float shoulderStartPerceptual = DICE::ToPerceptual(shoulderStart / normalizationRange, Settings).x;
    const float inputMaxPerceptual = DICE::ToPerceptual(Settings.InputMax / normalizationRange, Settings).x;
    const float peakWhitePerceptual = DICE::ToPerceptual(PeakWhite / normalizationRange, Settings).x;
    
    // Convert to the display primaries, for example in HDR it's good to tonemap in BT.2020.
    // Theoretically some modes (e.g. "DICE_TYPE_BY_LUMINANCE_PQ") don't need this conversion as results would be identical, but we do it anyway for simplicity.
    Color = FromColorSpaceToColorSpace(Color, Settings.InOutColorSpace, Settings.ProcessingColorSpace);

    if (Settings.Type == DICE_TYPE_BY_LUMINANCE_PQ ||
        Settings.Type == DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE ||
        Settings.Type == DICE_TYPE_BY_LUMINANCE_PQ_WITH_BY_CHANNEL_CHROMINANCE_PLUS_CORRECT_CHANNELS_BEYOND_PEAK_WHITE ||
        Settings.Type == DICE_TYPE_BY_LUMINANCE_GAMMA_CORRECT_CHANNELS_BEYOND_PEAK_WHITE)
    {
      const float sourceLuminanceNormalized = sourceLuminance / normalizationRange;
      const float sourceLuminancePerceptual = DICE::ToPerceptual(sourceLuminanceNormalized, Settings, GCT_POSITIVE).x;

      if (sourceLuminancePerceptual > shoulderStartPerceptual) // Luminance below the shoulder (or below zero) don't need to be adjusted
      {
        const float3 originalColor = Color;

        const float compressedLuminancePerceptual = DICE::LuminanceCompress(sourceLuminancePerceptual, peakWhitePerceptual, shoulderStartPerceptual, clipInputMax, inputMaxPerceptual);
        const float compressedLuminanceNormalized = DICE::FromPerceptual(compressedLuminancePerceptual, Settings).x;
        Color *= compressedLuminanceNormalized / sourceLuminanceNormalized;

        if (Settings.Type == DICE_TYPE_BY_LUMINANCE_PQ_WITH_BY_CHANNEL_CHROMINANCE_PLUS_CORRECT_CHANNELS_BEYOND_PEAK_WHITE)
        {
          float3 perChannelTMColor = originalColor;
          const float3 sourceColorNormalized = perChannelTMColor / normalizationRange;
          const float3 sourceColorPerceptual = DICE::ToPerceptual(sourceColorNormalized, Settings, GCT_POSITIVE);

          // TODO: for consistency, we should pick a fixed alternative "peakWhitePerceptual" (and "shoulderStartPerceptual") here? Otherwise if we tonemap to 10k nits, per channel TM will basically be identical to TM by luminance, failing to desaturate highlights and failing to emulate the SDR per channel TM look, which is the primary reason for this method to exist
          const float3 compressedColorPerceptual = DICE::LuminanceCompress(sourceColorPerceptual, peakWhitePerceptual, shoulderStartPerceptual, clipInputMax, inputMaxPerceptual);
          const float3 compressedColorNormalized = DICE::FromPerceptual(compressedColorPerceptual, Settings);
          perChannelTMColor *= (sourceColorPerceptual > shoulderStartPerceptual) ? (compressedColorNormalized / sourceColorNormalized) : 1.0;

          Color = RestoreHueAndChrominance(Color, perChannelTMColor, 0.0, Settings.DesaturationVsDarkeningRatio, 0.0, FLT_MAX, 0.0, Settings.ProcessingColorSpace); // We don't expose the min/max chrominance changes given it should almost always be lower with TM by channel
        }

        // TODO: improve... this is very basic
        if (Settings.HighlightsDesaturation != 0.0)
        {
          float colorLuminance = GetLuminance(Color, Settings.ProcessingColorSpace);
          float colorMax = max3(Color); // We can't calculate the desaturation amount by ratio otherwise if we had a bright blue color it'd never trigger it (Edit: to do this, we need to move desaturation out of the branch above, otherwise there'd be steps)
          float desaturationRatio = saturate((colorMax - shoulderStart) / (PeakWhite - shoulderStart)); // Note: if we desaturate by luminance (instead of max), blues will desaturate a lot less
          //desaturationRatio = sqr(desaturationRatio); // Theoretically this should make the desaturation ratio smoother, but in reality it makes weaker and more stepped

#if 1

#if 1
          Color = lerp(Color, colorLuminance, desaturationRatio * Settings.HighlightsDesaturation);
#else // Looks worse, hue shift more
          Color = SetChrominance(Color, 1.0 - (desaturationRatio * Settings.HighlightsDesaturation));
#endif

#else

          float3 ucs = LINEAR_TO_UCS(Color, Settings.ProcessingColorSpace);

#if 1 // From RenoDX (it seems broken)
          float percentMax = saturate(sourceLuminance * 100.0 / 10000.0); // Caclulate desaturation on the original luminance, not the tonemapped one // TODO: why?
          // Positive = 1 to 0; Negative = 1 to 2
          float blowoutStrength = 100.0;
          float blowoutChange = pow(1.0 - percentMax, blowoutStrength * abs(Settings.HighlightsDesaturation));
          // Increase saturation in highlights (can look good too depending on the context)
          if (Settings.HighlightsDesaturation < 0)
          {
            blowoutChange = (2.0 - blowoutChange);
            //blowoutChange = 1.0 / blowoutChange; // TODO: alternative
          }
          ucs.yz *= blowoutChange;
#else // Simpler implementation
          ucs.yz *= 1.0 - (desaturationRatio * Settings.HighlightsDesaturation);
#endif

          Color = UCS_TO_LINEAR(ucs, Settings.ProcessingColorSpace);

#endif
        }
      }

      // We need to do this even if the tonemapper didn't do anything above, because (e.g.) blue might still go beyond peak even if the overall luminance is low
      if (Settings.Type == DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE || Settings.Type == DICE_TYPE_BY_LUMINANCE_PQ_WITH_BY_CHANNEL_CHROMINANCE_PLUS_CORRECT_CHANNELS_BEYOND_PEAK_WHITE || Settings.Type == DICE_TYPE_BY_LUMINANCE_GAMMA_CORRECT_CHANNELS_BEYOND_PEAK_WHITE)
      {
        float smoothing = 1.0 - Settings.ShoulderStart; // Match to shoulder start, just so we can re-use the parameter. 0.2 would be a good default otherwise.
        if (Settings.GamutMappingSmoothing >= 0.0)
          smoothing = Settings.GamutMappingSmoothing;
        Color = CorrectOutOfRangeColor(Color, Settings.Mirrored, true, Settings.DesaturationVsDarkeningRatio, PeakWhite, smoothing, Settings.ProcessingColorSpace);
      }
    }
    else // DICE_TYPE_BY_CHANNEL_PQ
    {
      float3 sourceColorNormalized = Color / normalizationRange;
      if (Settings.Mirrored)
        sourceColorNormalized = abs(sourceColorNormalized);
      const float3 sourceColorPerceptual = DICE::ToPerceptual(sourceColorNormalized, Settings, GCT_POSITIVE);

      const float3 compressedColorPerceptual = DICE::LuminanceCompress(sourceColorPerceptual, peakWhitePerceptual, shoulderStartPerceptual, clipInputMax, inputMaxPerceptual);
      const float3 compressedColorNormalized = DICE::FromPerceptual(compressedColorPerceptual, Settings);
      // Colors below the shoulder (or below zero) don't need to be adjusted
      Color *= (sourceColorPerceptual > shoulderStartPerceptual) ? (compressedColorNormalized / sourceColorNormalized) : 1.0;
    }
      
    Color = FromColorSpaceToColorSpace(Color, Settings.ProcessingColorSpace, Settings.InOutColorSpace);
  }
  else // DICE_TYPE_BY_LUMINANCE_RGB
  {
    if (sourceLuminance > shoulderStart) // Luminances below the shoulder (or below zero) don't need to be adjusted
    {
      const float compressedLuminance = DICE::LuminanceCompress(sourceLuminance, PeakWhite, shoulderStart, clipInputMax, Settings.InputMax);
      Color *= compressedLuminance / sourceLuminance;
    }
  }

  return Color;
}

#endif // SRC_DICE_HLSL