/**
 * @file HDROutputCS.hlsl
 * @brief Final output compute shader - handles BOTH HDR and SDR display modes.
 *
 * @details This shader is the final stage of the rendering pipeline when the HDR
 *   feature is loaded. It completely replaces ISHDR's output to the swap chain.
 *
 * Pipeline Overview:
 *   1. Scene renders to kMAIN (R16G16B16A16_FLOAT) with linear values
 *   2. ISHDR runs tonemapping/bloom and writes result back to kMAIN
 *   3. This shader reads from kMAIN and writes final output to swap chain
 *
 * Data Flow:
 *   - SceneTex (t0): kMAIN render target
 *     * For SDR: Contains post-ISHDR tonemapped/gamma-encoded values
 *     * For HDR: Contains linear values (>1.0 for highlights) - we read before ISHDR modifies
 *   - UITex (t1): UI layer with premultiplied alpha
 *   - HDROutput (u0): Final swap chain buffer
 *     * HDR: R10G10B10A2 with HDR10/PQ color space
 *     * SDR: Standard sRGB
 *
 * @see ISHDR.hlsl for vanilla tonemapping (still runs but output is overwritten)
 * @see HDR.cpp ApplyHDR() for the dispatch logic
 */

#include "Common/Color.hlsli"
#include "Common/SharedData.hlsli"

/// Scene texture - reads from kMAIN (pre-tonemapped linear HDR values)
Texture2D<float4> SceneTex : register(t0);
/// UI texture - gamma-encoded with premultiplied alpha
Texture2D<float4> UITex : register(t1);
/// Output UAV - writes to HDR10 swap chain (PQ encoded)
RWTexture2D<float4> HDROutput : register(u0);

cbuffer PerFrame : register(b0)
{
	float4 parameters0 : packoffset(c0);  ///< .x=enableHDR, .y=paperWhite nits, .z=peakNits, .w=skipUIComposite
	float4 parameters1 : packoffset(c1);  ///< .x=uiBrightness multiplier
}

/**
 * @brief Soft shoulder rolloff to prevent hard clipping at peak brightness.
 * @param colorNits Input color in nits (cd/m²)
 * @param paperWhite SDR reference white level in nits
 * @param peakNits Display peak brightness in nits
 * @return Compressed color that smoothly approaches peakNits
 */
float3 HDRSoftClip(float3 colorNits, float paperWhite, float peakNits)
{
	// Ensure peak is always above paper white to avoid broken math
	peakNits = max(peakNits, paperWhite * 1.1);
	
	float shoulder = paperWhite * 2.0;  // Start rolloff at 2x paper white
	
	float3 result;
	[unroll]
	for (int i = 0; i < 3; i++) {
		float x = colorNits[i];
		if (x <= shoulder) {
			result[i] = x;
		} else {
			// Attempt at smooth Reinhard-style compression from shoulder to peak
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

	if (enableHDR) {
		// kMAIN contains LINEAR scene values that can exceed 1.0 for bright areas
		// These are PRE-tonemapped values - we need to tonemap for HDR output
		float3 sceneLinear = max(0, scene.rgb);
		
		// Check if Linear Lighting is enabled
		bool linearLighting = SharedData::linearLightingSettings.enableLinearLighting > 0;
		
		// If Linear Lighting is NOT enabled, kMAIN is gamma-encoded
		// We need to convert to linear first, then process
		if (!linearLighting) {
			// Scene is gamma-encoded, convert to linear for proper HDR processing
			// Use pow for HDR-safe conversion (no clamping)
			sceneLinear = pow(abs(sceneLinear), 2.2);
		}
		
		// Map scene luminance to nits:
		// - 1.0 linear = paperWhite nits (SDR reference white)
		// - Values > 1.0 = highlights above SDR white, up to peakNits
		float3 sceneBT2020 = Color::BT709ToBT2020(sceneLinear);
		float3 sceneNits = sceneBT2020 * paperWhite;
		
		float3 finalNits;
		if (skipUIComposite) {
			finalNits = sceneNits;
		} else {
			// Composite UI (UI is gamma-encoded with premultiplied alpha)
			float3 uiLinear = Color::GammaToTrueLinear(max(0, ui.rgb));
			float3 uiBT2020 = Color::BT709ToBT2020(uiLinear);
			float3 uiNits = uiBT2020 * (paperWhite * uiBrightness);
			// Premultiplied alpha blend
			finalNits = uiNits + sceneNits * (1.0 - ui.a);
		}
		
		// Apply soft rolloff to prevent hard clipping at peak brightness
		finalNits = HDRSoftClip(finalNits, paperWhite, peakNits);
		
		// Encode to PQ for HDR10 output
		finalColor = Color::pq::Encode(finalNits / 10000.0, 10000.0);
	} else {
		// SDR output path
		// Scene from kMAIN is already tonemapped and gamma-encoded by ISHDR
		float3 sceneGamma = scene.rgb;
		
		float3 composited;
		if (skipUIComposite) {
			// FG handles UI compositing - skip our compositing
			composited = sceneGamma;
		} else {
			// Alpha blend with UI brightness scaling
			// UI brightness multiplier is independent of scene brightness in SDR mode
			float3 uiScaled = ui.rgb * uiBrightness;
			composited = uiScaled * ui.a + sceneGamma * (1.0 - ui.a);
		}
		finalColor = composited;
	}

	HDROutput[dispatchID.xy] = float4(enableHDR ? finalColor : saturate(finalColor), 1.0);
}
