#include "Common/Color.hlsli"

SamplerState LinearSampler : register(s0);

Texture2D<float4> SceneTex : register(t0);
Texture2D<float4> UIBuffer : register(t1);
Texture2D<float4> BloomTex : register(t2);
Texture2D<float4> AdaptTex : register(t3);
RWTexture2D<float4> HDROutput : register(u0);

cbuffer PerFrame : register(b0)
{
	float4 parameters0 : packoffset(c0);  // .x = paperWhite, .y = peakNits, .z = exposure
	float4 parameters1 : packoffset(c1);  // .x = highlights, .y = shadows, .z = contrast, .w = saturation
	float4 parameters2 : packoffset(c2);  // .x = dechroma, .y = hueCorrectionStrength, .z = vanillaEyeAdaptation, .w = vanillaBloom
	float4 parameters3 : packoffset(c3);  // .x = bypassTonemapping, .y = hdrMode (1.0 = HDR display detected)
}

float3 ApplyHighlightsShadows(float3 color, float highlights, float shadows)
{
	float luminance = Color::RGBToLuminance(color);
	float highlightWeight = saturate(luminance - 0.5) * 2.0;
	float shadowWeight = saturate(0.5 - luminance) * 2.0;
	float multiplier = lerp(1.0, highlights, highlightWeight) * lerp(1.0, shadows, shadowWeight);
	return color * multiplier;
}

float3 ApplyContrast(float3 color, float contrast)
{
	float midGray = 0.18;
	return midGray + (color - midGray) * contrast;
}

float3 ApplySaturation(float3 color, float saturation)
{
	float luminance = Color::RGBToLuminance(color);
	return lerp(luminance.xxx, color, saturation);
}

float3 ApplyDechroma(float3 color, float dechroma)
{
	float luminance = Color::RGBToLuminance(color);
	float saturationReduction = saturate(luminance) * dechroma;
	return lerp(color, luminance.xxx, saturationReduction);
}

float3 ApplyHueCorrection(float3 color, float3 originalColor, float strength)
{
	float3 originalOklab = Color::BT709ToOKLab(max(0.001, originalColor));
	float3 currentOklab = Color::BT709ToOKLab(max(0.001, color));
	currentOklab.yz = lerp(currentOklab.yz, originalOklab.yz * currentOklab.x / max(0.001, originalOklab.x), strength);
	return Color::OkLabToBT709(currentOklab);
}

float3 HDRTonemap(float3 color, float peakNits, float paperWhiteNits)
{
	// Use vanilla HejlBurgessDawson curve without clamping, linear output
	// Scale input to prevent crushing blacks, then scale output to HDR range
	float3 x = max(0.0, color);
	float3 numerator = (x * 6.2 + 0.5) * x;
	float3 denominator = max(1e-6, x * (x * 6.2 + 1.7) + 0.06);
	float3 result = numerator / denominator;
	
	// Scale to paper white brightness for HDR display
	return result * paperWhiteNits / 80.0;
}

float3 ApplyColorGrading(float3 color, float3 originalColor, float highlights, float shadows, float contrast, float saturation, float dechroma, float hueCorrectionStrength)
{
	float3 result = color;
	result = ApplyHighlightsShadows(result, highlights, shadows);
	result = ApplyContrast(result, contrast);
	result = ApplySaturation(result, saturation);
	if (dechroma > 0.0)
		result = ApplyDechroma(result, dechroma);
	if (hueCorrectionStrength > 0.0)
		result = ApplyHueCorrection(result, originalColor, hueCorrectionStrength);
	
	return result;
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID)
{
	uint2 dims;
	HDROutput.GetDimensions(dims.x, dims.y);
	float2 uv = (float2(dispatchID.xy) + 0.5) / float2(dims);
	
	float4 scene = SceneTex[dispatchID.xy];
	float4 ui = UIBuffer[dispatchID.xy];
	float3 bloom = BloomTex[dispatchID.xy].rgb;
	float2 adaptValue = AdaptTex.SampleLevel(LinearSampler, uv, 0).xy;
	
	bool hdrMode = parameters3.y > 0.5;

#ifdef SDR_OUTPUT
	// SDR mode on HDR display: ISHDR has already done tonemapping and output gamma space
	// Need to convert gamma SDR to PQ for proper HDR display output
	// This maps SDR [0-1] gamma to HDR display's SDR range
	
	// Composite UI first (both in gamma space)
	float3 sdrGamma = lerp(scene.rgb, ui.rgb, ui.a);
	
	// Convert gamma to linear
	float3 sdrLinear = Color::GammaToLinearSafe(sdrGamma);
	
	// SDR content on HDR display should be at paper white level
	float paperWhiteNits = parameters0.x;
	
	// Convert to BT.2020 and encode to PQ at paper white level
	float3 bt2020Linear = Color::BT709ToBT2020(sdrLinear);
	float3 pqColor = Color::pq::Encode(bt2020Linear, paperWhiteNits);
	
	HDROutput[dispatchID.xy] = float4(pqColor, scene.w);
#else
	// HDR mode: kMAIN contains gamma-space rendering from vanilla shaders
	// Convert to linear for proper HDR processing
	float3 linearScene = Color::GammaToLinearSafe(scene.rgb);
	
	float exposure = parameters0.z;
	bool useVanillaAdaptation = parameters2.z > 0.5;
	bool useVanillaBloom = parameters2.w > 0.5;
	
	// Apply eye adaptation
	if (useVanillaAdaptation && adaptValue.x > 0.0 && adaptValue.y > 0.0)
		linearScene *= adaptValue.y / adaptValue.x;
	linearScene = max(0.0, linearScene);
	
	// Bloom is also in gamma space, convert to linear
	float3 linearBloom = Color::GammaToLinearSafe(bloom);
	
	// Apply bloom
	float3 sceneWithBloom = useVanillaBloom ? (linearScene + linearBloom) : linearScene;
	
	// Apply exposure
	float3 exposedScene = sceneWithBloom * exposure;
	
	float paperWhiteNits = parameters0.x;
	float peakNits = parameters0.y;
	float highlights = parameters1.x;
	float shadows = parameters1.y;
	float contrast = parameters1.z;
	float saturation = parameters1.w;
	float dechroma = parameters2.x;
	float hueCorrectionStrength = parameters2.y;
	bool bypassTonemapping = parameters3.x > 0.5;

	// Save original before color grading for hue correction reference
	float3 originalColor = exposedScene;
	float3 processedColor = ApplyColorGrading(exposedScene, originalColor, highlights, shadows, contrast, saturation, dechroma, hueCorrectionStrength);
	processedColor = max(0.0, processedColor);

	float3 outputColor;
	if (bypassTonemapping) {
		outputColor = processedColor;
	} else {
		outputColor = HDRTonemap(processedColor, peakNits, paperWhiteNits);
	}

	float3 bt2020Linear = Color::BT709ToBT2020(outputColor);
	
	HDROutput[dispatchID.xy] = float4(pqColor, scene.w);
#endif
}
