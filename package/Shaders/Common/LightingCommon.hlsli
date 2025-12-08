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
#endif
};

struct IndirectContext
{
	float3 worldNormal;
	float3 vertexNormal;
	float3 viewDir;
}

struct DirectLightingOutput
{
	float3 diffuse = 0;
	float3 specular = 0;
	float3 transmission = 0;
#if defined(TRUE_PBR)
	float3 coatDiffuse = 0;
#endif
};

struct IndirectLobeWeights
{
	float3 diffuse = 0;
	float3 specular = 0;
}

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
	float3 BaseColor = 0;
#	if !defined(TRUE_PBR)
	float Shininess = 0;
	float Glossiness = 0;
	float3 SpecularColor = 0;
#	    if (defined(RIM_LIGHTING) || defined(SOFT_LIGHTING) || defined(LOAD_SOFT_LIGHTING))
    float3 rimSoftLightColor = 0;
#       endif
#	else
	float Roughness = 1;
	float Metallic = 0;
	float AO = 1;
	float3 F0 = 0;
	float3 SubsurfaceColor = 0;
	float Thickness = 0;
	float3 CoatColor = 0;
	float CoatStrength = 0;
	float CoatRoughness = 0;
	float3 CoatF0 = 0;
	float3 FuzzColor = 0;
	float FuzzWeight = 0;
	float GlintScreenSpaceScale = 1.5;
	float GlintLogMicrofacetDensity = 1.0;
	float GlintMicrofacetRoughness = 0.015;
	float GlintDensityRandomization = 2.0;
	Glints::GlintCachedVars GlintCache;
	float Noise = 0;
#	endif
};