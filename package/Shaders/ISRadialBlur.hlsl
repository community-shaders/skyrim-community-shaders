#include "Common/DummyVSTexCoord.hlsl"
#include "Common/FrameBuffer.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color : SV_Target0;
};

#if defined(PSHADER)
SamplerState ImageSampler : register(s0);

Texture2D<float4> ImageTex : register(t0);

cbuffer PerGeometry : register(b2)
{
	float4 Params : packoffset(c0);
	float4 Center : packoffset(c1);
};

float GetCircleParam(float centerDistance, float param1, float param2)
{
	float circleDistance = max(0, centerDistance - param1);
	float result = 0;
	if (circleDistance > 0) {
		result = Params.x * (1 - 1 / (param2 * circleDistance + 1));
	}
	return result;
}

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float2 offset = Center.xy - input.TexCoord;
	float centerDistance = length(offset);
	float2 sampleDelta =
		0.5 * (normalize(offset) * max(0, GetCircleParam(centerDistance, Params.z, Params.y) -
											  GetCircleParam(centerDistance, Center.z, Params.w)));

	float4 color = 0;
	for (float sampleIndex = -NUM_STEPS; sampleIndex <= NUM_STEPS; ++sampleIndex) {
		float2 texCoord = input.TexCoord + sampleDelta * sampleIndex;
		float2 adjustedTexCoord = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(texCoord);
		float4 currentColor = ImageTex.SampleLevel(ImageSampler, adjustedTexCoord, 0);
		color += currentColor;
	}
	color *= (1. / (2. * NUM_STEPS + 1.));

	psout.Color = color;

	return psout;
}
#endif
