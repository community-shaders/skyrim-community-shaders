#include "Common/Color.hlsli"
#include "Common/DummyVSTexCoord.hlsl"
#include "Common/FrameBuffer.hlsli"
#include "Common/Math.hlsli"
#include "Common/SharedData.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color: SV_Target0;
};

#if defined(PSHADER)
SamplerState ImageSampler : register(s0);
#	if defined(DOWNSAMPLE)
SamplerState AdaptSampler : register(s1);
#	elif defined(BLEND)
SamplerState BlendSampler : register(s1);
#	endif
SamplerState AvgSampler : register(s2);

Texture2D<float4> ImageTex : register(t0);
#	if defined(DOWNSAMPLE)
Texture2D<float4> AdaptTex : register(t1);
#	elif defined(BLEND)
Texture2D<float4> BlendTex : register(t1);
#	endif
Texture2D<float4> AvgTex : register(t2);

cbuffer PerGeometry : register(b2)
{
	float4 Flags : packoffset(c0);
	float4 TimingData : packoffset(c1);
	float4 Param : packoffset(c2);      ///< .x=bloom intensity, .y=tonemap white point, .z=use HejlBurgessDawson
	float4 Cinematic : packoffset(c3);  ///< .x=saturation, .z=contrast, .w=intensity
	float4 Tint : packoffset(c4);       ///< .xyz=tint color, .w=tint amount
	float4 Fade : packoffset(c5);       ///< .xyz=fade color, .w=fade amount
	float4 BlurScale : packoffset(c6);
	float4 BlurOffsets[16] : packoffset(c7);
};

float ReinhardDerivative(float x, float p)
{
	return (p * x * x + 2.0 * p * x + 1.0) / ((x + 1.0) * (x + 1.0));
}

// finds f'(x) = x for the Reinhard operator, which can be used as a branching point for piecewise tonemapping to preserve highlights
float ReinhardFindBranchingPoint(float p)
{
	float inner = 31.0 - 46.0 * p + 27.0 * p * p - 8.0 * p * p * p - 4.0 * p * p * p * p;
	float A = 29.0 - 21.0 * p + 6.0 * p * p + 2.0 * p * p * p + 3.0 * sqrt(3.0) * sqrt(inner);

	float cbrtA = pow(abs(A), 1.0 / 3.0);
	float cbrt2 = pow(2.0, 1.0 / 3.0);

	return (p - 2.0) / 3.0 - (-1.0 - 2.0 * p - p * p) / (3.0 * cbrt2 * cbrt2 * cbrtA) + cbrtA / (3.0 * cbrt2);
}

/// Reinhard tonemapping operator
float3 GetTonemapFactorReinhard(float3 luminance, bool isHDR = false)
{
	float p = Param.y;
	float3 tonemapped = (luminance * (luminance * p + 1)) / (luminance + 1);

	if (isHDR && p < 1.0) {
		float x0 = ReinhardFindBranchingPoint(p);
		float y0 = (x0 * (x0 * p + 1.0)) / (x0 + 1.0);

		float m = ReinhardDerivative(x0, p);
		float b = y0 - m * x0;

		float3 extended = m * luminance + b;

		tonemapped = lerp(tonemapped, extended, step(x0, luminance));
	}

	return tonemapped;
}

/// Hejl-Burgess-Dawson filmic tonemapping operator
/// modified to output in linear instead of gamma
/// includes an HDR mode that skips the highlight rolloff
float3 GetTonemapFactorHejlBurgessDawson(float3 luminance, bool isHDR = false)
{
	float3 tmp = max(0, luminance - 0.004);
	float3 color = Color::SrgbToLinear(((tmp * 6.2 + 0.5) * tmp) / (tmp * (tmp * 6.2 + 1.7) + 0.06));

	if (isHDR)  // branch before shoulder (f''(x) = 0)
	{
		color = (luminance < 0.0843247172) ? color : (1.47829915 * luminance - 0.0321621545);
	}

	return Param.y * color;
}

#	include "Common/DisplayMapping.hlsli"

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

#	if defined(DOWNSAMPLE)
	float3 downsampledColor = 0;
	for (int sampleIndex = 0; sampleIndex < DOWNSAMPLE; ++sampleIndex) {
		float2 texCoord = BlurOffsets[sampleIndex].xy * BlurScale.xy + input.TexCoord;

		[branch] if (Flags.x > 0.5)
		{
			texCoord = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(texCoord);
		}

		float3 imageColor = clamp(ImageTex.Sample(ImageSampler, texCoord).xyz, 0.0, 50.0);  // Clamp to reasonable HDR bounds

#		if defined(RGB2LUM)
		imageColor = Color::RGBToLuminance(imageColor);
#		elif (defined(LUM) || defined(LUMCLAMP)) && !defined(DOWNADAPT)
		imageColor = imageColor.x;
#		endif
		downsampledColor += imageColor * BlurOffsets[sampleIndex].z;
	}

#		if defined(DOWNADAPT)
	float2 adaptValue = max(0.001, AdaptTex.Sample(AdaptSampler, input.TexCoord).xy);
	float2 adaptDelta = downsampledColor.xy - adaptValue;
	downsampledColor.xy =
		sign(adaptDelta) * clamp(abs(Param.wz * adaptDelta), 0.00390625, abs(adaptDelta)) +
		adaptValue;
#		endif
	psout.Color = float4(downsampledColor, BlurScale.z);

#	elif defined(BLEND)
	float2 uv = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);

	float3 inputColor = BlendTex.Sample(BlendSampler, uv).xyz;

	float3 bloomColor = 0;
	if (Flags.x > 0.5) {
		bloomColor = ImageTex.Sample(ImageSampler, uv).xyz;
	} else {
		bloomColor = ImageTex.Sample(ImageSampler, input.TexCoord.xy).xyz;
	}

	float2 avgValue = AvgTex.Sample(AvgSampler, input.TexCoord.xy).xy;

	float4 hdrShared = SharedData::HDRData;
	bool isHDR = hdrShared.x > 0.5;
	float menuSceneEncoding = hdrShared.w;
	static const float MENU_SCENE_ISHDR_BYPASS_THRESHOLD = 0.9;  // encoding 1.0 == main/loading
	if (menuSceneEncoding > MENU_SCENE_ISHDR_BYPASS_THRESHOLD)
		isHDR = false;

	float3 outputColor = 0.0;

	if (avgValue.x != 0 && avgValue.y != 0)
		inputColor *= avgValue.y / avgValue.x;
	inputColor = max(0, inputColor);

	float3 blendedColor;

	[branch] if (Param.z > 0.5)
	{
		blendedColor = DisplayMapping::HuePreservingHejlBurgessDawson(inputColor, bloomColor, isHDR);
	}
	else
	{
		float maxCol = Color::RGBToLuminance(inputColor);
		float mappedMax = GetTonemapFactorReinhard(maxCol, isHDR).x;
		float3 compressedHuePreserving = inputColor * mappedMax / maxCol;
		blendedColor = compressedHuePreserving;
		// SDR uses a hard cutoff (Param.x - blendedColor) so legacy weather mods that tuned
		// bloom intensity against this shoulder don't get blown-out highlights. HDR keeps the
		// soft-saturation form (1 - exp2(-x)) which bleeds bloom into specular peaks intentionally.
		float3 bloomMask = isHDR ? saturate(Param.x - (1.0 - exp2(-blendedColor)))
		                         : saturate(Param.x - blendedColor);
		blendedColor += bloomMask * bloomColor;
	}

	float blendedLuminance = Color::RGBToLuminance(blendedColor);
	float3 tintedColor = Cinematic.w * lerp(lerp(blendedLuminance, blendedColor, Cinematic.x), blendedLuminance * Tint.xyz, Tint.w).xyz;
	float3 contrastedColor = lerp(avgValue.x, tintedColor, Cinematic.z);

	// Contrast modified to fix crushed shadows
	float safeAvgValue = max(avgValue.x, EPSILON_DIVISION);
	float3 contrastedColorModified = pow(max(0.0, abs(tintedColor) / safeAvgValue), Cinematic.z) * safeAvgValue * sign(tintedColor);
	contrastedColor = lerp(contrastedColorModified, contrastedColor, saturate(contrastedColorModified / 0.1f));  // blend in modified contrast for shadows

	outputColor = contrastedColor;

#		if defined(FADE)
	outputColor = lerp(outputColor, Fade.xyz, Fade.w);
#		endif

	if (SharedData::linearLightingSettings.enableLinearLighting && SharedData::linearLightingSettings.enableGammaCorrection) {
		outputColor = Color::GammaToLinearSafe(outputColor);
	}

	if (isHDR) {
		if (!ENABLE_LL)
			outputColor = Color::GammaToLinearSafe(outputColor);
		float paperWhiteNits = max(hdrShared.y, 1e-6);
		float peakWhiteRatio = max(hdrShared.z / paperWhiteNits, 1.0);  // peakNits / paperWhite

		// reduce highlights
		float y_in = Color::RGBToLuminance(outputColor);
		float highlight_start = 1.f;
		float y_in_normalized = y_in / highlight_start;
		float y_out = (y_in_normalized > 1.0) ? pow(max(0.0, y_in_normalized), 0.85) : y_in_normalized;
		y_out *= highlight_start;
		float scale = (y_in > 0.0) ? (y_out / y_in) : 0.0;
		outputColor *= scale;

		// force stronger hue shift
		outputColor = Color::Correct::Hue(outputColor, DisplayMapping::RangeCompress(outputColor, 1.f, 6.f), 0.25f);

		// map to display peak
		outputColor = Color::BT709ToBT2020(outputColor);
		outputColor = exp2(DisplayMapping::RangeCompress(log2(max(0, outputColor)), log2(0.4 * peakWhiteRatio), log2(peakWhiteRatio), log2(100.f)));
		outputColor = Color::BT2020ToBT709(outputColor);
		if (!ENABLE_LL)
			outputColor = Color::LinearToGammaSafe(outputColor);
	} else {
		outputColor = max(0, outputColor);
		outputColor = FrameBuffer::ToSRGBColor(outputColor);
	}

	// outputColor = blendedColor; // debug: bypass color grading and hdr display mapping

	psout.Color = float4(outputColor, 1.0);

#	endif

	return psout;
}
#endif
