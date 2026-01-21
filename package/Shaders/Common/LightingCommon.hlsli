#ifndef LIGHTING_COMMON_HLSLI
#define LIGHTING_COMMON_HLSLI

struct DirectContext
{
    float3 worldNormal;
    float3 vertexNormal;
    float3 viewDir;
    float3 lightDir;
    float3 halfVector;
    float3 lightColor;
#if defined(TRUE_PBR)
	float3 coatWorldNormal;
	float3 coatViewDir;
	float3 coatLightDir;
	float3 coatHalfVector;
	float3 coatLightColor;
#elif defined(HAIR) && defined(CS_HAIR)
	float hairShadow;
#endif
};

struct IndirectContext
{
	float3 worldNormal;
	float3 vertexNormal;
	float3 viewDir;
};

struct DirectLightingOutput
{
	float3 diffuse;
	float3 specular;
	float3 transmission;
#if defined(TRUE_PBR)
	float3 coatDiffuse;
#endif
};

struct IndirectLobeWeights
{
	float3 diffuse;
	float3 specular;
};

#if defined(TRUE_PBR)
#	if defined(GLINT)
#		include "Common/Glints/Glints2023.hlsli"
#	else
namespace Glints
{
	typedef float GlintCachedVars;
}
#	endif
#endif

struct MaterialProperties
{
	float3 BaseColor;
#if !defined(TRUE_PBR)
	float Shininess;
	float Glossiness;
	float3 SpecularColor;
#	if (defined(RIM_LIGHTING) || defined(SOFT_LIGHTING) || defined(LOAD_SOFT_LIGHTING))
    float3 rimSoftLightColor;
#   endif
#	if defined(BACK_LIGHTING)
	float3 backLightColor;
#	endif
	float Roughness;
	float3 F0;
#else
	float Roughness;
	float Metallic;
	float AO;
	float3 F0;
	float3 SubsurfaceColor;
	float Thickness;
	float3 CoatColor;
	float CoatStrength;
	float CoatRoughness;
	float3 CoatF0;
	float3 FuzzColor;
	float FuzzWeight;
	float GlintScreenSpaceScale;
	float GlintLogMicrofacetDensity;
	float GlintMicrofacetRoughness;
	float GlintDensityRandomization;
	Glints::GlintCachedVars GlintCache;
	float Noise;
#endif
};

float ShininessToRoughness(float shininess)
{
	return pow(abs(2.0 / (shininess + 2.0)), 0.25);
}

float3x3 ReconstructTBN(float3 worldPos, float3 worldNormal, float2 uv)
{
	float3 dFdx = ddx(worldPos);
	float3 dFdy = ddy(worldPos);
	float2 dUVdx = ddx(uv);
	float2 dUVdy = ddy(uv);
	float3 tangent = normalize(dFdx * dUVdy.y - dFdy * dUVdx.y);
	float3 bitangent = normalize(dFdy * dUVdx.x - dFdx * dUVdy.x);
	tangent = normalize(tangent - worldNormal * dot(worldNormal, tangent));
	bitangent = normalize(bitangent - worldNormal * dot(worldNormal, bitangent));

	return float3x3(tangent, bitangent, normalize(worldNormal));
}
#endif