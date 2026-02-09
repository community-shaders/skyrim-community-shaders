// Scales UI brightness for FG compositing.
// UI is 8-bit UNORM (gamma SDR). Convert to PQ for HDR, or apply brightness for SDR.
// Output is premultiplied alpha: result = ui.rgb + scene * (1 - ui.a)

#include "Common/Color.hlsli"

RWTexture2D<float4> UITex : register(u0);

cbuffer PerFrame : register(b0)
{
	float4 parameters0 : packoffset(c0);  // .x = enableHDR, .y = paperWhite, .z = peakNits, .w = unused
	float4 parameters1 : packoffset(c1);  // .x = uiBrightness, .y = unused
}

// UI reference brightness in nits - matches typical SDR monitor brightness
// This ensures UI has consistent perceived brightness in both SDR and HDR modes
static const float UI_REFERENCE_NITS = 100.0;

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID)
{
	float4 ui = UITex[dispatchID.xy];

	// Use small threshold instead of 0 to handle near-invisible UI with potential garbage RGB. (Invisible UI bars like Magika/Stamina)
	if (ui.a > 1.0 / 255.0) {
		bool enableHDR = parameters0.x > 0.5;
		float paperWhite = parameters0.y;
		float peakNits = parameters0.z;
		float uiBrightness = parameters1.x;

		if (enableHDR) {
			// HDR: encode UI to PQ so FidelityFX can blend PQ over PQ
			// UI renders at UI_REFERENCE_NITS for consistent brightness with SDR
			float3 uiLinear = Color::GammaToTrueLinear(max(0, ui.rgb));
			float3 uiBT2020 = Color::BT709ToBT2020(uiLinear);
			float3 uiNits = uiBT2020 * UI_REFERENCE_NITS * uiBrightness;
			ui.rgb = Color::pq::Encode(uiNits / 10000.0, 10000.0);
		} else {
			// SDR: simple brightness multiplier in gamma space
			ui.rgb = max(0, ui.rgb * uiBrightness);
		}

		// Output premultiplied alpha: RGB * alpha
		// FidelityFX expects: result = ui.rgb + scene * (1 - ui.a)
		ui.rgb *= ui.a;
	} else {
		// Zero out pixel when alpha is effectively 0 to prevent FG artifacts
		ui = 0;
	}

	UITex[dispatchID.xy] = ui;
}
