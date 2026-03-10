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

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
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
		// ISHDR already scales the scene into HDR paper-white space using 80-nit-relative units.
		// Convert to linear, then BT.2020, then PQ for HDR10 output.
		float3 sceneLinear = Color::GammaToLinear(max(0.0, scene.rgb));

		float3 sceneBT2020 = Color::BT709ToBT2020(sceneLinear);
		sceneBT2020 = max(sceneBT2020, 0.0);

		if (skipUI) {
			// FG handles UI compositing separately. Scene is already scaled by ISHDR.
			finalColor = Color::pq::Encode(sceneBT2020, sRGB_WhiteLevelNits);
		} else {
			// Composite in gamma space (matching SDR behavior), then convert to HDR.
			// The vanilla UI was designed for gamma-space blending; compositing in PQ
			// over-darkens and compositing in linear over-brightens behind UI overlays.
			float3 composited = ui.rgb * uiBrightness + scene.rgb * (1.0 - ui.a);

			float3 compositedLinear = Color::GammaToLinear(max(0.0, composited));
			float3 compositedBT2020 = Color::BT709ToBT2020(compositedLinear);
			finalColor = Color::pq::Encode(max(0.0, compositedBT2020), sRGB_WhiteLevelNits);
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