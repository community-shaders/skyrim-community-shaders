/**
 * @file HDROutputCS.hlsl
 * @brief HDR: gamma decode, paper-white × (nits/203), BT.2020, PQ. SDR: passthrough + UI.
 */

#include "Common/Color.hlsli"
#include "Common/SharedData.hlsli"

Texture2D<float4> SceneTex : register(t0);
Texture2D<float4> UITex : register(t1);
RWTexture2D<float4> HDROutput : register(u0);

cbuffer PerFrame : register(b0)
{
	float enableHDR : packoffset(c0.x);
	float paperWhite : packoffset(c0.y);
	float peakNits : packoffset(c0.z);
	float skipUIComposite : packoffset(c0.w);
	float uiBrightness : packoffset(c1.x);
	float isSceneLinear : packoffset(c1.y);
	float isMainOrLoadingMenu : packoffset(c1.z);
	float fgTweenMenuMidAlphaBoost : packoffset(c1.w);  ///< TweenMenu: soften AA band when compositing here (UIBrightnessCS skips while paused)
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	uint width, height;
	HDROutput.GetDimensions(width, height);
	if (dispatchID.x >= width || dispatchID.y >= height)
		return;

	float4 scene = SceneTex[dispatchID.xy];
	float4 ui = UITex[dispatchID.xy];

	bool hdrEnabled = enableHDR > 0.5;
	bool skipUI = skipUIComposite > 0.5;

	float3 finalColor;

	if (hdrEnabled) {
		static const float HDR_TONEMAP_REF_WHITE_NITS = 203.0;
		float paperWhiteNits = max(paperWhite, 1.0);
		float paperWhiteDisplayScale = paperWhiteNits / HDR_TONEMAP_REF_WHITE_NITS;

		float3 sceneLinear = Color::SkyrimGammaToLinear(max(0.0, scene.rgb));
		sceneLinear *= paperWhiteDisplayScale;

		float3 sceneBT2020 = Color::BT709ToBT2020(sceneLinear);
		sceneBT2020 = max(sceneBT2020, 0.0);

		if (skipUI) {
			finalColor = Color::pq::Encode(sceneBT2020, sRGB_WhiteLevelNits);
		} else {
			const float menuUIBrightnessScale = 0.695652f;
			float effectiveUIBrightness = (isMainOrLoadingMenu > 0.5) ? (uiBrightness * menuUIBrightnessScale) : uiBrightness;
			// Match UIBrightnessCS: pause menu (TweenMenu) soft-AA band — FG PQ pass skips while paused, so boost runs here.
			float aIn = ui.a;
			float aOut = aIn;
			if (fgTweenMenuMidAlphaBoost > 0.5 && aIn > 1e-3) {
				float midBand = smoothstep(0.3, 0.35, aIn) * (1.0 - smoothstep(0.55, 0.6, aIn));
				const float fgMidAlphaBoost = 0.12;
				aOut = saturate(aIn + midBand * fgMidAlphaBoost);
			}
			float3 uiPremul = ui.rgb * (aOut / aIn);
			float3 uiLinear = Color::SkyrimGammaToLinear(max(0.0, uiPremul * effectiveUIBrightness));
			uiLinear *= paperWhiteDisplayScale;
			float a = aOut;
			float3 compositedLinear = uiLinear * a + sceneLinear * (1.0 - a);
			if (isMainOrLoadingMenu > 0.5) {
				const float menuSaturation = 1.25f;
				float luma = Color::RGBToLuminance(compositedLinear);
				compositedLinear = max(0.0, lerp(luma.xxx, compositedLinear, menuSaturation));
			}
			float3 compositedBT2020 = Color::BT709ToBT2020(compositedLinear);
			finalColor = Color::pq::Encode(max(0.0, compositedBT2020), sRGB_WhiteLevelNits);
		}
	} else {
		float3 sceneGamma = scene.rgb;

		if (skipUI) {
			finalColor = sceneGamma;
		} else {
			finalColor = ui.rgb + sceneGamma * (1.0 - ui.a);
		}

		finalColor = saturate(finalColor);
	}

	HDROutput[dispatchID.xy] = float4(finalColor, 1.0);
}