// Scales UI brightness and encodes for FidelityFX Frame Gen compositing
// For HDR: converts gamma UI to PQ so FidelityFX can blend PQ UI over PQ scene
// For SDR: just applies brightness multiplier

#include "Common/Color.hlsli"

RWTexture2D<float4> UITex : register(u0);

cbuffer PerFrame : register(b0)
{
	float4 parameters0 : packoffset(c0);  // .x = enableHDR, .y = paperWhite, .z = peakNits, .w = unused
	float4 parameters1 : packoffset(c1);  // .x = uiBrightness, .y = unused
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID)
{
	float4 ui = UITex[dispatchID.xy];
	
	if (ui.a > 0.0) {
		bool enableHDR = parameters0.x > 0.5;
		float paperWhite = parameters0.y;
		float peakNits = parameters0.z;
		float uiBrightness = parameters1.x;
		
		// Apply brightness multiplier in gamma space first
		ui.rgb *= uiBrightness;
		
		if (enableHDR) {
			// For HDR: encode UI to PQ so FidelityFX can blend PQ over PQ
			// gamma -> linear -> BT.2020 -> nits -> PQ
			float3 uiLinear = Color::GammaToTrueLinear(ui.rgb);
			float3 uiBT2020 = Color::BT709ToBT2020(uiLinear);
			float3 uiNits = uiBT2020 * paperWhite;
			ui.rgb = Color::pq::Encode(uiNits / 10000.0, 10000.0);
		}
		// For SDR: UI stays in gamma space, FidelityFX blends gamma over gamma
	}
	
	UITex[dispatchID.xy] = ui;
}
