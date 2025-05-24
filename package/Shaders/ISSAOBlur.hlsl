#include "Common/DummyVSTexCoord.hlsl"
#include "Common/FrameBuffer.hlsli"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color : SV_Target0;
};

#if defined(PSHADER)
SamplerState sourceSampler : register(s0);

Texture2D<float4> sourceTex : register(t0);

cbuffer PerGeometry : register(b2)
{
	float4 g_ScreenInfos : packoffset(c0);
};

float GetBlurFactor(float3 color)
{
	return dot(color.yz, float2(0.996108949, 0.00389105058));
}

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	const uint sampleCount = 7;
#	if defined(AXIS_H)
	const float2 texCoordOffsets[] = {
		float2(0, 0), float2(-6, -0), float2(-4, -0), float2(-2, -0), float2(2, 0), float2(4, 0), float2(6, 0)
	};
#	elif defined(AXIS_V)
	const float2 texCoordOffsets[] = {
		float2(0, 0), float2(-0, -6), float2(-0, -4), float2(-0, -2), float2(0, 2), float2(0, 4), float2(0, 6)
	};
#	endif
	float3 offsetColors[sampleCount];
	[unroll] for (uint sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
	{
		float2 offsetTexCoord = g_ScreenInfos.zw * texCoordOffsets[sampleIndex] + input.TexCoord;
		float2 adjustedOffsetTexCoord = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(offsetTexCoord);
		offsetColors[sampleIndex] = sourceTex.Sample(sourceSampler, adjustedOffsetTexCoord).xyz;
	}

	float3 centralColor = offsetColors[0];
	float centralBlurFactor = GetBlurFactor(centralColor);
	if (centralBlurFactor == 1.0) {
		psout.Color = float4(0, centralColor.yz, 0);
		return psout;
	}

	float weight = 0;
	float weightedComponent = 0;
	const float blurParameters[] = { 0.153170004, 0.392902017, 0.422649026, 0.444893003, 0.444893003, 0.422649026, 0.392902017 };
	[unroll] for (uint offsetIndex = 0; offsetIndex < sampleCount; ++offsetIndex)
	{
		float3 offsetColor = offsetColors[offsetIndex];
		float blurFactor = GetBlurFactor(offsetColor);
		float offsetWeight = blurParameters[offsetIndex] * max(0, 1 - 2000 * abs(blurFactor - centralBlurFactor));
		weightedComponent += offsetWeight * offsetColor.x;
		weight += offsetWeight;
	}

	float blurredComponent = weightedComponent / (1e-4 + weight);
#	if defined(AXIS_H)
	psout.Color = float4(blurredComponent, offsetColors[0].yz, 0);
#	elif defined(AXIS_V)
	psout.Color = blurredComponent;
#	endif

	return psout;
}
#endif
