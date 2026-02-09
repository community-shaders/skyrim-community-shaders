/**
 * @file HDROutputCS.hlsl
 * @brief Final output compute shader - HDR encoding and UI compositing.
 *
 * @details ISHDR HDR path bypasses vanilla tonemapping, outputting raw linear
 *   scene values (post auto-exposure). This shader converts to BT.2020 and
 *   PQ-encodes for HDR10 output.
 *
 * The key math (matching the original HDR PR and Luma Framework pattern):
 *   Color::pq::Encode(bt2020Color, paperWhite)
 * Inside Encode, color is scaled by (paperWhite / 10000), so:
 *   - Scene 1.0 encodes to paperWhite nits (e.g. 203 nits)
 *   - Scene > 1.0 naturally extends above paper white toward 10000 nits
 *   - The display clips at its own peak brightness
 *
 * Pipeline:
 *   - SDR: ISHDR tonemaps to 0-1 → passthrough + UI composite
 *   - HDR: ISHDR passes raw linear → BT.2020 → PQ encode (paperWhite scaling) + UI composite
 *
 * @see ISHDR.hlsl for the HDR bypass path
 * @see HDR.cpp ApplyHDR() for the dispatch logic
 */

#include "Common/Color.hlsli"
#include "Common/SharedData.hlsli"

Texture2D<float4> SceneTex : register(t0);
Texture2D<float4> UITex : register(t1);
RWTexture2D<float4> HDROutput : register(u0);

cbuffer PerFrame : register(b0)
{
	float4 parameters0 : packoffset(c0);  ///< .x=enableHDR, .y=paperWhite nits, .z=peakNits, .w=skipUIComposite
	float4 parameters1 : packoffset(c1);  ///< .x=uiBrightness multiplier, .y=isSceneLinear
}

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
	bool isSceneLinear = parameters1.y > 0.5;

	float3 finalColor;

	if (enableHDR) {
		float3 sceneLinear = max(0, scene.rgb);
		if (!isSceneLinear)
			sceneLinear = Color::GammaToTrueLinear(sceneLinear);

		float3 bt2020 = Color::BT709ToBT2020(sceneLinear);

		// PQ encode: scene 1.0 maps to paperWhite nits on display.
		// Values >1.0 extend above paper white toward 10000 nits (PQ max).
		// The display naturally clips at its peak brightness.
		float3 scenePQ = Color::pq::Encode(bt2020, paperWhite);

		if (skipUIComposite) {
			finalColor = scenePQ;
		} else {
			float3 uiPremulPQ = float3(0, 0, 0);
			float uiAlpha = ui.a;

			if (uiAlpha > 1.0 / 255.0) {
				float3 uiGamma = ui.rgb / uiAlpha;
				float3 uiLinear = Color::GammaToTrueLinear(uiGamma);
				float3 uiBT2020 = Color::BT709ToBT2020(uiLinear);
				float uiNits = UI_REFERENCE_NITS * uiBrightness;
				uiPremulPQ = Color::pq::Encode(uiBT2020, uiNits) * uiAlpha;
			}

			finalColor = uiPremulPQ + scenePQ * (1.0 - uiAlpha);
		}
	} else {
		finalColor = saturate(scene.rgb);
	}

	HDROutput[dispatchID.xy] = float4(finalColor, 1.0);
}