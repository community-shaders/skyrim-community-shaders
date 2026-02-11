/**
 * @file UIBrightnessCS.hlsl
 * @brief Pre-processing compute shader for UI brightness scaling.
 *
 * @details Prepares vanilla UI for different display modes:
 *   - SDR: Apply brightness multiplier in gamma space
 *   - HDR: Convert gamma UI to PQ-encoded BT.2020 for seamless FidelityFX compositing
 *
 * Input: Vanilla UI at 8-bit gamma with premultiplied alpha
 * Output: HDR-ready PQ or SDR-adjusted gamma, premultiplied alpha
 *
 * @see HDROutputCS.hlsl for final compositing onto the scene
 */

#include "Common/Color.hlsli"

RWTexture2D<float4> UITex : register(u0);

cbuffer PerFrame : register(b0)
{
	float4 parameters0 : packoffset(c0);  // .x = enableHDR, .y = paperWhite nits, .z = peakNits, .w = unused
	float4 parameters1 : packoffset(c1);  // .x = uiBrightness multiplier, .y = unused
}

// UI reference brightness in nits — matches typical SDR monitor brightness.
// Ensures UI appears consistently bright in both SDR and HDR without being washed out or blown out.
static const float UI_REFERENCE_NITS = 100.0;

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID)
{
	float4 ui = UITex[dispatchID.xy];

	// Skip fully transparent pixels to prevent FidelityFX artifacts from processing garbage RGB.
	// Use 1/255 threshold to handle near-invisible UI (e.g., empty resource bars).
	if (ui.a > 1.0 / 255.0) {
		bool enableHDR = parameters0.x > 0.5;
		float paperWhite = parameters0.y;
		float peakNits = parameters0.z;
		float uiBrightness = parameters1.x;

		if (enableHDR) {
			// === HDR Pipeline ===
			// Input: Vanilla gamma UI (sRGB, BT.709)
			// Output: PQ-encoded UI in BT.2020 colorspace
			// 
			// FidelityFX FrameGeneration blends in PQ space, so UI must be PQ-encoded.
			// Scale to UI_REFERENCE_NITS for consistent perceived brightness with SDR.
			
			// Convert from gamma to linear
			float3 uiLinear = Color::GammaToTrueLinear(max(0, ui.rgb));
			
			// Expand to wider BT.2020 colorspace for HDR (reduces banding on saturated colors)
			float3 uiBT2020 = Color::BT709ToBT2020(uiLinear);
			
			// Scale to reference nit level and apply user brightness adjustment
			float3 uiNits = uiBT2020 * UI_REFERENCE_NITS * uiBrightness;
			
			// Encode to PQ (assumes 10000 nit max for PQ range)
			ui.rgb = Color::pq::Encode(uiNits / 10000.0, 10000.0);
		} else {
			// === SDR Pipeline ===
			// Input: Vanilla gamma UI (sRGB, BT.709)
			// Output: Adjusted gamma UI suitable for SDR displays
			// 
			// Simple brightness scaling in gamma space without color space conversion.
			ui.rgb = max(0, ui.rgb * uiBrightness);
		}

		// Convert to premultiplied alpha for proper compositing.
		// FidelityFX will blend as: result = ui.rgb * ui.a + scene * (1 - ui.a)
		ui.rgb *= ui.a;
	} else {
		// Fully transparent pixel — zero out to prevent artifacts
		ui = 0;
	}

	UITex[dispatchID.xy] = ui;
}