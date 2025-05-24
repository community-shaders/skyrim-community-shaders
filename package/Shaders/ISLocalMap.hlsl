#include "Common/Color.hlsli"
#include "Common/DummyVSTexCoord.hlsl"

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
	float4 TexelSize : packoffset(c0);
};

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float3 weight = float3(0.5, 0.25, 1);
	float3 colorLR = 0;
	float3 colorBT = 0;
	[unroll] for (int j = -1; j <= 1; ++j)
	{
		[unroll] for (int i = -1; i <= 1; ++i)
		{
			if (i == 0 && j == 0) {
				continue;
			}

			float3 currentColor =
				ImageTex.Sample(ImageSampler, input.TexCoord + float2(i * TexelSize.x, j * TexelSize.y))
					.xyz;

			float centerMul = 1;
			if (i == 0 || j == 0) {
				centerMul = 2;
			}

			colorLR += -i * (centerMul * weight) * currentColor;
			colorBT += -j * (centerMul * weight) * currentColor;
		}
	}

	float4 colorCC = ImageTex.Sample(ImageSampler, input.TexCoord);
	float luminance = Color::RGBToLuminanceAlternative(colorCC.xyz);

	float alpha = (dot(4 * (pow(colorLR, 2) + pow(colorBT, 2)), 1.75) + luminance) * (1 - colorCC.w);
	float2 edgeFadeFactor = 1 - pow(2 * abs(input.TexCoord - 0.5), 5);

	psout.Color.xyz = 1.04 * luminance;
	psout.Color.w = alpha * edgeFadeFactor.x * edgeFadeFactor.y;

	return psout;
}
#endif
