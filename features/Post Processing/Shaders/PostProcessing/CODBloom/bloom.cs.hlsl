/// By ProfJack/五脚猫, 2024-2-28 UTC
/// ref:
/// http://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare

#include "PostProcessing/common.hlsli"

Texture2D<float4> TexColor : register(t0);
Texture2D<float4> TexBloomIn : register(t1);

RWTexture2D<float4> RWTexBloomOut : register(u0);

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

[numthreads(32, 32, 1)] void CS_Threshold(uint2 tid : SV_DispatchThreadID) {
	float3 col_input = TexColor[tid].rgb;

	float3 col = col_input;
	col = Sanitise(col);
	col = ThresholdColor(col, Threshold.x);
	RWTexBloomOut[tid] = float4(col, 1);
};

[numthreads(32, 32, 1)] void CS_Downsample(uint2 tid : SV_DispatchThreadID) {
	uint2 dims;
	RWTexBloomOut.GetDimensions(dims.x, dims.y);

	float2 px_size = rcp(dims);
	float2 uv = (tid + .5) * px_size;

#ifdef FIRST_MIP
	float3 col = DownsampleCODFirstMip(TexBloomIn, SampColor, uv, px_size).rgb;
#else
	float3 col = DownsampleCOD(TexBloomIn, SampColor, uv, px_size).rgb;
#endif
	RWTexBloomOut[tid] = float4(col, 1);
};

[numthreads(32, 32, 1)] void CS_Upsample(uint2 tid : SV_DispatchThreadID) {
	uint2 dims;
	RWTexBloomOut.GetDimensions(dims.x, dims.y);

	float2 px_size = rcp(dims);
	float2 uv = (tid + .5) * px_size;

	float3 col = RWTexBloomOut[tid].rgb * CurrentMipMult + UpsampleCOD(TexBloomIn, uv, px_size * UpsampleRadius).rgb * UpsampleMult;
	RWTexBloomOut[tid] = float4(col, 1);
};

[numthreads(32, 32, 1)] void CS_Composite(uint2 tid : SV_DispatchThreadID) {
	uint2 dims;
	RWTexBloomOut.GetDimensions(dims.x, dims.y);

	float2 px_size = rcp(dims);
	float2 uv = (tid + .5) * px_size;

	float3 col = TexColor[tid].rgb + UpsampleCOD(TexBloomIn, uv, px_size * UpsampleRadius).rgb * UpsampleMult;

	RWTexBloomOut[tid] = float4(col, 1);
};