#include "Common/Color.hlsli"

Texture2D<float4> SceneTex : register(t0);
Texture2D<float4> UITex : register(t1);
RWTexture2D<float4> HDROutput : register(u0);

cbuffer PerFrame : register(b0)
{
	float4 parameters0 : packoffset(c0);  // .x = enableHDR, .y = paperWhite, .z = peakNits, .w = skipUIComposite
	float4 parameters1 : packoffset(c1);  // .x = uiBrightness, .y = isSceneLinear
}

// Soft shoulder rolloff to prevent hard clipping at peak brightness (HDR only)
// Uses Reinhard-style compression above shoulder threshold
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
	bool isSceneLinear = parameters1.y > 0.5;

	float3 finalColor;
	
	// Scene from ISHDR is always gamma-encoded (tonemapper includes gamma 2.2 curve)
	// Convert to linear for processing, regardless of Linear Lighting status
	float3 sceneLinear = isSceneLinear ? max(0, scene.rgb) : Color::GammaToTrueLinear(max(0, scene.rgb));

	if (enableHDR) {
		// HDR output path: composite in gamma space (if not skipped), then convert to BT.2020 and PQ encode
		
		// Convert linear scene to gamma for compositing (UI is in gamma space)
		float3 sceneGamma = Color::TrueLinearToGamma(sceneLinear);
		
		float3 composited;
		if (skipUIComposite) {
			// FG handles UI compositing after frame interpolation - skip our compositing
			composited = sceneGamma;
		} else {
			// Match FidelityFX standard alpha blend (non-premultiplied formula)
			// FidelityFX without USE_PREMUL_ALPHA flag uses: Final = UI.RGB * UI.A + Scene * (1 - UI.A)
			float3 uiScaled = ui.rgb * uiBrightness;
			composited = uiScaled * ui.a + sceneGamma * (1.0 - ui.a);
		}
		
		// Convert composited result: gamma -> linear -> BT.2020 -> nits -> soft clip -> PQ
		float3 compositedLinear = Color::GammaToTrueLinear(max(0, composited));
		float3 compositedBT2020 = Color::BT709ToBT2020(compositedLinear);
		float3 compositedNits = compositedBT2020 * paperWhite;
		compositedNits = HDRSoftClip(compositedNits, paperWhite, peakNits);
		finalColor = Color::pq::Encode(compositedNits / 10000.0, 10000.0);
	} else {
		// SDR output path: gamma 2.2 output
		float3 sceneGamma = Color::TrueLinearToGamma(sceneLinear);
		
		float3 composited;
		if (skipUIComposite) {
			// FG handles UI compositing - skip our compositing
			composited = sceneGamma;
		} else {
			// Match FidelityFX standard alpha blend (non-premultiplied formula)
			// FidelityFX without USE_PREMUL_ALPHA flag uses: Final = UI.RGB * UI.A + Scene * (1 - UI.A)
			float3 uiScaled = ui.rgb * uiBrightness;
			composited = uiScaled * ui.a + sceneGamma * (1.0 - ui.a);
		}
		finalColor = composited;
	}

	HDROutput[dispatchID.xy] = float4(enableHDR ? finalColor : saturate(finalColor), 1.0);
}
