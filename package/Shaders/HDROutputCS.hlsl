#include "Common/Color.hlsli"

Texture2D<float4> Framebuffer : register(t0);
RWTexture2D<float4> HDROutput : register(u0);

cbuffer PerFrame : register(b0)
{
	// parameters0.x = paperWhite
	// parameters0.y = peakNits
	// parameters0.z = exposure
	// parameters0.w = unused
	float4 parameters0 : packoffset(c0);
	// parameters1.x = highlights
	// parameters1.y = shadows
	// parameters1.z = contrast
	// parameters1.w = saturation
	float4 parameters1 : packoffset(c1);
	// parameters2.x = dechroma
	// parameters2.y = hueCorrectionStrength
	// parameters2.z = 0.f // Currently unused
	// parameters2.w = 0.f // Currently unused
	float4 parameters2 : packoffset(c2);
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
	float peakLinear = peakNits / paperWhiteNits;
	float3 compressed = color / (1.0 + color / peakLinear);
	return compressed;
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID
								: SV_DispatchThreadID) {
	float4 framebuffer = Framebuffer[dispatchID.xy];

#ifdef HDR_INPUT
	float3 linearColor = framebuffer.xyz;
#else
	float3 linearColor = Color::GammaToLinearSafe(framebuffer.xyz);
#endif

	float3 originalColor = linearColor;
	float exposure = parameters0.z;
	float3 exposedColor = linearColor * exposure;

#ifdef SDR_OUTPUT
	float highlights = parameters1.x;
	float shadows = parameters1.y;
	float contrast = parameters1.z;
	float saturation = parameters1.w;
	float dechroma = parameters2.x;
	float hueCorrectionStrength = parameters2.y;

	float3 processedColor = exposedColor;
	processedColor = ApplyHighlightsShadows(processedColor, highlights, shadows);
	processedColor = ApplyContrast(processedColor, contrast);
	processedColor = ApplySaturation(processedColor, saturation);
	processedColor = ApplyDechroma(processedColor, dechroma);
	if (hueCorrectionStrength > 0.0)
		processedColor = ApplyHueCorrection(processedColor, originalColor * exposure, hueCorrectionStrength);

	float3 sdrColor = Color::LinearToGammaSafe(max(0.0, processedColor));
	HDROutput[dispatchID.xy] = float4(sdrColor, framebuffer.w);
#else
	float paperWhiteNits = parameters0.x;
	float peakNits = parameters0.y;
	float highlights = parameters1.x;
	float shadows = parameters1.y;
	float contrast = parameters1.z;
	float saturation = parameters1.w;
	float dechroma = parameters2.x;
	float hueCorrectionStrength = parameters2.y;

	float3 processedColor = exposedColor;
	processedColor = ApplyHighlightsShadows(processedColor, highlights, shadows);
	processedColor = ApplyContrast(processedColor, contrast);
	processedColor = ApplySaturation(processedColor, saturation);
	processedColor = ApplyDechroma(processedColor, dechroma);
	if (hueCorrectionStrength > 0.0)
		processedColor = ApplyHueCorrection(processedColor, originalColor * exposure, hueCorrectionStrength);

	float3 tonemapped = HDRTonemap(max(0.0, processedColor), peakNits, paperWhiteNits);
	float3 bt2020Linear = Color::BT709ToBT2020(tonemapped);
	float3 pqColor = Color::pq::Encode(bt2020Linear, paperWhiteNits);
	
	HDROutput[dispatchID.xy] = float4(pqColor, framebuffer.w);
#endif
}
