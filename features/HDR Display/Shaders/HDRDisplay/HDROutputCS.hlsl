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
#include "Common/DisplayMapping.hlsli"

/// Scene texture - reads from kFRAMEBUFFER (ISHDR's output)
Texture2D<float4> SceneTex : register(t0);
/// UI texture - gamma-encoded with straight alpha
Texture2D<float4> UITex : register(t1);
/// Output UAV - writes to swap chain
RWTexture2D<float4> HDROutput : register(u0);

cbuffer PerFrame : register(b0)
{
	float4 parameters0 : packoffset(c0);  ///< .x=enableHDR, .y=paperWhite nits, .z=peakNits, .w=skipUIComposite
	float4 parameters1 : packoffset(c1);  ///< .x=uiBrightness multiplier, .y=isSceneLinear
}

// UI reference brightness in nits
static const float UI_REFERENCE_NITS = 100.0;

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
		// ISHDR HDR path now runs the same tonemapping as SDR via HDR variant functions.
		// Output is tonemapped 0-1 gamma-encoded (HejlBurgessDawson bakes in gamma).
		// With Linear Lighting: output is tonemapped linear 0-1.
		// We linearize, scale to paper white nits, convert to BT.2020, and PQ encode.
		float3 sceneLinear = max(0, scene.rgb);
		if (!isSceneLinear)
			sceneLinear = Color::GammaToTrueLinear(sceneLinear);

		// Map SDR 1.0 to paper white nits, convert to BT.2020, PQ encode
		float3 sceneBT2020 = Color::BT709ToBT2020(sceneLinear);
		float3 sceneNits = sceneBT2020 * paperWhite;
		float3 scenePQ = Color::pq::Encode(sceneNits / 10000.0, 10000.0);

		if (skipUIComposite) {
			finalColor = scenePQ;
		} else {
			// UI is 8-bit gamma UNORM captured on black
			float3 uiPremulPQ = float3(0, 0, 0);
			float uiAlpha = ui.a;

			if (uiAlpha > 1.0 / 255.0) {
				float3 uiGamma = ui.rgb / uiAlpha;
				float3 uiLinear = Color::GammaToTrueLinear(uiGamma);
				float3 uiBT2020 = Color::BT709ToBT2020(uiLinear);
				float3 uiNits = uiBT2020 * UI_REFERENCE_NITS * uiBrightness;
				uiPremulPQ = Color::pq::Encode(uiNits / 10000.0, 10000.0) * uiAlpha;
			}

			finalColor = uiPremulPQ + scenePQ * (1.0 - uiAlpha);
		}
	} else {
		// SDR path: skipUIComposite is always true (vanilla composites to kFRAMEBUFFER)
		finalColor = saturate(scene.rgb);
	}

	HDROutput[dispatchID.xy] = float4(finalColor, 1.0);
}