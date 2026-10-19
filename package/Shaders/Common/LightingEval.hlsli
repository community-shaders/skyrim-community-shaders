#ifndef LIGHTING_EVAL_HLSLI
#define LIGHTING_EVAL_HLSLI
#include "Common/LightingCommon.hlsli"

#include "Common/BRDF.hlsli"
#include "Common/Math.hlsli"
#if defined(TRUE_PBR)
#	include "Common/PBR.hlsli"
#endif
#if defined(WETNESS_EFFECTS)
#	include "WetnessEffects/WetnessEffects.hlsli"
#endif

#if defined(TRUE_PBR)
DirectContext CreateDirectLightingContext(float3 worldNormal, float3 coatWorldNormal, float3 vertexNormal, float3 viewDir, float3 coatViewDir, float3 lightDir, float3 coatLightDir, float3 lightColor, float detailedShadow, float softShadow)
#else
DirectContext CreateDirectLightingContext(float3 worldNormal, float3 vertexNormal, float3 viewDir, float3 lightDir, float3 lightColor, float detailedShadow, float softShadow)
#endif
{
	DirectContext context = (DirectContext)0;
	context.worldNormal = normalize(worldNormal);
	context.vertexNormal = normalize(vertexNormal);
	context.viewDir = normalize(viewDir);
	context.lightDir = normalize(lightDir);
	context.halfVector = normalize(context.viewDir + context.lightDir);
	context.lightColor = lightColor;
	context.detailedShadow = detailedShadow;
	context.softShadow = softShadow;
#if defined(TRUE_PBR)
	context.coatWorldNormal = normalize(coatWorldNormal);
	context.coatViewDir = normalize(coatViewDir);
	context.coatLightDir = normalize(coatLightDir);
	context.coatHalfVector = normalize(context.coatViewDir + context.coatLightDir);
	[branch] if ((PBRFlags & PBR::Flags::InterlayerParallax) != 0)
	{
		context.coatLightColor = lightColor * softShadow;
	}
	else
	{
		context.coatLightColor = context.lightColor * detailedShadow;
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

float3 VanillaSpecular(DirectContext context, float shininess, bool specular, float2 uv, float2 uv_ddx, float2 uv_ddy)
{
	const float3 N = context.worldNormal;
	const float3 G = context.vertexNormal;
	float3 V = context.viewDir;
	const float3 L = context.lightDir;
	const float3 H = context.halfVector;
	float HdotN;
#if defined(ANISO_LIGHTING)
	const float3 AN = normalize(N * 0.5 + G);
	float LdotAN = dot(AN, L);
	float HdotAN = dot(AN, H);
	HdotN = 1 - min(1, abs(LdotAN - HdotAN));
#else
	HdotN = saturate(dot(H, N));
#endif

#if defined(SPARKLE)
	float lightColorMultiplier = 0;
#else
	float lightColorMultiplier = HdotN;
#endif
	if (specular)
		lightColorMultiplier = exp2(shininess * log2(HdotN));

#if defined(ANISO_LIGHTING)
	lightColorMultiplier *= 0.7 * max(0, L.z);
#endif

#if defined(SPARKLE) && !defined(SNOW)
	float3 sparkleUvScale = exp2(float3(1.3, 1.6, 1.9) * log2(abs(SparkleParams.x)).xxx);

	float sparkleColor1 = TexProjDetail.SampleGrad(SampProjDetailSampler, uv * sparkleUvScale.xx, uv_ddx * sparkleUvScale.x, uv_ddy * sparkleUvScale.x).z;
	float sparkleColor2 = TexProjDetail.SampleGrad(SampProjDetailSampler, uv * sparkleUvScale.yy, uv_ddx * sparkleUvScale.y, uv_ddy * sparkleUvScale.y).z;
	float sparkleColor3 = TexProjDetail.SampleGrad(SampProjDetailSampler, uv * sparkleUvScale.zz, uv_ddx * sparkleUvScale.z, uv_ddy * sparkleUvScale.z).z;
	float sparkleColor = ProcessSparkleColor(sparkleColor1) + ProcessSparkleColor(sparkleColor2) + ProcessSparkleColor(sparkleColor3);
	float VdotN = dot(V, N);
	V += N * -(2 * VdotN);
	float sparkleMultiplier = exp2(SparkleParams.w * log2(saturate(dot(V, -L)))) * (SparkleParams.z * sparkleColor);
	sparkleMultiplier = sparkleMultiplier >= 0.5 ? 1 : 0;
	lightColorMultiplier += sparkleMultiplier * HdotN;
#endif
	return lightColorMultiplier;
}

void EvaluateLighting(DirectContext context, MaterialProperties material, float3x3 tbnTr, float2 uv, float2 uv_ddx, float2 uv_ddy, out DirectLightingOutput lightingOutput)
{
	lightingOutput = (DirectLightingOutput)0;
#if defined(TRUE_PBR)
	PBR::GetDirectLightInput(lightingOutput, context, material, tbnTr, uv);
#else
#	if defined(CS_HAIR_SHADING)
	if (SharedData::hairSpecularSettings.Enabled) {
		Hair::GetHairDirectLight(lightingOutput, context, material, tbnTr, uv);
		return;
	}
#	endif
#	if defined(CS_SKIN_SHADING)
	if (SharedData::skinData.skinParams.w > 0.0f) {
		Skin::SkinDirectLightInput(lightingOutput, context, material);
		float3 softLightColor = context.lightColor * context.softShadow;

		// SSS fallback for forward skin rendering
#		if !defined(DEFERRED)
		const float NdotL = dot(context.worldNormal, context.lightDir);
		if (HasSoftLighting())
			lightingOutput.diffuse += softLightColor * GetSoftLightMultiplier(NdotL) * material.rimSoftLightColor;

		if (HasRimLighting())
			lightingOutput.diffuse += softLightColor * GetRimLightMultiplier(context.lightDir, context.viewDir, context.worldNormal) * material.rimSoftLightColor;

		if (HasBackLighting())
			lightingOutput.diffuse += softLightColor * saturate(-NdotL) * material.backLightColor;
#		endif
		return;
	}
#	endif
	const float NdotL = dot(context.worldNormal, context.lightDir);
	float3 diffuseLightColor = context.lightColor * context.detailedShadow;
	float3 softLightColor = context.lightColor * context.softShadow;
	lightingOutput.diffuse = saturate(NdotL) * diffuseLightColor * Color::VanillaNormalization();
	if (HasSoftLighting())
		lightingOutput.diffuse += softLightColor * GetSoftLightMultiplier(NdotL) * material.rimSoftLightColor * Color::VanillaNormalization();

	if (HasRimLighting())
		lightingOutput.diffuse += softLightColor * GetRimLightMultiplier(context.lightDir, context.viewDir, context.worldNormal) * material.rimSoftLightColor * Color::VanillaNormalization();

	if (HasBackLighting())
		lightingOutput.diffuse += softLightColor * saturate(-NdotL) * material.backLightColor * Color::VanillaNormalization();
	lightingOutput.specular = VanillaSpecular(context, material.Shininess, HasSpecular(), uv, uv_ddx, uv_ddy) * material.SpecularColor * material.Glossiness * diffuseLightColor * Color::VanillaNormalization();
#endif
}

void GetIndirectLobeWeights(out IndirectLobeWeights lobeWeights, IndirectContext context, MaterialProperties material, float2 uv)
{
	lobeWeights = (IndirectLobeWeights)0;
#if defined(TRUE_PBR)
	PBR::GetIndirectLobeWeights(lobeWeights, context, material);
#else
#	if defined(CS_HAIR_SHADING)
	if (SharedData::hairSpecularSettings.Enabled) {
		Hair::GetHairIndirectLobeWeights(lobeWeights, context, material, uv);
		return;
	}
#	endif
#	if defined(CS_SKIN_SHADING)
	if (SharedData::skinData.skinParams.w > 0.0f) {
		Skin::SkinIndirectLobeWeights(lobeWeights, material, context);
		return;
	}
#	endif
	lobeWeights.diffuse = material.BaseColor;
#	if defined(DYNAMIC_CUBEMAPS)
	if (any(material.F0 > 0.0)) {
		const float3 N = context.worldNormal;
		const float3 V = context.viewDir;
		const float3 VN = context.vertexNormal;

		float NdotV = saturate(dot(N, V));

		float2 specularBRDF = BRDF::EnvBRDF(material.Roughness, NdotV);
		lobeWeights.specular = material.F0 * specularBRDF.x + specularBRDF.y;
	}
#	endif
#endif
}

#if defined(WETNESS_EFFECTS)
void EvaluateWetnessLighting(float3 wetnessNormal, DirectContext context, float roughness, inout DirectLightingOutput lightingOutput)
{
#	if defined(TRUE_PBR)
	const float3 lightColor = context.coatLightColor;
	const float specularScale = 1.0;
#	else
	const float3 lightColor = context.lightColor;
	const float specularScale = context.detailedShadow * Color::PBRLightingCompensation * Color::PBRLightingScale;
#	endif

	WetnessEffects::SurfaceWetnessState wetnessState = WetnessEffects::InitSurfaceWetnessState(wetnessNormal, roughness);

	float3 wetnessSpecular = 0;
	WetnessEffects::ApplySurfaceWetnessDirectLighting(wetnessState, context.viewDir, context.lightDir, lightColor, specularScale, lightingOutput.diffuse, lightingOutput.specular, wetnessSpecular);
	lightingOutput.specular += wetnessSpecular;
}

float3 GetWetnessIndirectLobeWeights(inout IndirectLobeWeights lobeWeights, float3 wetnessNormal, float roughness, IndirectContext context)
{
	WetnessEffects::SurfaceWetnessState wetnessState = WetnessEffects::InitSurfaceWetnessState(wetnessNormal, roughness);
	return WetnessEffects::ApplySurfaceWetnessIndirectLobeWeights(lobeWeights.diffuse, lobeWeights.specular, wetnessState, context.viewDir);
}
#endif
#endif
