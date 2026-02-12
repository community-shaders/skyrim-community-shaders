/**
 * @file ISHDR.hlsl
 * @brief Skyrim's tonemapping and post-processing imagespace shader.
 *
 * @details This shader handles:
 *   - DOWNSAMPLE: Luminance downsampling for auto-exposure calculation
 *   - BLEND: Final tonemapping, bloom compositing, and color grading
 *   - FADE: Screen fade effects (loading screens, etc.)
 *
 * HDR Pipeline:
 *   1. Scene renders to kMAIN with linear HDR values (can exceed 1.0)
 *   2. This shader (ISHDR BLEND) reads from BlendTex, applies bloom and color grading
 *      - SDR mode: Applies tonemapping to compress to 0-1 range, outputs gamma-encoded
 *      - HDR mode: Skips tonemapping to preserve >1.0 values.
 *        Bloom is Reinhard-compressed to SDR range to prevent excessive intensity.
 *        Gamma-encodes output unless Linear Lighting is active.
 *   3. Output goes to kFRAMEBUFFER, then HDROutputCS reads it for final processing:
 *      - SDR: Passthrough + UI composite
 *      - HDR: BT.2020 conversion, nit scaling, PQ encoding + UI composite
 *
 * @see HDROutputCS.hlsl for final format conversion and UI compositing
 * @see HDR.cpp for the C++ HDR feature implementation
 */

#include "Common/Color.hlsli"
#include "Common/DummyVSTexCoord.hlsl"
#include "Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color : SV_Target0;
};

#if defined(PSHADER)
SamplerState ImageSampler : register(s0);
#	if defined(DOWNSAMPLE)
SamplerState AdaptSampler : register(s1);
#	elif defined(BLEND)
SamplerState BlendSampler : register(s1);
#	endif
SamplerState AvgSampler : register(s2);

Texture2D<float4> ImageTex : register(t0);
#	if defined(DOWNSAMPLE)
Texture2D<float4> AdaptTex : register(t1);
#	elif defined(BLEND)
Texture2D<float4> BlendTex : register(t1);
#	endif
Texture2D<float4> AvgTex : register(t2);

cbuffer PerGeometry : register(b2)
{
	float4 Flags : packoffset(c0);
	float4 TimingData : packoffset(c1);
	float4 Param : packoffset(c2);      ///< .x=bloom intensity, .y=tonemap white point, .z=use HejlBurgessDawson
	float4 Cinematic : packoffset(c3);  ///< .x=saturation, .z=contrast, .w=intensity
	float4 Tint : packoffset(c4);       ///< .xyz=tint color, .w=tint amount
	float4 Fade : packoffset(c5);       ///< .xyz=fade color, .w=fade amount
	float4 BlurScale : packoffset(c6);
	float4 BlurOffsets[16] : packoffset(c7);
};

/// Reinhard tonemapping operator
float3 GetTonemapFactorReinhard(float3 luminance)
{
	return (luminance * (luminance * Param.y + 1)) / (luminance + 1);
}

/// Hejl-Burgess-Dawson filmic tonemapping operator (includes gamma)
float3 GetTonemapFactorHejlBurgessDawson(float3 luminance)
{
	float3 tmp = max(0, luminance - 0.004);
	return Param.y *
	       pow(((tmp * 6.2 + 0.5) * tmp) / (tmp * (tmp * 6.2 + 1.7) + 0.06), Color::GammaCorrectionValue);
}

#	include "Common/DisplayMapping.hlsli"

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

#	if defined(DOWNSAMPLE)
	// === Auto-Exposure Luminance Downsampling ===
	// Repeatedly downsample image to compute average luminance for exposure adjustment.
	// Output is used by BLEND pass to normalize scene brightness.

	float3 downsampledColor = 0;
	for (int sampleIndex = 0; sampleIndex < DOWNSAMPLE; ++sampleIndex) {
		float2 texCoord = BlurOffsets[sampleIndex].xy * BlurScale.xy + input.TexCoord;

		// Adjust for dynamic resolution scaling
		[branch] if (Flags.x > 0.5)
		{
			texCoord = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(texCoord);
		}

		float3 imageColor = max(0.0, ImageTex.Sample(ImageSampler, texCoord).xyz);

		// Extract luminance based on shader variant
#		if defined(RGB2LUM)
		// Full RGB to luminance conversion
		imageColor = Color::RGBToLuminance(imageColor);
#		elif (defined(LUM) || defined(LUMCLAMP)) && !defined(DOWNADAPT)
		// Use pre-computed luminance channel
		imageColor = imageColor.x;
#		endif
		// Accumulate weighted sample
		downsampledColor += imageColor * BlurOffsets[sampleIndex].z;
	}

#		if defined(DOWNADAPT)
	// Adaptive exposure — smoothly adjust luminance target over time.
	// Prevents jarring exposure changes when moving between light/dark areas.
	float2 adaptValue = max(0.001, AdaptTex.Sample(AdaptSampler, input.TexCoord).xy);
	float2 adaptDelta = downsampledColor.xy - adaptValue;
	// Clamp delta to prevent extreme exposure swings
	downsampledColor.xy =
		sign(adaptDelta) * clamp(abs(Param.wz * adaptDelta), 0.00390625, abs(adaptDelta)) +
		adaptValue;
#		endif
	psout.Color = float4(downsampledColor, BlurScale.z);

#	elif defined(BLEND)
	// === Final Tonemapping, Bloom Compositing, and Color Grading ===
	// This is the final pass that combines scene, bloom, adjusts colors, and encodes for output.
	// Output format depends on HDR mode:
	//   - SDR: Tonemapped to [0,1] gamma-encoded sRGB
	//   - HDR: Preserved >1.0 linear values, then converted to BT.2020 PQ for HDROutputCS

	float2 uv = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);

	// Main scene color (already linearly lit from engine)
	float3 inputColor = BlendTex.Sample(BlendSampler, uv).xyz;

	// Bloom from separate high-pass filtered texture
	float3 bloomColor = 0;
	if (Flags.x > 0.5) {
		bloomColor = ImageTex.Sample(ImageSampler, uv).xyz;
	} else {
		bloomColor = ImageTex.Sample(ImageSampler, input.TexCoord.xy).xyz;
	}

	// Auto-exposure values: (x=current luminance, y=target luminance)
	float2 avgValue = AvgTex.Sample(AvgSampler, input.TexCoord.xy).xy;

	bool isHDR = SharedData::HDRData.x > 0.5;

	float3 outputColor = 0.0;

	// === Auto-Exposure Adjustment ===
	// Normalizes scene brightness to target luminance computed by DOWNSAMPLE pass.
	// Prevents exposure from drifting as lighting conditions change.
	if (avgValue.x != 0 && avgValue.y != 0)
		inputColor *= avgValue.y / avgValue.x;
	inputColor = max(0, inputColor);

	if (isHDR) {
		// === HDR Pipeline (Float16 RT) ===
		// Input: HDR values (can exceed 1.0) - gamma-encoded (vanilla) OR linear (Linear Lighting)
		// Output: PQ-encoded BT.2020 for HDROutputCS → display

		float paperWhiteNits = SharedData::HDRData.y;
		float peakNits = SharedData::HDRData.z;

		// === Convert to Linear Space ===
		// If Linear Lighting is active, input is already linear.
		float3 hdrLinear = ENABLE_LL ? inputColor : Color::GammaToLinear(inputColor);

		// === Color Grading (Mixed Linear/Gamma, matching vanilla order) ===
		// Vanilla order: Saturation → Tint → Brightness → Contrast
		// Physical accuracy: Saturation in linear; Tint & Contrast in gamma for artistic control.

		// Saturation adjustment in linear (can generate negative RGB for highly saturated colors)
		hdrLinear = Color::Saturation(hdrLinear, Cinematic.x);

		// Tint and Contrast in gamma space (perceptual control matching SDR behavior)
		float3 hdrGamma = Color::LinearToGamma(hdrLinear);

		// Color tint in gamma (blend to monochrome tint if nonzero)
		float hdrLuminanceGamma = Color::RGBToLuminance(hdrGamma);
		hdrGamma = lerp(hdrGamma, hdrLuminanceGamma * Tint.xyz, Tint.w);

		// Convert back to linear for brightness (uniform intensity scaling)
		hdrLinear = Color::GammaToLinear(hdrGamma);
		hdrLinear *= Cinematic.w;

		// Contrast adjustment in gamma space (pivot around scene average)
		hdrGamma = Color::LinearToGamma(hdrLinear);
		hdrGamma = lerp(avgValue.x, hdrGamma, Cinematic.z);
		hdrLinear = Color::GammaToLinear(hdrGamma);

#		if defined(FADE)
		// Screen fade effect in linear (loading screens, death, etc.)
		hdrLinear = lerp(hdrLinear, Fade.xyz, Fade.w);
#		endif

		// === Bloom Compositing (Linear Space) ===
		// High-pass filtered bloom adds glow to bright areas.
		// Bloom is added in linear space to prevent excessive intensity at bright values.
		hdrLinear += saturate(Param.x - hdrLinear) * bloomColor;

		// === DICE Tonemapping ===
		// INPUT: Linear BT.709 color in 80-nit reference (1.0 = 80 nits)
		// Multiply paper-white scalar into color, as per Luma Framework design.
		// DICE then compresses highlights from paper-white to peak, preserving mid-tone detail.
		// OUTPUT: Still in 80-nit reference (1.0 = 80 nits), just with tonemapped values.
		float pw = paperWhiteNits / sRGB_WhiteLevelNits;
		float peak = peakNits / sRGB_WhiteLevelNits;
		hdrLinear *= pw;  // Paper-white multiplied in (e.g., 2.5 for 200 nits)
		hdrLinear = DisplayMapping::DICETonemap(hdrLinear, peak, 0.5, CS_BT709, CS_BT709);

		// === Color Space Conversion and PQ Encoding ===
		// Expand from BT.709 (SDR) to BT.2020 (HDR) for wider color gamut.
		float3 bt2020 = Color::BT709ToBT2020(hdrLinear);
		// Clamp to non-negative values to prevent NaN in pq::Encode pow operations
		bt2020 = max(bt2020, 0.0);
		// Encode to PQ curve: color remains in 80-nit reference (1.0 = 80 nits)
		outputColor = Color::pq::Encode(bt2020, sRGB_WhiteLevelNits);
	} else {
		// === SDR Pipeline (LDR RT) ===
		// Input: Linear HDR values (can exceed 1.0)
		// Output: Tonemapped [0,1] gamma sRGB for traditional displays

		float3 blendedColor;

		// === Tonemapping + Bloom Selection ===
		// Choose between two hue-preserving tonemap algorithms (user preference Param.z).

		[branch] if (Param.z > 0.5)
		{
			// Hejl-Burgess-Dawson: Smoother rolloff, better for cinematic look
			blendedColor = DisplayMapping::HuePreservingHejlBurgessDawson(inputColor, bloomColor);
		}
		else
		{
			// Reinhard: Hue-preserving tone compression
			// Extract luminance and compress with Reinhard curve
			float maxCol = Color::RGBToLuminance(inputColor);
			float mappedMax = GetTonemapFactorReinhard(maxCol).x;
			// Apply compression uniformly to preserve hue
			float3 compressedHuePreserving = inputColor * mappedMax / maxCol;
			blendedColor = compressedHuePreserving;
			// Add bloom to tonemapped result
			blendedColor += saturate(Param.x - blendedColor) * bloomColor;
		}

		// === Color Grading (Post-Tonemap) ===
		// Apply saturation, contrast, and tint to the tonemapped result.

		float blendedLuminance = Color::RGBToLuminance(blendedColor);

		// Saturation adjustment: lerp between luminance (desaturated) and full color
		float3 saturated = lerp(blendedLuminance, blendedColor, Cinematic.x);

		// Brightness scaling and tint application
		float3 tinted = lerp(saturated, blendedLuminance * Tint.xyz, Tint.w);

		// Scale by brightness and apply contrast (pivot around scene average)
		float3 linearColor = Cinematic.w * tinted;
		linearColor = lerp(avgValue.x, linearColor, Cinematic.z);

		// Clamp to prevent negative values
		outputColor = max(0, linearColor);

#		if defined(FADE)
		// Screen fade (blending toward fade color)
		outputColor = lerp(outputColor, Fade.xyz, Fade.w);
#		endif
	}

	psout.Color = float4(outputColor, 1.0);

#	endif

	return psout;
}
#endif
