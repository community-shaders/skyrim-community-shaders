#include "Common/Color.hlsli"
#include "Common/DummyVSTexCoord.hlsl"
#include "Common/FrameBuffer.hlsli"
#include "Common/Math.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color: SV_Target0;
};

#if defined(PSHADER)
SamplerState ImageSampler : register(s0);
SamplerState AvgLumSampler : register(s1);

Texture2D<float4> ImageTex : register(t0);
Texture2D<float4> AvgLumTex : register(t1);

cbuffer PerGeometry : register(b2)
{
	float4 BlurBrightPass : packoffset(c0);
	float4 BlurScale : packoffset(c1);
	float BlurRadius : packoffset(c2);
	float4 BlurOffsets[16] : packoffset(c3);
};

float4 GetImageColor(float2 texCoord, float blurScale)
{
	return ImageTex.Sample(ImageSampler, texCoord) * float4(blurScale.xxx, 1);
}

#	if defined(BRIGHTPASS)
static const float kMinFireflyLuminanceLimit = 4.0;
static const float kFireflyAvgLuminanceMultiplier = 12.0;
static const float kBrightPassSoftKneeRatio = 0.5;

float3 ApplyBrightPassPrefilter(float3 hdrColor, float avgLum)
{
	float threshold = max(BlurBrightPass.x, 0.0);
	float scale = max(BlurBrightPass.y, 0.0);

	// Clamp isolated HDR outliers before thresholding to suppress firefly-induced bloom flashes.
	float luminance = max(Color::RGBToLuminance(hdrColor), EPSILON_DIVISION);
	float luminanceLimit = max(kMinFireflyLuminanceLimit, avgLum * kFireflyAvgLuminanceMultiplier);
	hdrColor *= min(1.0, luminanceLimit / luminance);

	// Soft-knee threshold avoids binary bloom popping when highlights hover near threshold.
	float knee = max(threshold * kBrightPassSoftKneeRatio, EPSILON_DIVISION);
	float brightness = max(Color::RGBToLuminance(hdrColor), EPSILON_DIVISION);
	float soft = saturate((brightness - threshold + knee) / max(2.0 * knee, EPSILON_DIVISION));
	soft = soft * soft * (3.0 - 2.0 * soft);
	float contribution = max(brightness - threshold, 0.0) + soft * knee;
	float weight = contribution / brightness;

	return hdrColor * weight * scale;
}
#	endif

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float4 color = 0;

#	if defined(TEXTAP)
	int blurRadius = TEXTAP;
#	else
	uint blurRadius = asuint(BlurRadius);
#	endif
	float2 blurScale = BlurScale.zw;
#	if !defined(TEXTAP) || !defined(COLORRANGE)
	blurScale = 1;
#	endif

#	if defined(BRIGHTPASS)
	float avgLum = Color::RGBToLuminance(AvgLumTex.Sample(AvgLumSampler, input.TexCoord.xy).xyz);
#	endif

	for (int blurIndex = 0; blurIndex < blurRadius; ++blurIndex) {
		float2 screenPosition = BlurOffsets[blurIndex].xy + input.TexCoord.xy;
		float4 imageColor = 0;
		[branch] if (BlurScale.x < 0.5)
		{
			imageColor = GetImageColor(FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(screenPosition), blurScale.y);
		}
		else
		{
			imageColor = GetImageColor(screenPosition, blurScale.y);
		}
#	if defined(BRIGHTPASS)
		imageColor.rgb = ApplyBrightPassPrefilter(imageColor.rgb, avgLum);
#	endif
		color += imageColor * BlurOffsets[blurIndex].z;
	}

#	if defined(BRIGHTPASS)
	color.w = avgLum;
#	endif

	psout.Color = color * float4(blurScale.xxx, 1);

	return psout;
}
#endif
