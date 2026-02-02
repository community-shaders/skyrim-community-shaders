/**
 * @file HDROutputCS.hlsl
 * @brief Final output compute shader - format conversion and UI compositing.
 *
 * @details This shader is the final stage that copies ISHDR's output to the swap chain.
 *   ISHDR does all the heavy lifting (tonemapping, bloom, color grading).
 *   This shader just handles format conversion and UI compositing.
 *
 * Pipeline Overview:
 *   1. Scene renders to kMAIN with linear HDR values
 *   2. ISHDR reads from BlendTex, applies processing, writes to kFRAMEBUFFER:
 *      - SDR: Tonemapped, gamma-encoded (0-1 range)
 *      - HDR: Linear with preserved highlights (can exceed 1.0)
 *   3. This shader reads kFRAMEBUFFER (ISHDR's output):
 *      - SDR: Passthrough + UI composite
 *      - HDR: BT.2020 conversion, nit scaling, PQ encoding + UI composite
 *
 * Data Flow:
 *   - SceneTex (t0): kFRAMEBUFFER - ISHDR's output
 *     * SDR: Tonemapped, gamma-encoded values (0-1)
 *     * HDR: Linear values from ISHDR (can exceed 1.0 for highlights)
 *   - UITex (t1): UI layer with premultiplied alpha
 *   - HDROutput (u0): Final swap chain buffer (PQ encoded for HDR, sRGB for SDR)
 *
 * @see ISHDR.hlsl for tonemapping, bloom, and color grading
 * @see HDR.cpp ApplyHDR() for the dispatch logic
 */

#include "Common/Color.hlsli"
#include "Common/SharedData.hlsli"

/// Scene texture - reads from kFRAMEBUFFER (ISHDR's output)
Texture2D<float4> SceneTex : register(t0);
/// UI texture - gamma-encoded with premultiplied alpha
Texture2D<float4> UITex : register(t1);
/// Output UAV - writes to swap chain
RWTexture2D<float4> HDROutput : register(u0);

cbuffer PerFrame : register(b0)
{
	float4 parameters0 : packoffset(c0);  ///< .x=enableHDR, .y=paperWhite nits, .z=peakNits, .w=skipUIComposite
	float4 parameters1 : packoffset(c1);  ///< .x=uiBrightness multiplier
}

/// Soft shoulder rolloff to prevent hard clipping at peak brightness
float3 HDRSoftClip(float3 colorNits, float paperWhite, float peakNits)
{
	peakNits = max(peakNits, paperWhite * 1.1);
	float shoulder = paperWhite * 2.0;
	
	float3 result;
	[unroll]
	for (int i = 0; i < 3; i++) {
		float x = colorNits[i];
		if (x <= shoulder) {
			result[i] = x;
		} else {
			float overshoot = x - shoulder;
			float range = peakNits - shoulder;
			float compressed = overshoot / (overshoot / range + 1.0);
			result[i] = shoulder + compressed;
		}
	}
	return result;
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

	float3 finalColor;

	// Check if Linear Lighting is enabled - determines if ISHDR outputs linear or gamma
	bool linearLighting = SharedData::linearLightingSettings.enableLinearLighting > 0;

	if (enableHDR) {
		// HDR path: ISHDR outputs linear (if LL enabled) or gamma-encoded values
		// Convert to linear if needed, then to BT.2020, scale to nits, and PQ encode
		float3 sceneLinear = max(0, scene.rgb);
		
		// If Linear Lighting is NOT enabled, ISHDR outputs gamma-encoded values
		if (!linearLighting) {
			// Convert gamma to linear for HDR processing (preserves >1.0 values)
			sceneLinear = pow(abs(sceneLinear), 2.2);
		}
		
		// Convert BT.709 to BT.2020 and scale to nits
		// ISHDR output of 1.0 = paperWhite nits, values >1.0 = brighter
		float3 sceneBT2020 = Color::BT709ToBT2020(sceneLinear);
		float3 sceneNits = sceneBT2020 * paperWhite;
		
		float3 finalNits;
		if (skipUIComposite) {
			finalNits = sceneNits;
		} else {
			// Composite UI (UI is gamma-encoded)
			float3 uiLinear = Color::GammaToTrueLinear(max(0, ui.rgb));
			float3 uiBT2020 = Color::BT709ToBT2020(uiLinear);
			float3 uiNits = uiBT2020 * (paperWhite * uiBrightness);
			finalNits = uiNits * ui.a + sceneNits * (1.0 - ui.a);
		}
		
		// Soft clip to peak brightness and PQ encode
		finalNits = HDRSoftClip(finalNits, paperWhite, peakNits);
		finalColor = Color::pq::Encode(finalNits / 10000.0, 10000.0);
	} else {
		// SDR path: ISHDR outputs tonemapped, gamma-encoded values
		// Just composite UI and pass through
		float3 sceneGamma = scene.rgb;
		
		float3 composited;
		if (skipUIComposite) {
			composited = sceneGamma;
		} else {
			float3 uiScaled = ui.rgb * uiBrightness;
			composited = uiScaled * ui.a + sceneGamma * (1.0 - ui.a);
		}
		finalColor = saturate(composited);
	}

	HDROutput[dispatchID.xy] = float4(finalColor, 1.0);
}
