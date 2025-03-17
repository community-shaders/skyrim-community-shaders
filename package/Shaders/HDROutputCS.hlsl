
#include "Common/Color.hlsli"
#include "Common/DisplayMapping.hlsli"
#include "Common/PumboDICE.hlsli"
#include "Common/Uncharted2Tonemapper.hlsli"
#include "Common/frostbite.hlsli"

Texture2D<float4> Framebuffer : register(t0);
Texture2D<float4> UI : register(t1);

RWTexture2D<float4> HDROutput : register(u0);

cbuffer PerFrame : register(b0)
{
	float4 HDRData;
};

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	float3 framebuffer = Framebuffer[dispatchID.xy];
	float4 ui = UI[dispatchID.xy];

	// Tonemap game render before UI blending
#if 0
    // Gamma space tonemapping results are more pleasing
	// Investigate linear tonemapping with lowered exposure	
    framebuffer.rgb = tonemap::frostbite::BT709(framebuffer.rgb, Color::LinearToGammaSafe(HDRData.y / HDRData.z), 0.5);
#else
	float exposureBias = 1.0;
	float hue_correction_strength = 0.5;  // recommended range: below 1
	float shoulderStart = 0.15;           // recommended range: 0.25 - 0.5

	float3 untonemapped = Color::GammaToLinearSafe(framebuffer.rgb) * exposureBias;
	framebuffer.rgb = applyPumboDICE(untonemapped.rgb, HDRData.z, HDRData.y, shoulderStart);
	framebuffer.rgb = Color::Correct::Hue(framebuffer.rgb, applyUncharted2Tonemap(untonemapped), hue_correction_strength);
	framebuffer.rgb = Color::LinearToGammaSafe(framebuffer.rgb);

#endif
	// Scale UI as a ratio of game brightness
	ui.xyz = Color::GammaToLinear(ui.xyz);
	ui.xyz *= HDRData.w / HDRData.z;
	ui.xyz = Color::LinearToGamma(ui.xyz);

	// Apply the Reinhard tonemapper on any background color in excess, to avoid it burning it through the UI.
	float3 excessBackgroundColor = framebuffer - min(1.0, framebuffer);
	float3 tonemappedBackgroundColor = excessBackgroundColor / (1.0 + excessBackgroundColor);
	framebuffer = min(1.0, framebuffer) + lerp(tonemappedBackgroundColor, excessBackgroundColor, 1.0 - ui.a);

	// Blend UI
	framebuffer = ui.xyz + framebuffer * (1.0 - ui.a);

	framebuffer = Color::GammaToLinearSafe(framebuffer);

	// Scale combined UI and game render by game brightness
	framebuffer *= HDRData.z;

	// Convert to BT.2020 and Encode in PQ
	framebuffer = Color::BT709ToBT2020(framebuffer);
	framebuffer = DisplayMapping::LinearToPQ(framebuffer, 10000.f);

	HDROutput[dispatchID.xy] = float4(framebuffer, 1.0);
};
