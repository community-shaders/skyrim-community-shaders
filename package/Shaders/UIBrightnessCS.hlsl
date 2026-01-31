// Scales UI brightness for HDR before FidelityFX composites it
// In HDR mode: converts UI from gamma to linear, scales by paper white * uiBrightness
// In SDR mode: just applies brightness multiplier

#include "Common/Color.hlsli"

RWTexture2D<float4> UITex : register(u0);

cbuffer PerFrame : register(b0)
{
	float4 parameters0 : packoffset(c0);  // .x = enableHDR, .y = paperWhite, .z = peakNits, .w = skipUIComposite
	float4 parameters1 : packoffset(c1);  // .x = uiBrightness, .y = isSceneLinear
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID)
{
	float4 ui = UITex[dispatchID.xy];
	
	if (ui.a > 0.0) {
		bool enableHDR = parameters0.x > 0.5;
		float paperWhite = parameters0.y;
		float uiBrightness = parameters1.x;
		
		if (enableHDR) {
			// For HDR: FidelityFX composites in linear space
			// Convert UI from gamma to linear, scale by paper white and brightness
			float3 uiLinear = Color::GammaToTrueLinear(ui.rgb);
			float3 uiBT2020 = Color::BT709ToBT2020(uiLinear);
			ui.rgb = uiBT2020 * paperWhite * uiBrightness / 10000.0;  // Normalize for PQ
		} else {
			// For SDR: just apply brightness multiplier
			ui.rgb *= uiBrightness;
		}
	}
	
	UITex[dispatchID.xy] = ui;
}
