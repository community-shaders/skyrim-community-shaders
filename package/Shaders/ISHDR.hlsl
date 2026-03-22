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
		// === HDR Pipeline ===
		// Run the exact SDR tonemap + bloom + color grading pipeline to produce the SDR
		// reference image, then extend highlights above paperwhite using DICE.
		// This guarantees HDR and SDR are identical up to paperwhite nits.

		float paperWhiteNits = SharedData::HDRData.y;
		float peakNits = SharedData::HDRData.z;
		float pw = paperWhiteNits / sRGB_WhiteLevelNits;
		float peak = peakNits / sRGB_WhiteLevelNits;
		float3 hdrInputLinear = ENABLE_LL ? inputColor : Color::SkyrimGammaToLinear(inputColor);
		float3 hdrScene = hdrInputLinear * pw;

		// --- Step 1: Identical SDR tonemap + bloom ---
		float3 sdrTonemapped;

		[branch] if (Param.z > 0.5)
		{
			sdrTonemapped = DisplayMapping::HuePreservingHejlBurgessDawson(inputColor, bloomColor);
		}
		else
		{
			float maxCol = Color::RGBToLuminance(inputColor);
			float mappedMax = GetTonemapFactorReinhard(maxCol).x;
			float3 compressedHuePreserving = inputColor * mappedMax / maxCol;
			sdrTonemapped = compressedHuePreserving;

			// Standard SDR bloom: add bloom where there is headroom below the ceiling.
			sdrTonemapped += saturate(Param.x - sdrTonemapped) * bloomColor;
		}

		// --- Step 2: Identical SDR color grading ---
		float sdrLuminance = Color::RGBToLuminance(sdrTonemapped);
		float3 sdrGraded = Cinematic.w * lerp(lerp(sdrLuminance, sdrTonemapped, Cinematic.x), sdrLuminance * Tint.xyz, Tint.w).xyz;
		sdrGraded = lerp(avgValue.x, sdrGraded, Cinematic.z);
		sdrGraded = max(0.0, sdrGraded);

#		if defined(FADE)
		sdrGraded = lerp(sdrGraded, Fade.xyz, Fade.w);
#		endif

		// Secondary HDR tonemap path used for highlight shaping.
		float3 hdrShaped = DisplayMapping::DICETonemap(hdrScene, peak, /*ShoulderStart=*/pw);

		// Bloom
		hdrShaped += saturate(Param.x - hdrShaped) * bloomColor;

		// Desaturate toward luminance as brightness approaches peak.
		// 0 at paperwhite, 1 at peak.
		float hdrShapedLum = Color::RGBToLuminance(hdrShaped);
		float desatStart = lerp(pw, peak, 0.4);
		float desatT = saturate((hdrShapedLum - desatStart) / max(peak - desatStart, 1e-6));
		hdrShaped = lerp(hdrShaped, hdrShapedLum, desatT);

		// sdrGraded is now the exact SDR output (before final gamma encode).
		// Convert to linear and scale to paperwhite for the HDR base layer.
		float3 sdrLinear = Color::SkyrimGammaToLinear(max(0.0, sdrGraded));
		float3 sdrBase = sdrLinear * pw;

		// DICE compresses the full HDR range above the shoulder start into peak nits.
		float shoulderStart = pw / peak;
		float3 diceLinear = DisplayMapping::DICETonemap(hdrScene, peak, shoulderStart);

		// DICE only extends highlights at and above paperwhite.
		float rawLum = Color::RGBToLuminance(hdrScene) / peak;
		float diceBlend = saturate((rawLum - shoulderStart) / max(1.0 - shoulderStart, 1e-4));
		float3 hdrLinearOut = lerp(sdrBase, diceLinear, diceBlend);
		hdrLinearOut = lerp(hdrLinearOut, hdrShaped, 0.35 * diceBlend);

		// Cheap fire hue fix: when bright highlights are warm (R/G dominant over B),
		// push them slightly toward orange/yellow to avoid pink cores.
		float peakLum = Color::RGBToLuminance(hdrLinearOut);
		float fireHighlight = smoothstep(pw * 0.75, pw * 4.0, peakLum);
		float warmDominance = saturate((hdrLinearOut.r - hdrLinearOut.b) * 1.25) *
		                      saturate((hdrLinearOut.g - hdrLinearOut.b) * 2.0);
		float fireHueMask = fireHighlight * warmDominance;
		hdrLinearOut.b *= 1.0 - 0.30 * fireHueMask;
		hdrLinearOut.g += hdrLinearOut.r * (0.05 * fireHueMask);
		hdrLinearOut *= 10.0 + (0.18 * fireHueMask * diceBlend);

		outputColor = Color::LinearToSkyrimGamma(max(0.0, hdrLinearOut));
	} else {
		// === SDR Pipeline (LDR RT) ===
		// Input: Linear HDR values (can exceed 1.0)
		// Output: Tonemapped [0,1] gamma sRGB for traditional displays

		float3 blendedColor;

		[branch] if (Param.z > 0.5)
		{
			blendedColor = DisplayMapping::HuePreservingHejlBurgessDawson(inputColor, bloomColor);
		}
		else
		{
			float maxCol = Color::RGBToLuminance(inputColor);
			float mappedMax = GetTonemapFactorReinhard(maxCol).x;
			float3 compressedHuePreserving = inputColor * mappedMax / maxCol;
			blendedColor = compressedHuePreserving;
			blendedColor += saturate(Param.x - blendedColor) * bloomColor;
		}

		// === Color Grading (Post-Tonemap) ===
		// Apply saturation, contrast, and tint to the tonemapped result.

		float blendedLuminance = Color::RGBToLuminance(blendedColor);
		float3 linearColor = Cinematic.w * lerp(lerp(blendedLuminance, blendedColor, Cinematic.x), blendedLuminance * Tint.xyz, Tint.w).xyz;
		linearColor = lerp(avgValue.x, linearColor, Cinematic.z);
		outputColor = max(0, linearColor);

#		if defined(FADE)
		outputColor = lerp(outputColor, Fade.xyz, Fade.w);
#		endif

		if (SharedData::linearLightingSettings.enableLinearLighting && SharedData::linearLightingSettings.enableGammaCorrection) {
			outputColor = Color::LinearToSrgb(outputColor);
		}
		outputColor = FrameBuffer::ToSRGBColor(outputColor);
	}

	psout.Color = float4(outputColor, 1.0);

#	endif

	return psout;
}
#endif
