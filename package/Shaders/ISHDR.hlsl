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
 *      - HDR mode: DICE tonemapping preserves dynamic range above 1.0.
 *        Outputs gamma-encoded BT.709 values (can exceed 1.0) to float16 kFRAMEBUFFER.
 *   3. Post-tonemapping effects (TAA, ISDownsample, DOF) run on gamma-encoded output.
 *   4. HDROutputCS reads kFRAMEBUFFER for final processing:
 *      - SDR: Passthrough + UI composite
 *      - HDR: Gamma decode, BT.2020 conversion, nit scaling, PQ encoding + UI composite
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
	float4 Color: SV_Target0;
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

	// Force SDR tonemapping during loading screens and main menu
	if (SharedData::HDRData.w > 0.5)
		isHDR = false;

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
		// Output: Gamma-encoded BT.709 (values can exceed 1.0) for HDROutputCS to convert

		float paperWhiteNits = SharedData::HDRData.y;
		float peakNits = SharedData::HDRData.z;

		// === Convert to Linear Space ===
		// If Linear Lighting is active, input is already linear.
		float3 hdrLinear = ENABLE_LL ? inputColor : Color::GammaToLinear(inputColor);

		// === Color Grading (Mixed Linear/Gamma, matching vanilla order) ===
		// Vanilla order: Saturation → Tint → Brightness → Contrast
		// Physical accuracy: Saturation in linear; Tint & Contrast in gamma for artistic control.

		hdrLinear = Color::Saturation(hdrLinear, Cinematic.x);

		float3 hdrGamma = Color::LinearToGamma(hdrLinear);

		float hdrLuminanceGamma = Color::RGBToLuminance(hdrGamma);
		hdrGamma = lerp(hdrGamma, hdrLuminanceGamma * Tint.xyz, Tint.w);

		hdrLinear = Color::GammaToLinear(hdrGamma);
		hdrLinear *= Cinematic.w;

		// Power-curve contrast pivoted around photographic midgrey (0.18 linear).
		// Cinematic.z maps from [-1, 1] to a [0.5, 1.5] exponent: 1.0 = neutral,
		// >1.0 deepens blacks and lifts highlights without crushing toward a variable grey pivot.
		float contrastExp = Cinematic.z * 0.5 + 1.0;
		hdrLinear = 0.18 * pow(max(0, hdrLinear / 0.18), contrastExp);

#		if defined(FADE)
		hdrLinear = lerp(hdrLinear, Fade.xyz, Fade.w);
#		endif

		hdrLinear += saturate(Param.x - hdrLinear) * bloomColor;

		// DICE tonemapping: compresses highlights from paper-white to peak brightness.
		// Output remains in linear BT.709, values can exceed 1.0 up to peak/80.
		float pw = paperWhiteNits / sRGB_WhiteLevelNits;
		float peak = peakNits / sRGB_WhiteLevelNits;
		hdrLinear *= pw;

		// Shoulder anchored per Luma Framework DICE defaults.
		// ShoulderStart = 1/3 means compression starts at 33% of peak, giving highlights a gentle, perceptually-correct rolloff curve.
		float shoulderStart = 1.0 / 3.0;
		hdrLinear = DisplayMapping::DICETonemap(hdrLinear, peak, shoulderStart, CS_BT709, CS_BT709);

		// Output gamma-encoded BT.709 to kFRAMEBUFFER (float16).
		// BT.2020 conversion and PQ encoding happen in HDROutputCS after all post-processing.
		outputColor = Color::LinearToGamma(max(0.0, hdrLinear));
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
