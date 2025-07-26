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

#	if defined(DOWNSAMPLE) && !defined(DOWNADAPT)

	float3 downsampledColor = 0.0;

	[loop]
	for (int sampleIndex = 0; sampleIndex < DOWNSAMPLE; ++sampleIndex)
	{
		float2 texCoord = BlurOffsets[sampleIndex].xy * BlurScale.xy + input.TexCoord;

		[branch]
		if (Flags.x > 0.5)
		{
			texCoord = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(texCoord);
		}

		float3 imageColor = ImageTex.Sample(ImageSampler, texCoord).xyz;

#if defined(RGB2LUM)
		imageColor = max(imageColor.x, max(imageColor.y, imageColor.z));
#elif (defined(LUM) || defined(LUMCLAMP)) && !defined(DOWNADAPT)
		imageColor = imageColor.x;
#endif

		downsampledColor += imageColor * BlurOffsets[sampleIndex].z;
	}

	psout.Color = float4(downsampledColor, BlurScale.z);

#elif defined(DOWNADAPT)

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
// ENBSeries Fallout 4 adaptation file, hlsl DX11                   //
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
//  Histogram based adaptation by kingeric1992                      //
//      based on the description here:                              //
//  https://docs.unrealengine.com/latest/INT/Engine/Rendering/PostProcessEffects/AutomaticExposure/
//                                                                  //
//  For more info, visit                                            //
//     http://enbseries.enbdev.com/forum/viewtopic.php?f=7&t=5321   //
//                                                                  //
//  update: Nov.23.2016                                             //
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//

    float4 coord = { 1.0 / 32.0, 1.0 / 32.0, 1.0 / 32.0, 1.0 / 16.0};
    float4 bin[16];

    for(int k=0; k<16; k++)
    {
        bin[k]=float4(0.0, 0.0, 0.0, 0.0);
    }

    [loop]
    for(int i=0; i < 16.0; i++)
    {
        coord.y  = coord.z;
        [loop]
        for(int j=0; j<16.0; j++)
        {
			float2 uv = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(coord.xy);
            float  color = log2(ImageTex.SampleLevel(ImageSampler, uv.xy, 0.0).r);
            float level = saturate(( color + 5.0 ) / 7) * 63; // [-5, 2] - Tweaked for SSE by ez009 @ http://enbseries.enbdev.com/forum/viewtopic.php?p=90553#p90553
            bin[ level * 0.25 ] += float4(0.0, 1.0, 2.0, 3.0) == float4(trunc(level % 4).xxxx); //bitwise ?
            coord.y  += coord.w;
        }
        coord.x += coord.w;
    }

	float LowPercent = 0.6;
	float HighPercent = 0.9;

    float2 adaptAnchor = 0.5; //.x = high, .y = low
    float2 accumulate  = float2( HighPercent - 1.0, LowPercent - 1.0) * 256.0;

    [loop]
    for(int l=15; l>0; l--)
    {
        accumulate += bin[l].w;
        adaptAnchor = (accumulate.xy < bin[l].ww)? l * 4.0 + accumulate.xy / bin[l].ww + 3.0: adaptAnchor;

        accumulate += bin[l].z;
        adaptAnchor = (accumulate.xy < bin[l].zz)? l * 4.0 + accumulate.xy / bin[l].zz + 2.0: adaptAnchor;

        accumulate += bin[l].y;
        adaptAnchor = (accumulate.xy < bin[l].yy)? l * 4.0 + accumulate.xy / bin[l].yy + 1.0: adaptAnchor;

        accumulate += bin[l].x;
        adaptAnchor = (accumulate.xy < bin[l].xx)? l * 4.0 + accumulate.xy / bin[l].xx + 0.0: adaptAnchor;
    }

	float Bias = -2.0;
	float MaxBrightness = 2.0;
	float MinBrightness = -4.0;

    float adapt = (adaptAnchor.x + adaptAnchor.y) * 0.5 / 63.0 * 7.0 - 5.0; // - Tweaked for SSE by ez009 @ http://enbseries.enbdev.com/forum/viewtopic.php?p=90553#p90553
          adapt =  pow(2.0, clamp( adapt, MinBrightness, MaxBrightness) + Bias);  // min max on log2 scale

	psout.Color = lerp(AdaptTex.Sample(ImageSampler, 0.5).x, adapt, 0.1 * 0.001 * SharedData::Timer);

#	elif defined(BLEND)
	float2 uv = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);

	float3 inputColor = BlendTex.Sample(BlendSampler, uv).xyz;

	float3 bloomColor = 0;
	if (Flags.x > 0.5) {
		bloomColor = ImageTex.Sample(ImageSampler, uv).xyz;
	} else {
		bloomColor = ImageTex.Sample(ImageSampler, input.TexCoord.xy).xyz;
	}

	float adaptation = AvgTex.Sample(AvgSampler, uv).x;

	// Vanilla tonemapping and post-processing
	float3 gameSdrColor = 0.0;
	float3 ppColor = 0.0;
	{
		adaptation = adaptation * 0.20 + 0.02;

		inputColor = 0.04 * inputColor / adaptation;
		bloomColor = 0.04 * bloomColor / adaptation;

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

		gameSdrColor = blendedColor;

		float blendedLuminance = Color::RGBToLuminance(blendedColor);

		float3 linearColor = Cinematic.w * lerp(lerp(blendedLuminance, blendedColor, Cinematic.x), blendedLuminance * Tint.xyz, Tint.w).xyz;

		linearColor = lerp(adaptation, linearColor, Cinematic.z);

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
