struct VS_INPUT
{
	float4 Position: POSITION0;
	float2 TexCoord: TEXCOORD0;
};

struct VS_OUTPUT
{
	float4 Position: POSITION0;
	float2 TexCoord: TEXCOORD0;
};

#ifdef VSHADER
cbuffer PerGeometry : register(b2)
{
	float4 GeometryOffset : packoffset(c0);
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;

	vsout.Position.xy = input.Position.xy - GeometryOffset.xy * float2(2, -2);
	vsout.Position.zw = input.Position.zw;
	vsout.TexCoord = input.TexCoord.xy;

	return vsout;
}
#endif

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color: SV_Target0;
};

#if defined(PSHADER)
SamplerState NoiseMapSampler : register(s0);

Texture2D<float4> NoiseMapTex : register(t0);

cbuffer PerGeometry : register(b2)
{
	float2 fTexScroll0 : packoffset(c0.x);
	float2 fTexScroll1 : packoffset(c0.z);
	float2 fTexScroll2 : packoffset(c1.x);
	float2 fNoiseScale : packoffset(c1.z);
	float3 fTexScale : packoffset(c2);
	float3 fAmplitude : packoffset(c3);
};

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

#	if defined(NORMALMAP)

	float offset = 0.00390625;
	float valueRL = 0;
	float valueTB = 0;
	[unroll] for (int i = -1; i <= 1; ++i)
	{
		[unroll] for (int j = -1; j <= 1; ++j)
		{
			if (i == 0 && j == 0) {
				continue;
			}

			float currentValue = abs(
				NoiseMapTex.Sample(NoiseMapSampler, input.TexCoord + float2(i * offset, j * offset))
					.x);

			float centerMul = 1;
			if (i == 0 || j == 0) {
				centerMul = 2;
			}

			valueRL += i * (fNoiseScale.x * centerMul) * currentValue;
			valueTB += j * (fNoiseScale.x * centerMul) * currentValue;
		}
	}

	psout.Color.xyz = normalize(float3(-valueRL, valueTB, 1)) * 0.5 + 0.5;
	psout.Color.w = abs(NoiseMapTex.Sample(NoiseMapSampler, input.TexCoord).y);

#	elif defined(SCROLL_AND_BLEND)
	float noise1 =
		fAmplitude.x *
		(NoiseMapTex.Sample(NoiseMapSampler, input.TexCoord * fTexScale.x + fTexScroll0).z * 2 - 1);
	float noise2 =
		fAmplitude.y *
		(NoiseMapTex.Sample(NoiseMapSampler, input.TexCoord * fTexScale.y + fTexScroll1).y * 2 - 1);
	float noise3 =
		fAmplitude.z *
		(NoiseMapTex.Sample(NoiseMapSampler, input.TexCoord * fTexScale.z + fTexScroll2).x * 2 - 1);

	psout.Color = float4(saturate((noise1 + noise2 + noise3) * 0.5 + 0.5), 0, 0, 1);
#	endif

	return psout;
}
#endif
