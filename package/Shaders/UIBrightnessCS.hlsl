// Scales UI brightness for FidelityFX Frame Gen compositing (SDR only)
// For HDR, UI compositing is handled in HDROutputCS.hlsl to ensure correct gamma-space blending

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
		float uiBrightness = parameters1.x;
		
		// Apply brightness multiplier in gamma space
		// For SDR FG: FidelityFX composites this gamma UI over gamma scene
		// For HDR: This shader is not called (UI compositing happens in HDROutputCS)
		ui.rgb *= uiBrightness;
	}
	
	UITex[dispatchID.xy] = ui;
}
