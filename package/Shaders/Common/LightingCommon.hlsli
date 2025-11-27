#ifndef LIGHTING_COMMON_HLSLI
#define LIGHTING_COMMON_HLSLI

#include "Common/BRDF.hlsli"

struct Context
{
    float3 worldNormal;
    float3 geometryNormal;
    float3 viewDir;
    float3 lightDir;
    float3 halfVector;
    float3 lightColor;
    float NdotL;
    float NdotV;
    float NdotH;
    float VdotH;
    float LdotH;
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

void EvaluateLighting(Context context, MaterialProperties material, float2 uv, out float3 outDiffuse, out float3 outSpecular, out float3 outTransmission)
{
#if !defined(TRUE_PBR)
    outDiffuse = material.BaseColor * saturate(context.NdotL) * context.lightColor;
    #		if defined(SOFT_LIGHTING)
	outDiffuse += context.lightColor * GetSoftLightMultiplier(context.NdotL) * rimSoftLightColor.xyz;
#		endif

#		if defined(RIM_LIGHTING)
	outDiffuse += context.lightColor * GetRimLightMultiplier(context.lightDir, context.viewDir, context.worldNormal) * rimSoftLightColor.xyz;
#		endif

#		if defined(BACK_LIGHTING)
	outDiffuse += context.lightColor * saturate(-context.NdotL) * backLightColor.xyz;
#		endif
    outSpecular = VanillaSpecular(context, material.Shininess, uv) * material.SpecularColor * material.Glossiness * context.lightColor;
#else
    // TODO: Migrate from PBR.hlsli
#endif
}

#endif