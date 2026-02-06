/**
 * @file HDROutputCS.hlsl
 * @brief Final output compute shader - HDR encoding and UI compositing.
 *
 * @details For HDR, kFRAMEBUFFER is redirected to a float16 texture so ISHDR
 *   can write linear values >1.0. This shader reads those values directly,
 *   applies DICE highlight compression, and PQ-encodes for the HDR10 swap chain.
 *
 * Pipeline:
 *   - SDR: ISHDR tonemaps to 0-1 → passthrough + UI composite
 *   - HDR: ISHDR skips tonemapping → linear HDR → BT.2020 → DICE → PQ + UI composite
 *
 * @see ISHDR.hlsl for tonemapping, bloom, and color grading
 * @see HDR.cpp ApplyHDR() for the dispatch logic
 */

#include "Common/Color.hlsli"
#include "Common/SharedData.hlsli"

/// Scene texture - reads from kFRAMEBUFFER (ISHDR's output)
Texture2D<float4> SceneTex : register(t0);
/// UI texture - gamma-encoded with straight alpha
Texture2D<float4> UITex : register(t1);
/// Output UAV - writes to swap chain
RWTexture2D<float4> HDROutput : register(u0);

cbuffer PerFrame : register(b0)
{
	float4 parameters0 : packoffset(c0);  ///< .x=enableHDR, .y=paperWhite nits, .z=peakNits, .w=skipUIComposite
	float4 parameters1 : packoffset(c1);  ///< .x=uiBrightness multiplier
}

// sRGB reference white level per IEC 61966-2-1
static const float SRGB_WHITE_LEVEL_NITS = 80.0;

// UI reference brightness in nits
static const float UI_REFERENCE_NITS = 100.0;

// Exponential range compression: maps [0, inf) -> [0, 1) asymptotically
float DICERangeCompress(float x)
{
	return 1.0 - exp(-x);
}

// DICE tonemapper operating in PQ space by luminance.
// Input color is in normalized units where 1.0 = sRGB white (80 nits).
// Paper white and peak white are also in these normalized units.
float3 DICETonemap(float3 color, float peakWhite)
{
	float sourceLuminance = Color::RGBToLuminance(color);

	// Shoulder starts at a fraction of peak to leave SDR range largely untouched
	float shoulderStart = peakWhite * 0.5;
	if (sourceLuminance <= shoulderStart)
		return color;

	// Normalize to PQ space for perceptually uniform compression
	float HDR10_MaxWhite = 10000.0 / SRGB_WHITE_LEVEL_NITS;
	float sourceLumPQ = Color::pq::Encode(sourceLuminance / HDR10_MaxWhite, 10000.0).x;
	float shoulderPQ = Color::pq::Encode(shoulderStart / HDR10_MaxWhite, 10000.0).x;
	float peakPQ = Color::pq::Encode(peakWhite / HDR10_MaxWhite, 10000.0).x;

	float compressibleRange = sourceLumPQ - shoulderPQ;
	float compressedRange = peakPQ - shoulderPQ;
	float compressed = DICERangeCompress(compressibleRange / compressedRange);
	float compressedLumPQ = shoulderPQ + compressedRange * compressed;

	// Convert back to linear
	float compressedLum = Color::pq::Decode(compressedLumPQ, 10000.0).x * HDR10_MaxWhite;

	return color * (compressedLum / sourceLuminance);
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID)
{
	float4 scene = SceneTex[dispatchID.xy];
	float4 ui = UITex[dispatchID.xy];

	bool enableHDR = parameters0.x > 0.5;
	float paperWhite = parameters0.y;
	float peakNits = parameters0.z;
	bool skipUIComposite = parameters0.w > 0.5;
	float uiBrightness = parameters1.x;

	float3 finalColor;

	bool isSceneLinear = parameters1.y > 0.5;

	if (enableHDR) {
		// ISHDR HDR path preserves values >1.0 in a float16 texture.
		// With Linear Lighting: output is already linear.
		// Without Linear Lighting: output is gamma-encoded, needs linearization.
		float3 sceneLinear = max(0, scene.rgb);
		if (!isSceneLinear)
			sceneLinear = Color::GammaToTrueLinear(sceneLinear);

		float3 sceneBT2020 = Color::BT709ToBT2020(sceneLinear);

		// Normalize paper white and peak white relative to sRGB reference (80 nits).
		float pw = paperWhite / SRGB_WHITE_LEVEL_NITS;
		float peak = peakNits / SRGB_WHITE_LEVEL_NITS;

		// DICE compress highlights toward peak brightness.
		float3 tonemapped = DICETonemap(sceneBT2020 * pw, peak) / pw;

		// PQ encode: paper white maps scene value of 1.0 to paperWhite nits on display
		float3 scenePQ = Color::pq::Encode(tonemapped * paperWhite / 10000.0, 10000.0);

		if (skipUIComposite) {
			finalColor = scenePQ;
		} else {
			float3 uiLinear = Color::GammaToTrueLinear(max(0, ui.rgb));
			float3 uiBT2020 = Color::BT709ToBT2020(uiLinear);
			float3 uiNits = uiBT2020 * UI_REFERENCE_NITS * uiBrightness;
			float3 uiPQ = Color::pq::Encode(uiNits / 10000.0, 10000.0);

			float3 uiPremul = uiPQ * ui.a;
			finalColor = uiPremul + scenePQ * (1.0 - ui.a);
		}
	} else {
		// SDR path: ISHDR outputs tonemapped, gamma-encoded values
		// Just composite UI and pass through (premultiplied alpha)
		float3 sceneGamma = scene.rgb;

		float3 composited;
		if (skipUIComposite) {
			composited = sceneGamma;
		} else {
			// Premultiplied blend: result = ui_premul + scene * (1-alpha)
			float3 uiPremul = ui.rgb * uiBrightness * ui.a;
			composited = uiPremul + sceneGamma * (1.0 - ui.a);
		}
		finalColor = saturate(composited);
	}

	HDROutput[dispatchID.xy] = float4(finalColor, 1.0);
}
