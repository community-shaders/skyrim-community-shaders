#include "Common/DummyVSTexCoord.hlsl"

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color: SV_Target0;
};

#if defined(PSHADER)
SamplerState AlbedoSampler : register(s0);
SamplerState DiffuseSampler : register(s1);
SamplerState SpecularSampler : register(s2);
SamplerState SAOSampler : register(s3);
SamplerState FogSampler : register(s4);
SamplerState DirDiffuseSampler : register(s5);
SamplerState DirSpecularSampler : register(s6);
SamplerState ShadowMaskSampler : register(s7);

Texture2D<float4> AlbedoTex : register(t0);
Texture2D<float4> DiffuseTex : register(t1);
Texture2D<float4> SpecularTex : register(t2);
Texture2D<float4> SAOTex : register(t3);
Texture2D<float4> FogTex : register(t4);
Texture2D<float4> DirDiffuseTex : register(t5);
Texture2D<float4> DirSpecularTex : register(t6);
Texture2D<float4> ShadowMaskTex : register(t7);

cbuffer PerGeometry : register(b2)
{
	float4 FogParam : packoffset(c0);
	float4 FogNearColor : packoffset(c1);
};

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;

	float4 diffuse = DiffuseTex.Sample(DiffuseSampler, input.TexCoord);
	float4 specular = SpecularTex.Sample(SpecularSampler, input.TexCoord);
	float4 albedo = AlbedoTex.Sample(AlbedoSampler, input.TexCoord);

#	if defined(DIRECTIONALLIGHT)
	float4 dirDiffuse = DirDiffuseTex.Sample(DirDiffuseSampler, input.TexCoord);
	float4 dirSpecular = DirSpecularTex.Sample(DirSpecularSampler, input.TexCoord);
#	else
	float4 dirDiffuse = 0;
	float4 dirSpecular = 0;
#	endif

#	if !defined(MENU)
	float shadowMask = ShadowMaskTex.Sample(ShadowMaskSampler, input.TexCoord).x;
	float sao = SAOTex.Sample(SAOSampler, input.TexCoord).x;
#	else
	float shadowMask = 1;
	float sao = 1;
#	endif

	float4 preFog = (diffuse * sao + shadowMask * dirDiffuse) * albedo +
	                (specular * sao + dirSpecular * shadowMask);

	float4 fog = FogTex.Sample(FogSampler, input.TexCoord);

	if (fog.x + fog.y + fog.z + fog.w != 0) {
		psout.Color = float4(FogNearColor.w * lerp(preFog.xyz, fog.xyz, fog.w), saturate(preFog.w));
	} else {
		psout.Color = preFog;
	}

	return psout;
}
#endif
