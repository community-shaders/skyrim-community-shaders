#include "Common/Color.hlsli"
#include "Common/reinhard.hlsl"

Texture2D<float4> SceneTex : register(t0);
Texture2D<float4> UIBuffer : register(t1);
RWTexture2D<float4> HDROutput : register(u0);

cbuffer PerFrame : register(b0)
{
	float4 parameters0 : packoffset(c0);  // .x = paperWhite, .y = peakNits, .z = tonemapToSDR
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID)
{
	float4 scene = SceneTex[dispatchID.xy];
	float4 ui = UIBuffer[dispatchID.xy];
	
	float paperWhiteNits = parameters0.x;
	float peakNits = parameters0.y;
	bool tonemapToSDR = parameters0.z > 0.5;

	// Vanilla ISHDR outputs linear HDR to kMAIN - always process as HDR
	float3 linearScene = scene.rgb;
	
	// Composite UI (UI is in gamma space, convert to linear)
	float3 uiLinear = Color::GammaToLinearSafe(ui.rgb);
	float3 composited = lerp(linearScene, uiLinear, ui.a);
	
	float3 finalColor;
	
	if (tonemapToSDR) {
		// Apply Reinhard tonemap to SDR (0-1 range for standard displays)
		finalColor = renodx::tonemap::Reinhard(composited);
		
		// Convert to gamma space for SDR display
		finalColor = Color::LinearToGamma(finalColor);
	} else {
		// HDR display output: Apply Reinhard to prevent clipping, then convert to BT.2020 and encode to PQ
		float3 tonemapped = renodx::tonemap::Reinhard(composited);
		float3 bt2020Linear = Color::BT709ToBT2020(tonemapped);
		finalColor = Color::pq::Encode(bt2020Linear, peakNits);
	}
	
	HDROutput[dispatchID.xy] = float4(finalColor, scene.w);
}
