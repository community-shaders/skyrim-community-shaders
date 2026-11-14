#define TRUE_PBR
#define GLINT

static uint PBRFlags = 0;

#include "Common/LightingEval.hlsli"
#include "SnowDeformation/SnowCommon.hlsli"

#if defined(SNOW_EXP_HEIGHT_FOG)
#	define SampColorSampler ShellLinearSampler
#	include "ExponentialHeightFog/ExponentialHeightFog.hlsli"
#	undef SampColorSampler
#endif

#if defined(SNOW_IBL)
#	include "IBL/IBL.hlsli"
#endif

float3 SnowAmbientColor(float3 normalWS)
{
	float3 ambientColor = Color::Ambient(max(0, SharedData::GetAmbient(normalWS)));
#if defined(SNOW_IBL)
	[branch] if (SharedData::iblSettings.EnableIBL)
		ambientColor = ImageBasedLighting::GetDiffuseIBL(ambientColor, -normalWS);
#endif
	return ambientColor;
}

#define SNOW_SKYLIGHTING(ambientPart, diffuseLobe, directDiffuse, directSpecular, vertexAO, pos, n)                                                      \
	vertexAO = lerp(1.0, vertexAO, SharedData::truePBRSettings.VertexAOStrength);                                                                        \
	[branch] if (SkylightingActive > 0.5)                                                                                                                \
	{                                                                                                                                                    \
		sh2 skylightingSH = Skylighting::Sample(pos, n);                                                                                                 \
		float skylightingDiffuse = Skylighting::GetSkylightingDiffuse(skylightingSH, pos, n, vertexAO);                                                  \
		ambientPart = ColorManagement::SceneColor::ScaleByLinear(ambientPart, MultiBounceAO(diffuseLobe * Color::PBRLightingScale, skylightingDiffuse)); \
	}                                                                                                                                                    \
	ambientPart *= Color::PBRLightingScale;                                                                                                              \
	directDiffuse *= Color::PBRLightingScale;                                                                                                            \
	directSpecular *= Color::PBRLightingScale;                                                                                                           \
	diffuseLobe *= Color::PBRLightingScale

struct SnowSunLighting
{
	float3 directDiffuse;
	float3 directSpecular;
	float3 diffuseLobe;
	float3 specularLobe;
	float3 markSpecular;
};

float3 SnowVanillaSpecular(float3 normalWS, float3 V, float3 L, float3 lightColor, float shadow, float4 specular)
{
	DirectContext context = CreateDirectLightingContext(normalWS, normalWS, normalWS, V, V, L, L, lightColor, shadow, shadow);
	return VanillaSpecular(context, specular.w, true, 0.0, 0.0, 0.0) * specular.rgb * lightColor * shadow * Color::BRDFScale;
}

struct SnowMaterialCtx
{
	MaterialProperties material;
	float3x3 tbnTr;
};

SnowMaterialCtx SnowBuildMaterial(float3 normalWS,
	float3 albedo, float roughness, float3 F0, float ao,
	float4 glintParams, float glintActive,
	float2 glintUV, float2 uvDDX, float2 uvDDY, float2 pixelPos)
{
	MaterialProperties material = (MaterialProperties)0;
	material.BaseColor = albedo;
	material.Roughness = roughness;
	material.F0 = F0;
	material.AO = ao;
	material.CoatColor = 1.0;

	material.GlintScreenSpaceScale = max(1.0, glintParams.w);
	material.GlintLogMicrofacetDensity = glintActive > 0.5 ? clamp(glintParams.x, PBR::Constants::MinGlintDensity, PBR::Constants::MaxGlintDensity) : PBR::Constants::MinGlintDensity;
	material.GlintMicrofacetRoughness = clamp(glintParams.y, PBR::Constants::MinGlintRoughness, PBR::Constants::MaxGlintRoughness);
	material.GlintDensityRandomization = clamp(glintParams.z, PBR::Constants::MinGlintDensityRandomization, PBR::Constants::MaxGlintDensityRandomization);
	float glintNoise = Random::R1Modified(float(SharedData::FrameCount), (Random::pcg2d(uint2(pixelPos)) / 4294967296.0).x);
	material.Noise = glintNoise;
	[branch] if (material.GlintLogMicrofacetDensity > 1.1)
		Glints::PrecomputeGlints(glintNoise, glintUV, uvDDX, uvDDY, material.GlintScreenSpaceScale, material.GlintCache);

	SNOW_TANGENT_FRAME(glintT, glintB, normalWS);

	SnowMaterialCtx ctx;
	ctx.material = material;
	ctx.tbnTr = float3x3(glintT, glintB, normalWS);
	return ctx;
}

SnowSunLighting SnowEvaluateSunPBR(SnowMaterialCtx mtl, float3 normalWS, float3 V,
	float3 worldPos, float3 camPosAdjust, float sunShadow,
	float2 glintUV, float2 uvDDX, float2 uvDDY, float4 markSpecular)
{
	DirectLightingOutput lightingOutput = (DirectLightingOutput)0;
	float3 markLit = 0.0;
	[branch] if (sunShadow != 0.0)
	{
		float3 sunColor = Color::DirectionalLight(SharedData::DirLightColor.xyz) * Color::PBRLightingCompensation;

#if defined(SNOW_EXP_HEIGHT_FOG)
		[branch] if (SharedData::exponentialHeightFogSettings.enabled)
			sunColor *= ExponentialHeightFog::GetSunlightFogAttenuation(worldPos, camPosAdjust);
#endif

		float3 L = SharedData::DirLightDirection.xyz;
		DirectContext context = CreateDirectLightingContext(normalWS, normalWS, normalWS, V, V, L, L, sunColor, sunShadow, sunShadow);
		EvaluateLighting(context, mtl.material, mtl.tbnTr, glintUV, uvDDX, uvDDY, lightingOutput);
		[branch] if (any(markSpecular.rgb > 0.0))
			markLit = SnowVanillaSpecular(normalWS, V, L, sunColor / Color::PBRLightingCompensation, sunShadow, markSpecular);
	}

	IndirectContext indirectContext = CreateIndirectLightingContext(normalWS, normalWS, V);
	IndirectLobeWeights lobeWeights;
	GetIndirectLobeWeights(lobeWeights, indirectContext, mtl.material, glintUV);

	SnowSunLighting o;
	o.directDiffuse = lightingOutput.diffuse * mtl.material.BaseColor + lightingOutput.transmission;
	o.directSpecular = lightingOutput.specular;
	o.diffuseLobe = lobeWeights.diffuse;
	o.specularLobe = lobeWeights.specular;
	o.markSpecular = markLit;
	return o;
}

void SnowEvaluateLightPBR(SnowMaterialCtx mtl, float3 normalWS, float3 V, float3 L,
	float3 lightColor, float lightShadow,
	float2 glintUV, float2 uvDDX, float2 uvDDY, float4 markSpecular,
	inout float3 diffuse, inout float3 specular, inout float3 markLit)
{
	float3 c = lightColor * Color::PBRLightingCompensation;
	DirectContext context = CreateDirectLightingContext(normalWS, normalWS, normalWS, V, V, L, L, c, lightShadow, lightShadow);

	DirectLightingOutput lightingOutput;
	EvaluateLighting(context, mtl.material, mtl.tbnTr, glintUV, uvDDX, uvDDY, lightingOutput);

	diffuse += lightingOutput.diffuse * mtl.material.BaseColor + lightingOutput.transmission;
	specular += lightingOutput.specular;
	[branch] if (any(markSpecular.rgb > 0.0))
		markLit += SnowVanillaSpecular(normalWS, V, L, lightColor, lightShadow, markSpecular);
}
