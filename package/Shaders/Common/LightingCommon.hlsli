#ifndef LIGHTING_COMMON_HLSLI
#define LIGHTING_COMMON_HLSLI

#include "Common/BRDF.hlsli"

#if defined(TRUE_PBR)
#	include "Common/PBR.hlsli"
#endif

#if defined(TRUE_PBR)
struct CoatContext
{
	float3 coatNormal;
	float3 coatViewDir;
	float3 coatLightDir;
	float3 coatHalfVector;
};
#endif

struct Context
{
    float3 worldNormal;
	float NdotL;
    float3 geometryNormal;
	float NdotV;
    float3 viewDir;
	float NdotH;
    float3 lightDir;
	float VdotH;
    float3 halfVector;
	float LdotH;
    float3 lightColor;

#if defined(TRUE_PBR)
	CoatContext coatContext;
#endif
};

struct LightingOutput
{
	float3 diffuse = 0;
	float3 specular = 0;
	float3 transmission = 0;
#if defined(TRUE_PBR)
	float3 coatDiffuse = 0;
#endif
};

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

Context CreateLightingContext(float3 worldNormal, float3 geometryNormal, float3 viewDir, float3 lightDir, float3 lightColor, float3 lightAttenuation, float shadowFactor)
{
    Context context;
    context.worldNormal = normalize(worldNormal);
    context.geometryNormal = normalize(geometryNormal);
    context.viewDir = normalize(viewDir);
    context.lightDir = normalize(lightDir);
    context.halfVector = normalize(context.viewDir + context.lightDir);
    context.lightColor = lightColor * lightAttenuation * shadowFactor;
    context.NdotL = dot(context.worldNormal, context.lightDir);
    context.NdotV = dot(context.worldNormal, context.viewDir);
    context.NdotH = dot(context.worldNormal, context.halfVector);
    context.VdotH = dot(context.viewDir, context.halfVector);
    context.LdotH = dot(context.lightDir, context.halfVector);
    return context;
}

float3 VanillaSpecular(Context context, float shininess, float2 uv)
{
    float3 N = context.worldNormal;
    float3 G = context.geometryNormal;
    float3 V = context.viewDir;
    float3 L = context.lightDir;
    float3 H = context.halfVector;
    float HdotN;
#	if defined(ANISO_LIGHTING)
	float3 AN = normalize(N * 0.5 + G);
	float LdotAN = dot(AN, L);
	float HdotAN = dot(AN, H);
	HdotN = 1 - min(1, abs(LdotAN - HdotAN));
#	else
	HdotN = saturate(context.NdotH);
#	endif

#	if defined(SPECULAR)
	float lightColorMultiplier = exp2(shininess * log2(HdotN));

#	elif defined(SPARKLE)
	float lightColorMultiplier = 0;
#	else
	float lightColorMultiplier = HdotN;
#	endif

#	if defined(ANISO_LIGHTING)
	lightColorMultiplier *= 0.7 * max(0, L.z);
#	endif

#	if defined(SPARKLE) && !defined(SNOW)
	float3 sparkleUvScale = exp2(float3(1.3, 1.6, 1.9) * log2(abs(SparkleParams.x)).xxx);

	float sparkleColor1 = TexProjDetail.Sample(SampProjDetailSampler, uv * sparkleUvScale.xx).z;
	float sparkleColor2 = TexProjDetail.Sample(SampProjDetailSampler, uv * sparkleUvScale.yy).z;
	float sparkleColor3 = TexProjDetail.Sample(SampProjDetailSampler, uv * sparkleUvScale.zz).z;
	float sparkleColor = ProcessSparkleColor(sparkleColor1) + ProcessSparkleColor(sparkleColor2) + ProcessSparkleColor(sparkleColor3);
	float VdotN = dot(V, N);
	V += N * -(2 * VdotN);
	float sparkleMultiplier = exp2(SparkleParams.w * log2(saturate(dot(V, -L)))) * (SparkleParams.z * sparkleColor);
	sparkleMultiplier = sparkleMultiplier >= 0.5 ? 1 : 0;
	lightColorMultiplier += sparkleMultiplier * HdotN;
#	endif
	return lightColorMultiplier;
}

#if defined(TRUE_PBR)
void EvaluateLightingPBR(Context context, MaterialProperties material, float3x3 tbnTr, float2 uv, out LightingOutput outLighting)
{
	// TODO: migrate PBR lighting code here
}
#endif

void EvaluateLighting(Context context, MaterialProperties material, float3x3 tbnTr, float2 uv, out LightingOutput outLighting)
{
	outLighting = (LightingOutput)0;
#if defined(TRUE_PBR)
	EvaluateLightingPBR(context, material, tbnTr, uv, outLighting);
#else
    outLighting.diffuse = material.BaseColor * saturate(context.NdotL) * context.lightColor;
#		if defined(SOFT_LIGHTING)
	outLighting.diffuse += context.lightColor * GetSoftLightMultiplier(context.NdotL) * rimSoftLightColor.xyz;
#		endif

#		if defined(RIM_LIGHTING)
	outLighting.diffuse += context.lightColor * GetRimLightMultiplier(context.lightDir, context.viewDir, context.worldNormal) * rimSoftLightColor.xyz;
#		endif

#		if defined(BACK_LIGHTING)
	outLighting.diffuse += context.lightColor * saturate(-context.NdotL) * backLightColor.xyz;
#		endif
    outLighting.specular = VanillaSpecular(context, material.Shininess, uv) * material.SpecularColor * material.Glossiness * context.lightColor;
#endif
}

#endif