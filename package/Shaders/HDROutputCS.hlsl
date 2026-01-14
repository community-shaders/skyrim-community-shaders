#include "Common/Color.hlsli"

Texture2D<float4> SceneTex : register(t0);
Texture2D<float4> UITex : register(t1);
RWTexture2D<float4> HDROutput : register(u0);

cbuffer PerFrame : register(b0)
{
	float4 parameters0 : packoffset(c0);  // .x = enableHDR, .y = paperWhite, .zw = reserved
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID)
{
	float4 scene = SceneTex[dispatchID.xy];
	float4 ui = UITex[dispatchID.xy];

	float enableHDR = parameters0.x;
	float paperWhite = parameters0.y;

	float3 finalColor;
	if (enableHDR > 0.5) {
		// HDR10 path:
		// Scene is linear from ISHDR, convert to gamma for UI compositing
		float3 gammaColor = Color::TrueLinearToGamma(scene.rgb);
		// Blend SDR UI in gamma space (UI is already gamma-encoded)
		gammaColor = gammaColor * (1.0 - ui.a) + ui.rgb;
		// Convert back to linear, apply paper white scaling, PQ encode
		float3 linearComposited = Color::GammaToTrueLinear(gammaColor);
		finalColor = Color::pq::Encode(linearComposited, paperWhite);
	} else {
		// SDR path: vanilla behavior
		// Scene from ISHDR is already in the correct format for sRGB display
		// Just blend UI and clamp - the sRGB swap chain color space handles gamma
		float3 sceneColor = saturate(scene.rgb);
		finalColor = sceneColor * (1.0 - ui.a) + ui.rgb;
	}

	HDROutput[dispatchID.xy] = float4(finalColor, 1.0);
}
