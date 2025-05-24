#if defined(HEIGHTMAP_SMOOTH) || defined(HEIGHTMAP_RAIN) || defined(HEIGHTMAP_WADING)
#	define HEIGHTMAP
#endif

struct VS_INPUT
{
	float4 Position : POSITION0;
	float2 TexCoord : TEXCOORD0;
};

struct VS_OUTPUT
{
	float4 Position : SV_POSITION0;
	float2 TexCoord : TEXCOORD0;
#if defined(HEIGHTMAP)
	float2 TexCoord1 : TEXCOORD1;
	float2 TexCoord2 : TEXCOORD2;
	float2 TexCoord3 : TEXCOORD3;
	float2 TexCoord4 : TEXCOORD4;
#endif
};

#ifdef VSHADER
VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;

	vsout.Position = float4(input.Position.xyz, 1);
	vsout.TexCoord = input.TexCoord.xy;
#	if defined(HEIGHTMAP)
	vsout.TexCoord1 = input.TexCoord.xy + float2(-0.0009765625, -0.0009765625);
	vsout.TexCoord2 = input.TexCoord.xy + float2(-0.0009765625, 0.0009765625);
	vsout.TexCoord3 = input.TexCoord.xy + float2(0.0009765625, -0.0009765625);
	vsout.TexCoord4 = input.TexCoord.xy + float2(0.0009765625, 0.0009765625);
#	endif

	return vsout;
}
#endif

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color : SV_Target0;
};

#if defined(PSHADER)
#	if defined(BLEND_HEIGHTMAPS)
SamplerState HeightMap01Sampler : register(s0);
SamplerState HeightMap02Sampler : register(s1);
#	elif defined(NORMALS)
SamplerState DisplaceMapSampler : register(s0);
#	elif defined(TEXCOORD_OFFSET)
SamplerState DisplaySamplerSampler : register(s0);
#	elif defined(HEIGHTMAP)
SamplerState HeightMapSampler : register(s0);
#	endif

#	if defined(BLEND_HEIGHTMAPS)
Texture2D<float4> HeightMap01Tex : register(t0);
Texture2D<float4> HeightMap02Tex : register(t1);
#	elif defined(NORMALS)
Texture2D<float4> DisplaceMapTex : register(t0);
#	elif defined(TEXCOORD_OFFSET)
Texture2D<float4> DisplaySamplerTex : register(t0);
#	elif defined(HEIGHTMAP)
Texture2D<float4> HeightMapTex : register(t0);
#	endif

cbuffer PerGeometry : register(b2)
{
	float Time : packoffset(c0.x);
	float BlendAmount : packoffset(c0.y);
	float2 TextureOffset : packoffset(c0.z);
	float fDamp : packoffset(c1.x);
	float3 RainVars : packoffset(c1.y);
	float4 WadingVars : packoffset(c2.x);
};

#	if defined(NORMALS)
float GetDisplacementNormalValue(float2 texCoord)
{
	float displaceValue = DisplaceMapTex.Sample(DisplaceMapSampler, texCoord).x;
	return fDamp * pow(abs(displaceValue), 7);
}
#	endif

#	if defined(HEIGHTMAP)
float4 GetHeight(PS_INPUT input, uniform float3 Vars)
{
	float2 height = HeightMapTex.Sample(HeightMapSampler, input.TexCoord).xy;
	float heightLB = HeightMapTex.Sample(HeightMapSampler, input.TexCoord1).x;
	float heightLT = HeightMapTex.Sample(HeightMapSampler, input.TexCoord2).x;
	float heightRB = HeightMapTex.Sample(HeightMapSampler, input.TexCoord3).x;
	float heightRT = HeightMapTex.Sample(HeightMapSampler, input.TexCoord4).x;

	float heightDispersion = -height.x * 4 + (heightLB + heightLT + heightRB + heightRT);
	float tmp1 = Vars.x * heightDispersion + (height.y - 0.5);
	float heightLR = heightLB + heightLT - heightRB - heightRT;
	float heightBT = heightLB - heightLT + heightRB - heightRT;
	return 0.5 + float4(max(-1, Vars.z * float2(Vars.y * tmp1 + (height.x - 0.5), tmp1)), 0.5 * float2(heightLR, heightBT));
}
#	endif

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

#	if defined(BLEND_HEIGHTMAPS)
	float height01 = HeightMap01Tex.Sample(HeightMap01Sampler, input.TexCoord).x;
	float height02 = HeightMap02Tex.Sample(HeightMap02Sampler, input.TexCoord).x;

	psout.Color.xyz = lerp((0.8 / fDamp) * abs(height01), abs(height02), BlendAmount);
	psout.Color.w = 1;
#	elif defined(CLEAR_SIMULATION)
	psout.Color = 0.5;
#	elif defined(NORMALS)

	float offset = 0.001953125;
	float valueRL = 0;
	float valueTB = 0;
	[unroll] for (int i = -1; i <= 1; ++i)
	{
		[unroll] for (int j = -1; j <= 1; ++j)
		{
			if (i == 0 && j == 0) {
				continue;
			}

			float currentValue = GetDisplacementNormalValue(input.TexCoord + float2(i * offset, j * offset));

			float centerMul = 1;
			if (i == 0 || j == 0) {
				centerMul = 2;
			}

			valueRL += i * centerMul * currentValue;
			valueTB += j * centerMul * currentValue;
		}
	}

	psout.Color = float4(normalize(float3(-valueRL, valueTB, 1)), 1) * 0.5 + 0.5;

#	elif defined(RIPPLE_MAKER_RAIN) || defined(RIPPLE_MAKER_WADING)
	psout.Color = float4(1, 0.5, 0.5, 0.5);
#	elif defined(TEXCOORD_OFFSET)
	float lerpFactor = saturate(10 * (-0.4 + length(input.TexCoord - 0.5)));
	float4 displayColor = DisplaySamplerTex.Sample(DisplaySamplerSampler, TextureOffset + input.TexCoord);
	psout.Color.xy = displayColor.xy;
	psout.Color.zw = lerp(displayColor.zw, 0.5, lerpFactor);
#	elif defined(HEIGHTMAP_SMOOTH)
	psout.Color = HeightMapTex.Sample(HeightMapSampler, input.TexCoord);
#	elif defined(HEIGHTMAP_RAIN)
	psout.Color = GetHeight(input, RainVars);
#	elif defined(HEIGHTMAP_WADING)
	psout.Color = GetHeight(input, WadingVars.xyz);
#	endif

	return psout;
}
#endif
