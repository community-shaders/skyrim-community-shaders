// Preprocess vanilla UI for Frame Gen compositing.
// HDR path converts UI to PQ/BT.2020 using configured paper white.
// SDR path keeps gamma UI and only clamps negatives.

#include "Common/Color.hlsli"

RWTexture2D<float4> UITex : register(u0);

cbuffer PerFrame : register(b0)
{
	float enableHDR : packoffset(c0.x);                 ///< 1.0 = HDR output with PQ, 0.0 = SDR output with gamma
	float paperWhite : packoffset(c0.y);                ///< Reference white in nits (used by HDR UI conversion)
	float peakNits : packoffset(c0.z);                  ///< Maximum display brightness in nits for HDR (unused here)
	float skipUIComposite : packoffset(c0.w);           ///< Unused in this shader
	float uiBrightness : packoffset(c1.x);              ///< UI brightness multiplier
	float isSceneLinear : packoffset(c1.y);             ///< Unused in this shader
	float isMainOrLoadingMenu : packoffset(c1.z);       ///< Unused; layout matches HDRDataCB
	float fgTweenMenuMidAlphaBoost : packoffset(c1.w);  ///< 1 = TweenMenu open: apply mid-alpha AA boost only for pause UI
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	// Bounds check to prevent UAV out-of-bounds reads/writes
	uint width, height;
	UITex.GetDimensions(width, height);
	if (dispatchID.x >= width || dispatchID.y >= height)
		return;

	float4 ui = UITex[dispatchID.xy];

	bool hdrEnabled = enableHDR > 0.5;

	if (hdrEnabled) {
		// FidelityFX FG blends in PQ space, so UI must be PQ/BT.2020.
		const float uiReferenceNits = max(paperWhite, 1.0);

		if (ui.a > 0.001) {
			// Pause menu only: raise coverage in the soft AA band to avoid washout.
			float aIn = ui.a;
			float aOut = aIn;
			if (fgTweenMenuMidAlphaBoost > 0.5) {
				float midBand = smoothstep(0.3, 0.35, aIn) * (1.0 - smoothstep(0.55, 0.6, aIn));
				const float fgMidAlphaBoost = 0.12;
				aOut = saturate(aIn + midBand * fgMidAlphaBoost);
			}

			float3 uiStraight = ui.rgb / aIn;
			float3 uiLinear = Color::SrgbToLinear(max(0, uiStraight));
			float3 uiBT2020 = Color::BT709ToBT2020(uiLinear);
			float3 uiNits = uiBT2020 * uiReferenceNits * uiBrightness;
			ui.rgb = Color::pq::Encode(uiNits / 10000.0, 10000.0) * aOut;
			ui.a = aOut;
		} else {
			// Broken-alpha path: transform premultiplied color and keep alpha at 0.
			float3 uiLinear = Color::SrgbToLinear(max(0, ui.rgb));
			float3 uiBT2020 = Color::BT709ToBT2020(uiLinear);
			float3 uiNits = uiBT2020 * uiReferenceNits * uiBrightness;
			ui.rgb = Color::pq::Encode(uiNits / 10000.0, 10000.0);
		}
	} else {
		// SDR path: keep premultiplied gamma UI, clamp negatives only.
		ui.rgb = max(0, ui.rgb);
		UITex[dispatchID.xy] = ui;
		return;
	}

	UITex[dispatchID.xy] = ui;
}