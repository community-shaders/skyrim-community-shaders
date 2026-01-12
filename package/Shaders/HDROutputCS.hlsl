#include "Common/Color.hlsli"

Texture2D<float4> SceneTex : register(t0);
Texture2D<float4> UITex : register(t1);
RWTexture2D<float4> HDROutput : register(u0);

cbuffer PerFrame : register(b0)
{
	float4 parameters0 : packoffset(c0);  // .x = paperWhite, .y = peakNits, .z = tonemapToSDR
}

float3 Reinhard(float3 color)
{
	return color / (1.0 + color);
}

float3 ReinhardExtended(float3 color, float whitePoint)
{
	return color * (1.0 + color / (whitePoint * whitePoint)) / (1.0 + color);
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID)
{
	float4 scene = SceneTex[dispatchID.xy];
	float4 ui = UITex[dispatchID.xy];

	float paperWhiteNits = parameters0.x;
	float peakNits = parameters0.y;
	bool tonemapToSDR = parameters0.z > 0.5;

	float3 linearScene = scene.rgb;
	float3 finalColor;

	if (tonemapToSDR) {
		// SDR Path: Tonemap, convert to gamma space, blend UI, clamp to BT.709
		float3 tonemapped = Reinhard(linearScene);
		float3 gammaScene = Color::TrueLinearToGamma(tonemapped);

		// UI is already in gamma space (RGBA8 UNORM), blend over gamma scene
		float3 blended = lerp(gammaScene, ui.rgb, ui.a);

		// Clamp to BT.709 range (0-1) for SDR output
		finalColor = saturate(blended);
	} else {
		// HDR Path: Tonemap, blend UI in gamma space, output linear HDR
		float whitePoint = peakNits / paperWhiteNits;
		float3 tonemapped = ReinhardExtended(linearScene, whitePoint);

		// Convert to gamma space for UI blending
		float3 gammaScene = Color::TrueLinearToGamma(tonemapped);

		// UI is in gamma space, blend over gamma scene
		float3 blended = lerp(gammaScene, ui.rgb, ui.a);

		// Convert back to linear for HDR output
		float3 linearBlended = Color::GammaToTrueLinear(blended);

		// Scale to paper white nits and convert to BT.2020 PQ for HDR10 output
		float3 scaledLinear = linearBlended * (paperWhiteNits / 80.0);
		float3 bt2020 = Color::BT709ToBT2020(scaledLinear);
		finalColor = Color::pq::Encode(bt2020, peakNits);
	}

	HDROutput[dispatchID.xy] = float4(finalColor, 1.0);
}
