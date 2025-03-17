#include "Common/Color.hlsli"
#include "Common/DummyVSTexCoord.hlsl"
#include "Common/FrameBuffer.hlsli"

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
	float4 Param : packoffset(c2);
	float4 Cinematic : packoffset(c3);
	float4 Tint : packoffset(c4);
	float4 Fade : packoffset(c5);
	float4 BlurScale : packoffset(c6);
	float4 BlurOffsets[16] : packoffset(c7);
};

float3 GetTonemapFactorReinhard(float3 luminance)
{
	return (luminance * (luminance * Param.y + 1)) / (luminance + 1);
}

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
		float3 imageColor = ImageTex.Sample(ImageSampler, texCoord).xyz;
#		if defined(RGB2LUM)
		imageColor = Color::RGBToLuminance(imageColor);
#		elif (defined(LUM) || defined(LUMCLAMP)) && !defined(DOWNADAPT)
		imageColor = imageColor.x;
#		endif
		downsampledColor += imageColor * BlurOffsets[sampleIndex].z;
	}
#		if defined(DOWNADAPT)
	float2 adaptValue = AdaptTex.Sample(AdaptSampler, input.TexCoord).xy;
	if (isnan(downsampledColor.x) || isnan(downsampledColor.y) || isnan(downsampledColor.z)) {
		downsampledColor.xy = adaptValue;
	} else {
		float2 adaptDelta = downsampledColor.xy - adaptValue;
		downsampledColor.xy =
			sign(adaptDelta) * clamp(abs(Param.wz * adaptDelta), 0.00390625, abs(adaptDelta)) +
			adaptValue;
	}
	downsampledColor = max(asfloat(0x00800000), downsampledColor);  // Black screen fix
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

	// Vanilla tonemapping and post-processing
	float3 gameSdrColor = 0.0;
	float3 ppColor = 0.0;
	{
		inputColor *= avgValue.y / avgValue.x;
		inputColor = max(0, inputColor);

		float3 blendedColor;
		[branch] if (Param.z > 0.5)
		{
			blendedColor = DisplayMapping::HuePreservingHejlBurgessDawson(inputColor, bloomColor);
		}
		else
		{
			float maxCol = Color::RGBToLuminance(inputColor);
			float mappedMax = GetTonemapFactorReinhard(maxCol);
			float3 compressedHuePreserving = inputColor * mappedMax / maxCol;
			blendedColor = compressedHuePreserving;
			blendedColor += saturate(Param.x - blendedColor) * bloomColor;
		}

		gameSdrColor = blendedColor;

		float blendedLuminance = Color::RGBToLuminance(blendedColor);

		float3 linearColor = Cinematic.w * lerp(lerp(blendedLuminance, blendedColor, Cinematic.x), blendedLuminance * Tint, Tint.w).xyz;

		linearColor = lerp(avgValue.x, linearColor, Cinematic.z);

		ppColor = max(0, linearColor);
	}

	float3 srgbColor = ppColor;

#		if defined(FADE)
	srgbColor = lerp(srgbColor, Fade.xyz, Fade.w);
#		endif

	srgbColor = FrameBuffer::ToSRGBColor(srgbColor);

	psout.Color = float4(srgbColor, 1.0);

#	endif

	return psout;
}
#endif
