#include "Common/DummyVSTexCoord.hlsl"
#include "Common/FrameBuffer.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color : SV_Target0;
};

#if defined(PSHADER)
SamplerState ImageSampler : register(s0);
SamplerState DepthSampler : register(s1);

Texture2D<float4> ImageTex : register(t0);
Texture2D<float4> DepthTex : register(t1);

cbuffer PerGeometry : register(b2)
{
	float4 CameraParams : packoffset(c0);
	float4 DepthParams : packoffset(c1);
	float4 TexelSize : packoffset(c2);
};

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float2 adjustedTexCoord = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.TexCoord);

	float depth = DepthTex.Sample(DepthSampler, adjustedTexCoord).x;

	float depthFactor = DepthParams.w / ((1 - depth) * DepthParams.z + DepthParams.y);
	float offsetDelta = min(TexelSize.y, TexelSize.z * abs(depthFactor - TexelSize.x));
#	if defined(NO_SKY)
	if (1 - depth <= 1e-4) {
		offsetDelta = 0;
	}
#	endif
	if (depthFactor < TexelSize.x) {
		offsetDelta *= TexelSize.w;
	}
	float2 startOffset = input.TexCoord - 3 * (CameraParams.xy * offsetDelta);

	float4 color = 0;
	for (int i = 0; i < 7; ++i) {
		for (int j = 0; j < 7; ++j) {
			float2 currentTexCoord = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(
				startOffset + CameraParams.xy * offsetDelta * float2(i, j));
			float4 currentColor = ImageTex.Sample(ImageSampler, currentTexCoord);
			color += currentColor;
		}
	}

	psout.Color = 0.0204081628 * color;

	return psout;
}
#endif
