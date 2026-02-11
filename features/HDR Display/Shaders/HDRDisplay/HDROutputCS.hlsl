/**
 * @file HDROutputCS.hlsl
 * @brief Final output compute shader - UI compositing onto PQ scene.
 *
 * @details ISHDR HDR path handles bloom, cinematic grading, DICE tonemapping,
 *   BT.2020 conversion, and PQ encoding. Scene arrives here already PQ-encoded.
 *   This shader composites UI on top and passes through to the swap chain.
 *
 * Pipeline:
 *   - SDR: ISHDR tonemaps to 0-1 → passthrough + UI composite
 *   - HDR: ISHDR outputs PQ-encoded BT.2020 → passthrough + UI composite (PQ-encoded UI)
 *
 * @see ISHDR.hlsl for bloom, DICE tonemapping, and PQ encoding
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

static const float UI_REFERENCE_NITS = 80.0;

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID)
{
	// Bounds check to prevent UAV out-of-bounds writes
	uint width, height;
	HDROutput.GetDimensions(width, height);
	if (dispatchID.x >= width || dispatchID.y >= height)
		return;

	float4 scene = SceneTex[dispatchID.xy];
	float4 ui = UITex[dispatchID.xy];

	bool enableHDR = parameters0.x > 0.5;
	bool skipUIComposite = parameters0.w > 0.5;
	float uiBrightness = parameters1.x;

	float3 finalColor;

	if (enableHDR) {
		// === HDR Pipeline ===
		// Input: Scene is PQ-encoded in BT.2020 from ISHDR (bloom, tone, grading applied)
		// Output: PQ-encoded BT.2020 ready for display or swap chain
		
		float3 scenePQ = scene.rgb;

		if (skipUIComposite) {
			// Direct passthrough when UI compositing is disabled
			finalColor = scenePQ;
		} else {
			// Composite UI on top of PQ scene (UI was pre-processed to PQ by UIBrightnessCS)
			// Both UI and scene are in PQ space, so we can blend directly.
			
			// Scale UI to nit reference level
			float uiNits = UI_REFERENCE_NITS * uiBrightness;
			
			// UI is in BT.709, convert to BT.2020 for HDR
			float3 uiLinear = Color::GammaToTrueLinear(max(0, ui.rgb));
			float3 uiBT2020 = Color::BT709ToBT2020(uiLinear);
			
			// Encode UI to PQ space matching the scene
			float3 uiPQ = Color::pq::Encode(uiBT2020, uiNits);
			
			// Apply UI alpha: blend = ui + (1-alpha)*scene
			float3 uiPremulPQ = uiPQ * ui.a;
			finalColor = uiPremulPQ + scenePQ * (1.0 - ui.a);
		}
	} else {
		// === SDR Pipeline ===
		// Input: Scene is tonemapped to gamma LDR range [0,1] from ISHDR
		// Output: Gamma LDR ready for standard SDR display
		
		float3 sceneGamma = scene.rgb;

		if (skipUIComposite) {
			// Direct passthrough when UI compositing is disabled
			finalColor = sceneGamma;
		} else {
			// Composite UI on top of gamma scene using premultiplied alpha
			// Both UI and scene are in sRGB gamma space for SDR.
			float3 uiPremul = ui.rgb * ui.a;
			finalColor = uiPremul + sceneGamma * (1.0 - ui.a);
		}

		// Clamp to [0,1] for SDR output
		finalColor = saturate(finalColor);
	}

	HDROutput[dispatchID.xy] = float4(finalColor, 1.0);
}