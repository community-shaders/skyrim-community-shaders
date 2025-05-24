#include "Common/DummyVSTexCoord.hlsl"
#include "Common/FrameBuffer.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color : SV_Target0;
};

#if defined(PSHADER)
SamplerState DownScaledBlurredHDRSampler : register(s0);
SamplerState RampsSampler : register(s1);
SamplerState MasksSampler : register(s2);

Texture2D<float4> DownScaledBlurredHDRTex : register(t0);
Texture2D<float4> RampsTex : register(t1);
Texture2D<float4> MasksTex : register(t2);

cbuffer PerGeometry : register(b2)
{
	float lensCount : packoffset(c0.x);
	float flareDispersal : packoffset(c0.y);
	float lightsRangeDownshift : packoffset(c0.z);
	float invLightsRangeDownshift : packoffset(c0.w);
	float lateralRepeat : packoffset(c1.x);
	float channelsDistortionRed : packoffset(c1.y);
	float channelsDistortionGreen : packoffset(c1.z);
	float channelsDistortionBlue : packoffset(c1.w);
	float texelOffsetX : packoffset(c2.x);
	float texelOffsetY : packoffset(c2.y);
	float haloFetch : packoffset(c2.z);
	float haloWidthPow : packoffset(c2.w);
	float dynamicSource : packoffset(c3.x);
	float globalIntensity : packoffset(c4.x);
};

float4 GetSampleColor(float2 texCoord)
{
	float4 color = DownScaledBlurredHDRTex.Sample(DownScaledBlurredHDRSampler, texCoord);
	if (saturate(color.x - lightsRangeDownshift) + saturate(color.y - lightsRangeDownshift) +
			saturate(color.z - lightsRangeDownshift) <=
		0) {
		return 0;
	}
	return saturate(color);
}

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float4 lensColor = 0;
	for (int sampleIndex = -asint(lateralRepeat); sampleIndex <= asint(lateralRepeat); ++sampleIndex) {
		float4 sampleColor;
		if (dynamicSource > 0.5) {
			sampleColor = GetSampleColor(FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(
				input.TexCoord + float2(texelOffsetX * sampleIndex, 0)));
		} else {
			sampleColor = GetSampleColor(input.TexCoord + float2(texelOffsetX * sampleIndex, 0));
		}
		lensColor += sampleColor;
	}
	psout.Color = globalIntensity * lensColor;

	return psout;
}
#endif
