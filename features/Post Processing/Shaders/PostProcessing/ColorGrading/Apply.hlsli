#ifndef POST_PROCESSING_COLOR_GRADING_APPLY_HLSLI
#define POST_PROCESSING_COLOR_GRADING_APPLY_HLSLI

#include "Common/Color.hlsli"
#include "PostProcessing/common.hlsli"

// The same constants and LUT are used by the standalone and fused pixel shaders.
#ifdef COMPOSITE_APPLY_LUT
#	define COLOR_GRADING_CB b0
Texture3D<float4> TexLUT : register(t15);
SamplerState GradingSampler : register(s2);
#else
#	define COLOR_GRADING_CB b1
Texture3D<float4> TexLUT : register(t1);
SamplerState GradingSampler : register(s0);
#endif

#define LUT_SIZE 64

#include "PostProcessing/ColorGrading/Include/OpenDRTConfig.hlsli"

cbuffer ColorCB : register(COLOR_GRADING_CB)
{
	float4 asccdl[3];
	float4 liftgammagain[3];  // lift，gamma，gain
	float4 inOutGamma;        // .z = input gamma, .w = output gamma
	float4 oklchSaturation;
	float4 oklchColorMixer[7];
	float4 contrast;
	float4 pivot;
	float4 exposureTemperatureTint;
	float4 shadows;
	float4 midtones;
	float4 highlights;
	float4 shadowsHighlightsRange;  // shadowBegin, shadowEnd, highlightBegin, highlightEnd

	float4 tonemapParams[2];
	float4 inputToWorking[3];    // scene RGB → working color space
	float4 workingToTonemap[3];  // working → tonemapper native space
	float4 tonemapToOutput[3];   // tonemapper native → output space

	float4 workingToXYZ[3];  // working → XYZ D65 (white balance and Oklab)
	float4 xyzToWorking[3];  // XYZ D65 → working

	float4 workingWhitePoint;  // .xy = D65 reference white of the XYZ matrices

	float4 shadowsOffset;  // SMH color offsets
	float4 midtonesOffset;
	float4 highlightsOffset;

	// game value
	float4 cinematic;  // saturation, brightness, contrast
	float4 fade;       // color
	float4 tint;       // color

	uint logType;
	uint skipLDR;
	uint skipLUT;
	uint enableTonemap;
	uint enableColorSpaceTransform;
	uint enableHDR;           // HDR display is enabled (auto-set from HDR feature)
	float hdrPeakNits;        // Maximum display brightness in nits for HDR
	float hdrPaperWhiteNits;  // Reference white brightness in nits for HDR

	OpenDRTConfig odrtConfig;
};
#undef COLOR_GRADING_CB

float3 LogToLinear(float3 logColor)
{
	const float linearRange = 14.0f;
	const float linearGrey = 0.18f;
	const float exposureGrey = 444.0f;
	return exp2((logColor - exposureGrey / 1023.0) * linearRange) * linearGrey;
}

float3 LinearToLog(float3 linearColor)
{
	const float linearRange = 14.0f;
	const float linearGrey = 0.18f;
	const float exposureGrey = 444.0f;
	return saturate(log2(linearColor) / linearRange - log2(linearGrey) / linearRange + exposureGrey / 1023.0f);
}

float3 ApplyLUT(float3 color)
{
	color = LinearToLog(color + LogToLinear(0));
	color = color * ((LUT_SIZE - 1.0) / LUT_SIZE) + (0.5 / LUT_SIZE);
	color = TexLUT.SampleLevel(GradingSampler, color, 0).xyz;
	return color;
}

float3 ApplyGameCinematic(float3 linearColor)
{
	// Game cinematic
	float3 color = pow(abs(linearColor), inOutGamma.z) * cinematic.y;
	color = Saturation(color, cinematic.x);
	color = LinearContrast(color, cinematic.z, 0.18);
	return color;
}

float3 ApplyGameTintFade(float3 color)
{
	// Game tint/fade colors are Rec.709, while HDR grading output is Rec.2020.
	float luma = dot(color, enableHDR ? Rec2020_2_XYZ_MAT[1] : sRGB_2_XYZ_MAT[1]);
	float3 outputTint = enableHDR ? Color::BT709ToBT2020(tint.xyz) : tint.xyz;
	color = lerp(color, luma * outputTint, tint.w);

	// Game fade
	float3 outputFade = enableHDR ? Color::BT709ToBT2020(fade.xyz) : fade.xyz;
	return lerp(color, outputFade, fade.w);
}

float4 ApplyGradeOutput(float3 color)
{
	return float4(ApplyGameTintFade(pow(abs(color), inOutGamma.w)), 1);
}

#endif
