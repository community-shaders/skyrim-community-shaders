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
static const float kBrightPassSoftKneeRatio = 0.5;

// Karis average: weight each pixel inversely by its luminance so isolated bright outliers
// (fireflies) are automatically suppressed relative to their local 2x2 neighborhood.
// Technique: Brian Karis, "Real Shading in Unreal Engine 4", SIGGRAPH 2013, p.10
// https://cdn2.unrealengine.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf
float3 KarisWeightedAverage(float3 a, float3 b, float3 c, float3 d)
{
	float wa = rcp(1.0 + Color::RGBToLuminance(a));
	float wb = rcp(1.0 + Color::RGBToLuminance(b));
	float wc = rcp(1.0 + Color::RGBToLuminance(c));
	float wd = rcp(1.0 + Color::RGBToLuminance(d));
	return (a * wa + b * wb + c * wc + d * wd) / (wa + wb + wc + wd);
}

// Sample a 2x2 neighborhood and Karis-average to suppress isolated HDR outliers.
float3 SampleKarisFireflySuppress(float2 uv, float2 texelSize)
{
	float3 s0 = ImageTex.SampleLevel(ImageSampler, uv + float2(-0.5, -0.5) * texelSize, 0).rgb;
	float3 s1 = ImageTex.SampleLevel(ImageSampler, uv + float2(0.5, -0.5) * texelSize, 0).rgb;
	float3 s2 = ImageTex.SampleLevel(ImageSampler, uv + float2(-0.5, 0.5) * texelSize, 0).rgb;
	float3 s3 = ImageTex.SampleLevel(ImageSampler, uv + float2(0.5, 0.5) * texelSize, 0).rgb;
	return KarisWeightedAverage(s0, s1, s2, s3);
}

float3 ApplyBrightPass(float3 hdrColor)
{
	float threshold = max(BlurBrightPass.x, 0.0);
	float scale = max(BlurBrightPass.y, 0.0);

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
	uint imgWidth, imgHeight;
	ImageTex.GetDimensions(imgWidth, imgHeight);
	float2 texelSize = rcp(float2(imgWidth, imgHeight));
#	endif

	for (int blurIndex = 0; blurIndex < blurRadius; ++blurIndex) {
		float2 screenPosition = BlurOffsets[blurIndex].xy + input.TexCoord.xy;
		float4 imageColor = 0;
#	if defined(BRIGHTPASS)
		{
			float2 sampleUV = (BlurScale.x < 0.5) ? FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(screenPosition) : screenPosition;
			imageColor.rgb = ApplyBrightPass(SampleKarisFireflySuppress(sampleUV, texelSize));
		}
#	else
		[branch] if (BlurScale.x < 0.5)
		{
			imageColor = GetImageColor(FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(screenPosition), blurScale.y);
		}
		else
		{
			imageColor = GetImageColor(screenPosition, blurScale.y);
		}
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
