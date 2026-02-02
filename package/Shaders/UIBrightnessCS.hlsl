// Scales UI brightness and encodes for FidelityFX Frame Gen compositing
// For HDR: converts gamma UI to PQ so FidelityFX can blend PQ UI over PQ scene
// For SDR: applies brightness multiplier

#include "Common/Color.hlsli"

RWTexture2D<float4> UITex : register(u0);

cbuffer PerFrame : register(b0)
{
	float4 parameters0 : packoffset(c0);  // .x = enableHDR, .y = paperWhite, .z = peakNits, .w = unused
	float4 parameters1 : packoffset(c1);  // .x = uiBrightness, .y = unused
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID)
{
	float4 ui = UITex[dispatchID.xy];

	// Use small threshold instead of 0 to handle near-invisible UI with potential garbage RGB. (Invisible UI bars like Magika/Stamina)
	if (ui.a > 1.0 / 255.0) {
		bool enableHDR = parameters0.x > 0.5;
		float paperWhite = parameters0.y;
		float peakNits = parameters0.z;
		float uiBrightness = parameters1.x;

		// Apply brightness scaling and clamp to valid range
		ui.rgb = max(0, ui.rgb * uiBrightness);

		if (enableHDR) {
			// For HDR: encode UI to PQ so FidelityFX can blend PQ over PQ
			// gamma -> linear -> BT.2020 -> nits -> PQ
			float3 uiLinear = Color::GammaToTrueLinear(ui.rgb);
			float3 uiBT2020 = Color::BT709ToBT2020(uiLinear);
			float3 uiNits = uiBT2020 * paperWhite;
			ui.rgb = Color::pq::Encode(uiNits / 10000.0, 10000.0);
		}

		// FidelityFX configured WITHOUT USE_PREMUL_ALPHA flag
		// Standard alpha blend: Final = UI.RGB * UI.Alpha + Scene * (1 - UI.Alpha)
		// No premultiply needed - pass through as-is
	} else {
		// Zero out entire pixel when alpha is effectively 0 to prevent FG artifacts
		ui = 0;
	}

	UITex[dispatchID.xy] = ui;
}
