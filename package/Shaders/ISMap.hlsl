#include "Common/DummyVSTexCoord.hlsl"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color: SV_Target0;
};

#if defined(PSHADER)
SamplerState Src0Sampler : register(s0);

Texture2D<float4> Src0Tex : register(t0);

cbuffer PerGeometry : register(b2)
{
	float4 CameraPos : packoffset(c0);
};

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float4 colorLR = 0;
	float4 colorBT = 0;
	[unroll] for (int j = -1; j <= 1; ++j)
	{
		[unroll] for (int i = -1; i <= 1; ++i)
		{
			if (i == 0 && j == 0) {
				continue;
			}

			float4 currentColor =
				Src0Tex
					.Sample(Src0Sampler, input.TexCoord + float2(i * CameraPos.x, j * CameraPos.y));

			float centerMul = 1;
			if (i == 0 || j == 0) {
				centerMul = 2;
			}

			colorLR += -i * centerMul * currentColor;
			colorBT += -j * centerMul * currentColor;
		}
	}
	float4 convolved = pow(colorLR, 2) + pow(colorBT, 2);

	float3 mapColor = min(0.275, dot(float3(0.2, 0.2, 0.15), convolved.xyz) + min(0.1, 10 * convolved.w).xxx);
	psout.Color.xyz = mapColor;
	psout.Color.w = 2 * dot(1, mapColor.zzz);

	return psout;
}
#endif
