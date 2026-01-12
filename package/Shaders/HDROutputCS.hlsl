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
	const static float peakLuminance = 250.0;
	const static float paperWhiteRef = 80.0;

	float3 color = scene.rgb;

	float sdrPeakLinear = peakLuminance / paperWhiteRef;
	float hdrScaling = paperWhite / paperWhiteRef;

	float3 sdrCompressed = renodx::tonemap::ReinhardExtended(color, sdrPeakLinear * 2.0, 1.0, 0.0);
	float3 hdrScaled = color * hdrScaling;
	color = lerp(hdrScaled, sdrCompressed, sdrMode);

	float peakLinear = peakNits / paperWhiteRef;
	float3 tonemapped = renodx::tonemap::ReinhardExtended(color, peakLinear, 1.0, 0.0);
	color = lerp(color, tonemapped, enableTonemapping);

	float3 pqEncoded = Color::pq::Encode(color, peakNits);
	float3 gammaEncoded = Color::TrueLinearToGamma(color);
	color = lerp(pqEncoded, gammaEncoded, convertToGamma);

	// Simply alpha-blend the gamma UI onto the gamma-encoded scene
	color = color * (1.0 - ui.a) + ui.rgb;

	HDROutput[dispatchID.xy] = float4(color, 1.0);
}
