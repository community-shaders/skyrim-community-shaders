/**
 * @file ISHDR.hlsl
 * @brief Imagespace tonemap (DOWNSAMPLE / BLEND). HDR BLEND: SDR tonemap + DICE, output gamma BT.709; user paper white applied in HDROutputCS.
 *
 * @see HDROutputCS.hlsl
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
	float3 downsampledColor = 0;
	for (int sampleIndex = 0; sampleIndex < DOWNSAMPLE; ++sampleIndex) {
		float2 texCoord = BlurOffsets[sampleIndex].xy * BlurScale.xy + input.TexCoord;

		[branch] if (Flags.x > 0.5)
		{
			texCoord = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(texCoord);
		}

		float3 imageColor = max(0.0, ImageTex.Sample(ImageSampler, texCoord).xyz);

#		if defined(RGB2LUM)
		imageColor = Color::RGBToLuminance(imageColor);
#		elif (defined(LUM) || defined(LUMCLAMP)) && !defined(DOWNADAPT)
		imageColor = imageColor.x;
#		endif
		downsampledColor += imageColor * BlurOffsets[sampleIndex].z;
	}

#		if defined(DOWNADAPT)
	float2 adaptValue = max(0.001, AdaptTex.Sample(AdaptSampler, input.TexCoord).xy);
	float2 adaptDelta = downsampledColor.xy - adaptValue;
	downsampledColor.xy =
		sign(adaptDelta) * clamp(abs(Param.wz * adaptDelta), 0.00390625, abs(adaptDelta)) +
		adaptValue;
#		endif
	psout.Color = float4(downsampledColor, BlurScale.z);

#	elif defined(BLEND)
	float2 uv = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);

	float3 inputColor = BlendTex.Sample(BlendSampler, uv).xyz;

	float3 bloomColor = 0;
	if (Flags.x > 0.5) {
		bloomColor = ImageTex.Sample(ImageSampler, uv).xyz;
	} else {
		bloomColor = ImageTex.Sample(ImageSampler, input.TexCoord.xy).xyz;
	}

	float2 avgValue = AvgTex.Sample(AvgSampler, input.TexCoord.xy).xy;

	bool isHDR = SharedData::HDRData.x > 0.5;

	if (SharedData::HDRData.w > 0.5)
		isHDR = false;

	float3 outputColor = 0.0;

	if (avgValue.x != 0 && avgValue.y != 0)
		inputColor *= avgValue.y / avgValue.x;
	inputColor = max(0, inputColor);

	if (isHDR) {
		// pw = 203 nits here; user paper white in HDROutputCS.
		static const float HDR_TONEMAP_REF_WHITE_NITS = 203.0;
		float peakNits = SharedData::HDRData.z;
		float pw = HDR_TONEMAP_REF_WHITE_NITS / sRGB_WhiteLevelNits;
		float peak = peakNits / sRGB_WhiteLevelNits;

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

			sdrTonemapped += saturate(Param.x - sdrTonemapped) * bloomColor;
		}

		float sdrLuminance = Color::RGBToLuminance(sdrTonemapped);
		float3 sdrGraded = Cinematic.w * lerp(lerp(sdrLuminance, sdrTonemapped, Cinematic.x), sdrLuminance * Tint.xyz, Tint.w).xyz;
		sdrGraded = lerp(avgValue.x, sdrGraded, Cinematic.z);
		sdrGraded = max(0.0, sdrGraded);

#		if defined(FADE)
		sdrGraded = lerp(sdrGraded, Fade.xyz, Fade.w);
#		endif

		float3 sdrLinear = Color::SkyrimGammaToLinear(max(0.0, sdrGraded));
		float3 sdrBase = sdrLinear * pw;

		float shoulderStart = pw / peak;
		float3 hdrInputLinear = ENABLE_LL ? inputColor : Color::SkyrimGammaToLinear(inputColor);
		float3 bloomLinear = ENABLE_LL ? bloomColor : Color::SkyrimGammaToLinear(bloomColor);
		float3 hdrScene = (hdrInputLinear + bloomLinear) * pw;
		float3 diceLinear = DisplayMapping::DICETonemap(hdrScene, peak, shoulderStart, CS_BT709, CS_BT709);

		float diceBlend = saturate(Color::RGBToLuminance(hdrInputLinear * pw) / max(peak, 1e-6));
		float3 hdrLinearOut = lerp(sdrBase, diceLinear, diceBlend);

		outputColor = Color::LinearToSkyrimGamma(max(0.0, hdrLinearOut));
	} else {
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