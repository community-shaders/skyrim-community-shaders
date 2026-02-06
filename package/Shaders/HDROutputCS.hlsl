/**
 * @file HDROutputCS.hlsl
 * @brief Final output compute shader - format conversion and UI compositing.
 *
 * @details This shader is the final stage that copies ISHDR's output to the swap chain.
 *   ISHDR does all the heavy lifting (tonemapping, bloom, color grading).
 *   This shader just handles format conversion and UI compositing.
 *
 * Pipeline Overview:
 *   1. Scene renders to kMAIN with linear HDR values
 *   2. ISHDR reads from BlendTex, applies processing, writes to kFRAMEBUFFER:
 *      - SDR: Tonemapped, gamma-encoded (0-1 range)
 *      - HDR: Linear with preserved highlights (can exceed 1.0)
 *   3. This shader reads kFRAMEBUFFER (ISHDR's output):
 *      - SDR: Passthrough + UI composite
 *      - HDR: BT.2020 conversion, nit scaling, PQ encoding + UI composite
 *
 * Data Flow:
 *   - SceneTex (t0): kFRAMEBUFFER - ISHDR's output
 *     * SDR: Tonemapped, gamma-encoded values (0-1)
 *     * HDR: Linear values from ISHDR (can exceed 1.0 for highlights)
 *   - UITex (t1): UI layer with straight alpha
 *   - HDROutput (u0): Final swap chain buffer (PQ encoded for HDR, sRGB for SDR)
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

// DICE-style exponential range compression
float3 DICERangeCompress(float3 x, float maxRange)
{
	float3 compression = 1.0 - exp(-x);
	float maxCompression = 1.0 - exp(-maxRange);
	return compression / maxCompression;
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
	float3 compressed = DICERangeCompress(float3(compressibleRange / compressedRange, 0, 0), 1.0);
	float compressedLumPQ = shoulderPQ + compressedRange * compressed.x;

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

	// Check if Linear Lighting is enabled - determines if ISHDR outputs linear or gamma
	bool linearLighting = SharedData::linearLightingSettings.enableLinearLighting > 0;

	if (enableHDR) {
		// HDR path: ISHDR outputs linear (if LL enabled) or gamma-encoded values
		// Convert to linear, apply DICE in normalized space, then PQ encode
		float3 sceneLinear = max(0, scene.rgb);

		if (!linearLighting) {
			sceneLinear = pow(abs(sceneLinear), 2.2);
		}

		float3 sceneBT2020 = Color::BT709ToBT2020(sceneLinear);

		// Normalize paper white and peak white relative to sRGB reference (80 nits).
		// In this space, 1.0 = sRGB white = 80 nits.
		// Paper white (e.g. 203 nits) maps SDR white to 203/80 = 2.54x.
		float pw = paperWhite / SRGB_WHITE_LEVEL_NITS;
		float peak = peakNits / SRGB_WHITE_LEVEL_NITS;

		// DICE tonemap in normalized space — SDR content (0-1) passes through
		// largely untouched, only highlights above shoulder get compressed toward peak.
		// Multiply by pw before, divide after: preserves SDR range.
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
