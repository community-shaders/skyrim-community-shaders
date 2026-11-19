#include "PostProcessing/ConvertColor.hlsli"
#include "PostProcessing/fullscreen.hlsli"

Texture2D<float4> texSrc : register(t0);
cbuffer CopyConstants : register(b1)
{
	uint inputGamut;
	uint outputGamut;
	float gamma;
	float scale;
};

float4 main(FullscreenTriangleVSOutput input) : SV_Target
{
	float4 color = texSrc.Load(int3(input.Position.xy, 0));
	return ConvertColor(color, inputGamut, outputGamut, gamma, scale);
}
