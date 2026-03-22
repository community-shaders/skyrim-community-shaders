struct VS_INPUT
{
	float4 Position: POSITION0;
	float2 TexCoord: TEXCOORD0;
};

struct VS_OUTPUT
{
	float4 Position: SV_POSITION0;
	float3 TexCoordColor: TEXCOORD0;

#if defined(SPLATTER)
	float2 TexCoordAlpha: TEXCOORD1;
#endif
};

#ifdef VSHADER
cbuffer PerGeometry : register(b2)
{
	row_major float4x4 WorldViewProj : packoffset(c0);
	float4 LightLoc : packoffset(c4);
	float Ctrl : packoffset(c5);  // fBloodSplatterFlareOffsetScale
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;

	float4 projectedPosition = mul(WorldViewProj, float4(input.Position.xy, 0.0, 1.0));
	float2 lightOffset = (-LightLoc.xy + projectedPosition.xy) * LightLoc.ww;

#	if defined(SPLATTER)
	vsout.TexCoordAlpha = -(lightOffset.xy * float2(-0.5, 0.5)) + input.TexCoord;
#	elif defined(FLARE)
	projectedPosition.xy += lightOffset * Ctrl.xx;
#	endif

	vsout.Position = projectedPosition;
	vsout.TexCoordColor = float3(input.TexCoord, input.Position.z);

	return vsout;
}
#endif

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color: SV_Target0;
};

#ifdef PSHADER
SamplerState SampBloodColor : register(s0);
SamplerState SampBloodAlpha : register(s1);
SamplerState SampFlareColor : register(s2);
SamplerState SampFlareHDR : register(s3);

Texture2D<float4> TexBloodColor : register(t0);
Texture2D<float4> TexBloodAlpha : register(t1);
Texture2D<float4> TexFlareColor : register(t2);
Texture2D<float4> TexFlareHDR : register(t3);

cbuffer PerGeometry : register(b2)
{
	float Alpha : packoffset(c0);
};

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

#	if defined(SPLATTER)
	float3 bloodAplha = TexBloodAlpha.Sample(SampBloodAlpha, input.TexCoordAlpha.xy).xyz;
	float4 bloodColor = TexBloodColor.Sample(SampBloodColor, input.TexCoordColor.xy).xyzw;

	float colorMul = bloodColor.w * (Alpha * input.TexCoordColor.z);
	float3 adjustedBloodColor = colorMul * (bloodColor.xyz - 1.0.xxx) + 1.0.xxx;
	float3 adjustedBloodAlpha = colorMul * (-adjustedBloodColor.xyz + bloodAplha);

	psout.Color = float4(adjustedBloodAlpha + adjustedBloodColor, 1.0);
#	elif defined(FLARE)
	float flareMult = TexFlareColor.Sample(SampFlareColor, input.TexCoordColor.xy).x;
	float4 colorHDR = TexFlareHDR.Sample(SampFlareHDR, input.TexCoordColor.xy).xyzw;

	psout.Color = (colorHDR * flareMult) * Alpha;
#	endif

	return psout;
}
#endif
