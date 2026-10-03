/// By ProfJack/五脚猫, 2024-2-28 UTC
/// ref:
/// http://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare
///
/// Fullscreen-triangle bloom chain. Each mip pass renders into its target mip
/// with a viewport sized to that mip. The upsample accumulation is done by
/// fixed-function blending: the PS emits the upsample contribution scaled by
/// UpsampleMult and the blend stage adds it to the destination mip
/// pre-multiplied by CurrentMipMult (SrcBlend = ONE, DestBlend = BLEND_FACTOR,
/// blend factor = CurrentMipMult).

#include "PostProcessing/fullscreen.hlsli"

#include "PostProcessing/common.hlsli"

Texture2D<float4> TexColor : register(t0);
Texture2D<float4> TexBloomIn : register(t1);

cbuffer BloomCB : register(b1)
{
	// threshold
	float Threshold : packoffset(c0.x);
	// upsample
	float UpsampleRadius : packoffset(c0.y);
	float UpsampleMult : packoffset(c0.z);  // in composite: bloom mult
	float CurrentMipMult : packoffset(c0.w);
};

SamplerState SampColor : register(s0);

bool3 IsNaN(float3 x)
{
	return !(x < 0.f || x > 0.f || x == 0.f);
}

float3 Sanitise(float3 v)
{
	bool3 err = IsNaN(v) || (v < 0);
	v.x = err.x ? 0 : v.x;
	v.y = err.y ? 0 : v.y;
	v.z = err.z ? 0 : v.z;
	return v;
}

float3 ThresholdColor(float3 col, float threshold)
{
	float luma = Color::RGBToLuminance(col);
	if (luma < 1e-3)
		return 0;
	return col * (max(0, luma - threshold) / luma);
}

float4 UpsampleCOD(Texture2D tex, float2 uv, float2 radius)
{
	float4 retval = 0;
	for (int x = -1; x <= 1; ++x)
		for (int y = -1; y <= 1; ++y)
			retval += (1 << (!x + !y)) * 0.0625 * tex.SampleLevel(SampColor, uv + float2(x, y) * radius, 0);
	return retval;
}

float4 PS_Threshold(FullscreenTriangleVSOutput input) : SV_Target
{
	uint2 tid = uint2(input.Position.xy);

	float3 col_input = TexColor[tid].rgb;

	float3 col = col_input;
	col = Sanitise(col);
	col = ThresholdColor(col, Threshold.x);
	return float4(col, 1);
};

float4 PS_Downsample(FullscreenTriangleVSOutput input) : SV_Target
{
	float2 px_size = fwidth(input.TexCoord);
	float2 uv = input.TexCoord;

#ifdef FIRST_MIP
	float3 col = DownsampleCODFirstMip(TexBloomIn, SampColor, uv, px_size).rgb;
#else
	float3 col = DownsampleCOD(TexBloomIn, SampColor, uv, px_size).rgb;
#endif
	return float4(col, 1);
};

/// Upsample-accumulate pass:
///     out = dst * CurrentMipMult + UpsampleCOD(TexBloomIn) * UpsampleMult
/// The PS returns the scaled upsample contribution; the destination multiply
/// happens in the blend stage (see file header).
float4 PS_Upsample(FullscreenTriangleVSOutput input) : SV_Target
{
	float2 px_size = fwidth(input.TexCoord);
	float2 uv = input.TexCoord;

	float3 col = UpsampleCOD(TexBloomIn, uv, px_size * UpsampleRadius).rgb * UpsampleMult;
	return float4(col, 1);
};

float4 PS_Composite(FullscreenTriangleVSOutput input) : SV_Target
{
	float2 px_size = fwidth(input.TexCoord);
	float2 uv = input.TexCoord;

	float3 col = TexColor[input.Position.xy].rgb + UpsampleCOD(TexBloomIn, uv, px_size * UpsampleRadius).rgb * UpsampleMult;

	return float4(col, 1);
};
