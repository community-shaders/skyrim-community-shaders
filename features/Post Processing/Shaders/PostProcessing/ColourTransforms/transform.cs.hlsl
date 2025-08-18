/// By ProfJack/五脚猫, 2024-2-17 UTC

#include "PostProcessing/common.hlsli"
#include "PostProcessing/ColourTransforms/common.hlsli"
#include "PostProcessing/ColourTransforms/GT7ToneMapping.hlsli"

#define PI 3.1415926535

RWTexture2D<float4> RWTexOut : register(u0);

Texture2D<float4> TexColor : register(t0);

cbuffer TonemapCB : register(b1)
{
	float4 Params[8];
};

[numthreads(8, 8, 1)] void main(uint2 tid
								: SV_DispatchThreadID) {
	float3 color = TexColor[tid].rgb;

	color = TRANSFORM_FUNC(color);

	RWTexOut[tid] = float4(color, 1);
}