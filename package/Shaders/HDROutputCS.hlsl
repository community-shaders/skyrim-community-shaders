#include "Common/Color.hlsli"
#include "Common/reinhard.hlsl"

Texture2D<float4> SceneTex : register(t0);
Texture2D<float4> UITex : register(t1);
RWTexture2D<float4> HDROutput : register(u0);

cbuffer PerFrame : register(b0)
{
	float4 parameters0 : packoffset(c0);  // .x = sdrMode, .y = convertToGamma, .z = enableTonemapping, .w = peakNits
	float4 parameters1 : packoffset(c1);  // .x = paperWhite
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID)
{
	float4 scene = SceneTex[dispatchID.xy];
	float4 ui = UITex[dispatchID.xy];

	float sdrMode = parameters0.x;
	float convertToGamma = parameters0.y;
	float enableTonemapping = parameters0.z;
	float peakNits = parameters0.w;
	float paperWhite = parameters1.x;

	// Raw linear HDR scene
	float3 color = scene.rgb;

	// SDR mode: compress to SDR luminance range (peak ~250 nits = 3.125x reference white)
	// HDR mode: scale by paper white for HDR display
	float sdrPeakLinear = 250.0 / 80.0;  // SDR peak = 250 nits
	float hdrScaling = paperWhite / 80.0;

	// In SDR mode, apply Reinhard compression to fit HDR content into SDR range
	// In HDR mode, just scale by paper white
	float3 sdrCompressed = renodx::tonemap::ReinhardExtended(color, sdrPeakLinear * 2.0, 1.0, 0.0);
	float3 hdrScaled = color * hdrScaling;
	color = lerp(hdrScaled, sdrCompressed, sdrMode);

	// Apply additional tonemapping if enabled (for HDR highlight control)
	float peakLinear = peakNits / 80.0;
	float3 tonemapped = renodx::tonemap::ReinhardExtended(color, peakLinear, 1.0, 0.0);
	color = lerp(color, tonemapped, enableTonemapping);

	// UI blending - UI is in gamma space (sRGB), blend with scene in linear space
	// Always blend in linear space before any output encoding
	float3 uiLinear = Color::GammaToTrueLinear(ui.rgb);

	// Scale UI for HDR brightness (SDR UI at paper white level in HDR mode)
	float uiHdrScale = lerp(hdrScaling, 1.0, sdrMode);
	uiLinear *= uiHdrScale;

	// Blend UI onto scene in linear space
	color = lerp(color, uiLinear, ui.a);

	// Apply output encoding
	// convertToGamma: 0 = PQ encoding for HDR10, 1 = sRGB gamma for SDR/compatibility
	float3 pqEncoded = Color::pq::Encode(color, peakNits);
	float3 gammaEncoded = Color::TrueLinearToGamma(color);
	color = lerp(pqEncoded, gammaEncoded, convertToGamma);

	HDROutput[dispatchID.xy] = float4(color, 1.0);
}
