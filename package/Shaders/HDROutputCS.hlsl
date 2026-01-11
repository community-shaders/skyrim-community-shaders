#include "Common/Color.hlsli"

Texture2D<float4> SceneTex : register(t0);
Texture2D<float4> UIBuffer : register(t1);
RWTexture2D<float4> HDROutput : register(u0);

cbuffer PerFrame : register(b0)
{
	float4 parameters0 : packoffset(c0);  // .x = paperWhite, .y = peakNits, .z = hdrMode
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID)
{
	float4 scene = SceneTex[dispatchID.xy];
	float4 ui = UIBuffer[dispatchID.xy];
	
	float paperWhiteNits = parameters0.x;
	float peakNits = parameters0.y;
	bool hdrMode = parameters0.z > 0.5;

#ifdef SDR_OUTPUT
	// SDR mode on HDR display: Vanilla has tonemapped to gamma space
	// Composite UI (both in gamma space)
	float3 sdrGamma = lerp(scene.rgb, ui.rgb, ui.a);
	
	// Convert to linear, then to BT.2020 and encode to PQ
	float3 sdrLinear = Color::GammaToLinearSafe(sdrGamma);
	float3 bt2020Linear = Color::BT709ToBT2020(sdrLinear);
	float3 pqColor = Color::pq::Encode(bt2020Linear, paperWhiteNits);
	
	HDROutput[dispatchID.xy] = float4(pqColor, scene.w);
#else
	// HDR mode: Vanilla ISHDR outputs linear HDR to kMAIN
	// Apply simple tonemap and encode to PQ
	float3 linearScene = scene.rgb;
	
	// Simple filmic tonemap
	float3 x = max(0.0, linearScene);
	float3 tonemapped = (x * (x * 6.2 + 0.5)) / (x * (x * 6.2 + 1.7) + 0.06);
	float3 hdrOutput = tonemapped * paperWhiteNits / 80.0;
	
	// Convert to BT.2020 and encode to PQ
	float3 bt2020Linear = Color::BT709ToBT2020(hdrOutput);
	float3 pqColor = Color::pq::Encode(bt2020Linear, peakNits);
	
	// Composite UI (UI is in gamma space)
	float3 uiLinear = Color::GammaToLinearSafe(ui.rgb);
	float3 uiBT2020 = Color::BT709ToBT2020(uiLinear);
	float3 uiPQ = Color::pq::Encode(uiBT2020, paperWhiteNits);
	float3 finalColor = lerp(pqColor, uiPQ, ui.a);
	
	HDROutput[dispatchID.xy] = float4(finalColor, scene.w);
#endif
}
