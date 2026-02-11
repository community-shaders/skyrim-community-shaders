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

static const float UI_REFERENCE_NITS = 100.0;

// Interleaved gradient noise (Jimenez 2014) — smooth spatial dither pattern
float IGNoise(float2 pos)
{
	return frac(52.9829189 * frac(dot(pos, float2(0.06711056, 0.00583715))));
}

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
		// Scene is already PQ-encoded in BT.2020 from ISHDR
		float3 scenePQ = scene.rgb;

		if (skipUIComposite) {
			finalColor = scenePQ;
		} else {
			// UI is raw vanilla gamma - convert to PQ and composite
			float3 uiLinear = Color::GammaToTrueLinear(max(0, ui.rgb));
			float3 uiBT2020 = Color::BT709ToBT2020(uiLinear);
			float uiNits = UI_REFERENCE_NITS * uiBrightness;
			float3 uiPQ = Color::pq::Encode(uiBT2020, uiNits);
			float3 uiPremulPQ = uiPQ * ui.a;
			
			finalColor = uiPremulPQ + scenePQ * (1.0 - ui.a);
		}
	} else {
		// SDR mode  
		float3 sceneGamma = scene.rgb;

		if (skipUIComposite) {
			finalColor = sceneGamma;
		} else {
			// Premultiplied alpha composite
			float3 uiPremul = ui.rgb * ui.a * uiBrightness;
			finalColor = uiPremul + sceneGamma * (1.0 - ui.a);
		}

		finalColor = saturate(finalColor);
	}

	// Dither to break up 10-bit PQ banding in smooth gradients (sky, sun bloom)
	if (enableHDR) {
		float dither = (IGNoise(float2(dispatchID.xy)) - 0.5) / 1023.0;
		finalColor += dither;
	}

	HDROutput[dispatchID.xy] = float4(finalColor, 1.0);
}