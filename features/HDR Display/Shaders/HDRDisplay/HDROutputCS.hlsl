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
			// Composite UI on top of scene in linear space.
			// Blending in PQ space darkens alpha regions (~20%) due to PQ's steep nonlinearity.
			// Decode scene to linear, blend there (matching SDR's luminance-proportional blend),
			// then re-encode to PQ.

			float uiNits = UI_REFERENCE_NITS * uiBrightness;

			// Recover straight (non-premultiplied) UI color
			float3 uiStraight = (ui.a > 0.001) ? ui.rgb / ui.a : float3(0, 0, 0);

			// Convert UI from sRGB gamma BT.709 to linear BT.2020, scaled to nit level
			float3 uiLinear = Color::GammaToTrueLinear(max(0, uiStraight));
			float3 uiBT2020 = Color::BT709ToBT2020(uiLinear);
			float3 uiScaled = uiBT2020 * (uiNits / 10000.0);

			// Decode scene from PQ to linear (normalized to 10000 nits)
			float3 sceneLinear = Color::pq::Decode(scenePQ);

			// Blend in linear space for correct luminance
			float3 blendedLinear = uiScaled * ui.a + sceneLinear * (1.0 - ui.a);

			// Re-encode to PQ
			finalColor = Color::pq::Encode(blendedLinear);
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
			// UI texture already has premultiplied RGB from the render blend state.
			finalColor = ui.rgb + sceneGamma * (1.0 - ui.a);
		}

		// Clamp to [0,1] for SDR output
		finalColor = saturate(finalColor);
	}

	HDROutput[dispatchID.xy] = float4(finalColor, 1.0);
}