/**
 * @file HDROutputCS.hlsl
 * @brief Final output compute shader - colorspace conversion and UI compositing.
 *
 * @details ISHDR outputs gamma-encoded BT.709 values after DICE tonemapping.
 *   Post-processing (TAA, ISDownsample, DOF) runs in that standard color space.
 *   This shader performs the final HDR colorspace conversion and UI compositing.
 *
 * Pipeline:
 *   - SDR: Passthrough + UI composite
 *   - HDR: Gamma decode -> BT.709->BT.2020 -> PQ encode + UI composite
 *
 * @see ISHDR.hlsl for bloom, DICE tonemapping, and color grading
 * @see HDR.cpp ApplyHDR() for the dispatch logic
 */

#include "Common/Color.hlsli"
#include "Common/SharedData.hlsli"

Texture2D<float4> SceneTex : register(t0);
Texture2D<float4> UITex : register(t1);
RWTexture2D<float4> HDROutput : register(u0);

cbuffer PerFrame : register(b0)
{
	float enableHDR : packoffset(c0.x);
	float paperWhite : packoffset(c0.y);
	float peakNits : packoffset(c0.z);
	float skipUIComposite : packoffset(c0.w);
	float uiBrightness : packoffset(c1.x);
	float isSceneLinear : packoffset(c1.y);
}

static const float UI_REFERENCE_NITS = 80.0;

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID)
{
	uint width, height;
	HDROutput.GetDimensions(width, height);
	if (dispatchID.x >= width || dispatchID.y >= height)
		return;

	float4 scene = SceneTex[dispatchID.xy];
	float4 ui = UITex[dispatchID.xy];

	bool hdrEnabled = enableHDR > 0.5;
	bool skipUI = skipUIComposite > 0.5;

	float3 finalColor;

	if (hdrEnabled) {
		// Scene arrives gamma-encoded BT.709 from ISHDR (post-DICE tonemapping).
		// Convert to linear, then BT.2020, then PQ for HDR10 output.
		float3 sceneLinear = Color::GammaToLinear(max(0.0, scene.rgb));

		float3 sceneBT2020 = Color::BT709ToBT2020(sceneLinear);
		sceneBT2020 = max(sceneBT2020, 0.0);

		if (skipUI) {
			finalColor = Color::pq::Encode(sceneBT2020, sRGB_WhiteLevelNits);
		} else {
			float uiNits = UI_REFERENCE_NITS * uiBrightness;

			float3 uiStraight = (ui.a > 0.001) ? ui.rgb / ui.a : float3(0, 0, 0);
			float3 uiLinear = Color::GammaToTrueLinear(max(0, uiStraight));
			float3 uiBT2020 = Color::BT709ToBT2020(uiLinear);
			float3 uiScaled = uiBT2020 * (uiNits / 10000.0);

			// Scene is already in linear BT.2020, normalize to 10000 nit scale for PQ
			float3 sceneNormalized = sceneBT2020 / (10000.0 / sRGB_WhiteLevelNits);

			float3 blendedLinear = uiScaled * ui.a + sceneNormalized * (1.0 - ui.a);

			finalColor = Color::pq::Encode(blendedLinear);
		}
	} else {
		float3 sceneGamma = scene.rgb;

		if (skipUI) {
			finalColor = sceneGamma;
		} else {
			finalColor = ui.rgb + sceneGamma * (1.0 - ui.a);
		}

		finalColor = saturate(finalColor);
	}

	HDROutput[dispatchID.xy] = float4(finalColor, 1.0);
}