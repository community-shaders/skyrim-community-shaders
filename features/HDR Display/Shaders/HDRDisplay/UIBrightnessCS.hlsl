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
	float enableHDR : packoffset(c0.x);         ///< 1.0 = HDR output with PQ, 0.0 = SDR output with gamma
	float paperWhite : packoffset(c0.y);        ///< Reference white brightness in nits for HDR (unused here)
	float peakNits : packoffset(c0.z);          ///< Maximum display brightness in nits for HDR (unused here)
	float skipUIComposite : packoffset(c0.w);   ///< Unused in this shader
	float uiBrightness : packoffset(c1.x);      ///< UI brightness multiplier
	float isSceneLinear : packoffset(c1.y);     ///< Unused in this shader
}

// UI reference brightness in nits — matches typical SDR monitor brightness.
// Ensures UI appears consistently bright in both SDR and HDR without being washed out or blown out.
static const float UI_REFERENCE_NITS = 80.0;

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID)
{
	// Bounds check to prevent UAV out-of-bounds reads/writes
	uint width, height;
	UITex.GetDimensions(width, height);
	if (dispatchID.x >= width || dispatchID.y >= height)
		return;

	float4 ui = UITex[dispatchID.xy];

	bool hdrEnabled = enableHDR > 0.5;

	if (hdrEnabled) {
		// === HDR Pipeline ===
		// Input: Vanilla gamma UI (sRGB, BT.709) with premultiplied alpha
		// Output: PQ-encoded UI in BT.2020 colorspace, premultiplied alpha
		//
		// FidelityFX FrameGeneration blends in PQ space, so UI must be PQ-encoded.
		// Un-premultiply before nonlinear conversion to preserve antialiased edges.

		// Recover straight (non-premultiplied) color
		float3 uiStraight = (ui.a > 0.001) ? ui.rgb / ui.a : float3(0, 0, 0);

		// Convert from gamma to linear
		float3 uiLinear = Color::GammaToTrueLinear(max(0, uiStraight));

		// Expand to wider BT.2020 colorspace for HDR (reduces banding on saturated colors)
		float3 uiBT2020 = Color::BT709ToBT2020(uiLinear);

		// Scale to reference nit level and apply user brightness adjustment
		float3 uiNits = uiBT2020 * UI_REFERENCE_NITS * uiBrightness;

		// Encode to PQ (assumes 10000 nit max for PQ range)
		ui.rgb = Color::pq::Encode(uiNits / 10000.0, 10000.0);
	} else {
		// === SDR Pipeline ===
		// Input: Vanilla gamma UI (sRGB, BT.709) with premultiplied alpha
		// Output: Adjusted gamma UI suitable for SDR displays
		//
		// Already premultiplied from render blend state — just clamp.
		// No alpha multiply needed; skip to write.
		ui.rgb = max(0, ui.rgb);
		UITex[dispatchID.xy] = ui;
		return;
	}

	// HDR path: output is straight PQ color from the conversion above.
	// Premultiply for FidelityFX compositing: result = ui.rgb + scene * (1 - ui.a)
	ui.rgb *= ui.a;

	UITex[dispatchID.xy] = ui;
}