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
		imageColor = BlurBrightPass.y * max(0, -BlurBrightPass.x + imageColor);
#	endif
		color += imageColor * BlurOffsets[blurIndex].z;
	}

#	if defined(BRIGHTPASS)
	float avgLum = Color::RGBToLuminance(AvgLumTex.Sample(AvgLumSampler, input.TexCoord.xy).xyz);
	color.w = avgLum;
#	endif

	psout.Color = color * float4(blurScale.xxx, 1);

	return psout;
}
#endif
