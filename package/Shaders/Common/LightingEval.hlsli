#ifndef LIGHTING_EVAL_HLSLI
#define LIGHTING_EVAL_HLSLI
#include "Common/LightingCommon.hlsli"

#include "Common/BRDF.hlsli"
#include "Common/Math.hlsli"
#if defined(TRUE_PBR)
#	include "Common/PBR.hlsli"
#endif

#if defined(TRUE_PBR)
DirectContext CreateDirectLightingContext(float3 worldNormal, float3 coatWorldNormal, float3 vertexNormal, float3 viewDir, float3 coatViewDir, float3 lightDir, float3 coatLightDir, float3 lightColor, float shadowFactor, float parallaxShadow)
#else
DirectContext CreateDirectLightingContext(float3 worldNormal, float3 vertexNormal, float3 viewDir, float3 lightDir, float3 lightColor, float shadowFactor, float parallaxShadow)
#endif
{
    DirectContext context = (DirectContext)0;
    context.worldNormal = normalize(worldNormal);
    context.vertexNormal = normalize(vertexNormal);
    context.viewDir = normalize(viewDir);
    context.lightDir = normalize(lightDir);
    context.halfVector = normalize(context.viewDir + context.lightDir);
    context.lightColor = lightColor * shadowFactor * parallaxShadow;
#if defined(TRUE_PBR)
	context.coatWorldNormal = normalize(coatWorldNormal);
	context.coatViewDir = normalize(coatViewDir);
	context.coatLightDir = normalize(coatLightDir);
	context.coatHalfVector = normalize(context.coatViewDir + context.coatLightDir);
	[branch] if ((PBRFlags & PBR::Flags::InterlayerParallax) != 0)
	{
		context.coatLightColor = lightColor * shadowFactor;
	}
	else
	{
		context.coatLightColor = context.lightColor;
	}
#endif
    return context;
}

IndirectContext CreateIndirectLightingContext(float3 worldNormal, float3 vertexNormal, float3 viewDir)
{
	IndirectContext context = (IndirectContext)0;
	context.worldNormal = normalize(worldNormal);
	context.vertexNormal = normalize(vertexNormal);
	context.viewDir = normalize(viewDir);
	return context;
}

float3 VanillaSpecular(DirectContext context, float shininess, float2 uv)
{
    const float3 N = context.worldNormal;
    const float3 G = context.vertexNormal;
    float3 V = context.viewDir;
    const float3 L = context.lightDir;
    const float3 H = context.halfVector;
    float HdotN;
#	if defined(ANISO_LIGHTING)
	const float3 AN = normalize(N * 0.5 + G);
	float LdotAN = dot(AN, L);
	float HdotAN = dot(AN, H);
	HdotN = 1 - min(1, abs(LdotAN - HdotAN));
#	else
	HdotN = saturate(dot(H, N));
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

void EvaluateLighting(DirectContext context, MaterialProperties material, float3x3 tbnTr, float2 uv, out DirectLightingOutput lightingOutput)
{
	lightingOutput = (DirectLightingOutput)0;
#if defined(TRUE_PBR)
	PBR::GetDirectLightInput(lightingOutput, context, material, tbnTr, uv);
	lightingOutput.diffuse *= Color::PBRLightingScale;
	lightingOutput.specular *= Color::PBRLightingScale;
	lightingOutput.transmission *= Color::PBRLightingScale;
#else
#	if defined(HAIR) && defined(CS_HAIR)
	if (SharedData::hairSpecularSettings.Enabled)
	{
		Hair::GetHairDirectLight(lightingOutput, context, material, tbnTr, uv);
		return;
	}
#	endif
	const float NdotL = dot(context.worldNormal, context.lightDir);
    lightingOutput.diffuse = material.BaseColor * saturate(NdotL) * context.lightColor;
#		if defined(SOFT_LIGHTING)
	lightingOutput.diffuse += context.lightColor * GetSoftLightMultiplier(NdotL) * material.rimSoftLightColor;
#		endif

#		if defined(RIM_LIGHTING)
	lightingOutput.diffuse += context.lightColor * GetRimLightMultiplier(context.lightDir, context.viewDir, context.worldNormal) * material.rimSoftLightColor;
#		endif

#		if defined(BACK_LIGHTING)
	lightingOutput.diffuse += context.lightColor * saturate(-NdotL) * material.backLightColor;
#		endif
    lightingOutput.specular = VanillaSpecular(context, material.Shininess, uv) * material.SpecularColor * material.Glossiness * context.lightColor;
#endif
}

void GetIndirectLobeWeights(out IndirectLobeWeights lobeWeights, IndirectContext context, MaterialProperties material, float2 uv)
{
	lobeWeights = (IndirectLobeWeights)0;
#if defined(TRUE_PBR)
	PBR::GetIndirectLobeWeights(lobeWeights, context, material);
#else
#	if defined(HAIR) && defined(CS_HAIR)
	if (SharedData::hairSpecularSettings.Enabled)
	{
		Hair::GetHairIndirectLobeWeights(lobeWeights, context, material, uv);
		return;
	}
#	endif
	lobeWeights.diffuse = material.BaseColor;
#	if defined(DYNAMIC_CUBEMAPS)
	if (!any(material.F0 > 0)) {
		const float3 N = context.worldNormal;
		const float3 V = context.viewDir;
		const float3 VN = context.vertexNormal;

		float NdotV = saturate(dot(N, V));

		float2 specularBRDF = BRDF::EnvBRDF(material.Roughness, NdotV);
		lobeWeights.specular = material.F0 * specularBRDF.x + specularBRDF.y;

		lobeWeights.diffuse *= (1 - lobeWeights.specular);
		lobeWeights.specular *= 1 + material.F0 * (1 / (specularBRDF.x + specularBRDF.y) - 1);

		// Horizon specular occlusion
		// https://marmosetco.tumblr.com/post/81245981087
		float3 R = reflect(-V, N);
		float horizon = min(1.0 + dot(R, VN), 1.0);
		horizon = horizon * horizon;
		lobeWeights.specular *= horizon;
	}
#	endif
#endif
}

#if defined(WETNESS_EFFECTS)
void EvaluateWetnessLighting(float3 wetnessNormal, DirectContext context, float roughness, inout DirectLightingOutput lightingOutput)
{
	const float wetnessStrength = saturate(1 - roughness);
#	if defined(TRUE_PBR)
	const float3 lightColor = context.coatLightColor;
#	else
	const float3 lightColor = context.lightColor;
#	endif

	const float wetnessF0 = 0.02;

	const float3 N = wetnessNormal;
	const float3 V = context.viewDir;
	const float3 L = context.lightDir;
	const float3 H = context.halfVector;

	float NdotL = clamp(dot(N, L), EPSILON_DOT_CLAMP, 1);
	float NdotV = saturate(abs(dot(N, V)) + EPSILON_DOT_CLAMP);
	float NdotH = saturate(dot(N, H));
	float VdotH = saturate(dot(V, H));

	float D = BRDF::D_GGX(roughness, NdotH);
	float G = BRDF::Vis_SmithJointApprox(roughness, NdotV, NdotL);
	float3 F = BRDF::F_Schlick(wetnessF0, VdotH);

	F *= wetnessStrength;

	float3 wetnessSpecular = D * G * F * NdotL * lightColor;

#if !defined(TRUE_PBR)
	wetnessSpecular *= Math::PI * Color::PBRLightingScale;  // Compensate for GGX on traditional specular
#endif

	lightingOutput.diffuse *= 1 - F;
	lightingOutput.specular *= 1 - F;
	lightingOutput.specular += wetnessSpecular;
}

float3 GetWetnessIndirectLobeWeights(inout IndirectLobeWeights lobeWeights, float3 wetnessNormal, float roughness, IndirectContext context)
{
	const float wetnessF0 = 0.02;
	const float wetnessStrength = saturate(1 - roughness);

	const float3 N = wetnessNormal;
	const float3 V = context.viewDir;
	const float3 VN = context.vertexNormal;

	float NdotV = saturate(abs(dot(N, V)) + EPSILON_DOT_CLAMP);
	float2 specularBRDF = BRDF::EnvBRDF(roughness, NdotV);
	float3 specularLobeWeight = wetnessF0 * specularBRDF.x + specularBRDF.y;

	specularLobeWeight *= wetnessStrength;

	lobeWeights.diffuse *= 1 - specularLobeWeight;
	lobeWeights.specular *= 1 - specularLobeWeight;

	// Horizon specular occlusion
	// https://marmosetco.tumblr.com/post/81245981087
	float3 R = reflect(-V, N);
	float horizon = min(1.0 + dot(R, VN), 1.0);
	horizon = horizon * horizon;
	specularLobeWeight *= horizon;

	return specularLobeWeight;
}
#endif
#endif