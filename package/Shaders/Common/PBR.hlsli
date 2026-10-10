#ifndef __PBR_DEPENDENCY_HLSL__
#define __PBR_DEPENDENCY_HLSL__
#include "Common/LightingCommon.hlsli"

#include "Common/BRDF.hlsli"
#include "Common/Color.hlsli"
#include "Common/Math.hlsli"
#include "Common/PBRMath.hlsli"
#include "Common/Shading.hlsli"
#include "Common/SharedData.hlsli"

namespace PBR
{
#if defined(GLINT)
	float3 SpecularMicrofacetWithGlint(float noise, float roughness, float3 F0, float NdotL, float NdotV, float NdotH, float VdotH, float glintH,
		float logDensity, float microfacetRoughness, float densityRandomization, Glints::GlintCachedVars glintCache,
		out float3 F)
	{
		float D = BRDF::D_GGX(roughness, NdotH);
		[branch] if (logDensity > 1.1)
		{
			float D_max = BRDF::D_GGX(roughness, 1);
			D = Glints::SampleGlints2023NDF(noise, logDensity, microfacetRoughness, densityRandomization, glintCache, glintH, D, D_max).x;
		}
		float G = BRDF::Vis_SmithJointApprox(roughness, NdotV, NdotL);
		F = BRDF::F_Schlick(F0, VdotH);

		return D * G * F;
	}
#endif

	float3 GetHairDiffuseColorMarschner(float3 N, float3 V, float3 L, float NdotL, float NdotV, float VdotL, float backlit, float area, MaterialProperties material)
	{
		float3 S = 0;

		float cosThetaL = sqrt(max(0, 1 - NdotL * NdotL));
		float cosThetaV = sqrt(max(0, 1 - NdotV * NdotV));
		float cosThetaD = sqrt((1 + cosThetaL * cosThetaV + NdotV * NdotL) / 2.0);

		const float3 Lp = L - NdotL * N;
		const float3 Vp = V - NdotV * N;
		const float cosPhi = dot(Lp, Vp) * rsqrt(dot(Lp, Lp) * dot(Vp, Vp) + EPSILON_DIVISION);
		const float cosHalfPhi = sqrt(saturate(0.5 + 0.5 * cosPhi));

		float n_prime = 1.19 / cosThetaD + 0.36 * cosThetaD;

		const float Shift = 0.0499f;
		const float Alpha[] = {
			-Shift * 2,
			Shift,
			Shift * 4
		};
		float B[] = {
			area + material.Roughness,
			area + material.Roughness / 2,
			area + material.Roughness * 2
		};

		float hairIOR = HairIOR();
		float F0 = IORToF0(hairIOR);

		float3 Tp;
		float Mp, Np, Fp, a, h, f;
		float ThetaH = NdotL + NdotV;
		// R
		Mp = HairGaussian(B[0], ThetaH - Alpha[0]);
		Np = 0.25 * cosHalfPhi;
		Fp = BRDF::F_Schlick(F0, sqrt(saturate(0.5 + 0.5 * VdotL))).x;
		S += (Mp * Np) * (Fp * lerp(1, backlit, saturate(-VdotL)));

		// TT
		Mp = HairGaussian(B[1], ThetaH - Alpha[1]);
		a = (1.55f / hairIOR) * rcp(n_prime);
		h = cosHalfPhi * (1 + a * (0.6 - 0.8 * cosPhi));
		f = BRDF::F_Schlick(F0, cosThetaD * sqrt(saturate(1 - h * h))).x;
		Fp = (1 - f) * (1 - f);
		Tp = pow(abs(material.BaseColor), 0.5 * sqrt(1 - (h * a) * (h * a)) / cosThetaD);
		Np = exp(-3.65 * cosPhi - 3.98);
		S += (Mp * Np) * (Fp * Tp) * backlit;

		// TRT
		Mp = HairGaussian(B[2], ThetaH - Alpha[2]);
		f = BRDF::F_Schlick(F0, cosThetaD * 0.5f).x;
		Fp = (1 - f) * (1 - f) * f;
		Tp = pow(abs(material.BaseColor), 0.8 / cosThetaD);
		Np = exp(17 * cosPhi - 16.78);
		S += (Mp * Np) * (Fp * Tp);

		return S;
	}

	float3 GetHairDiffuseAttenuationKajiyaKay(float3 N, float3 V, float3 L, float NdotL, float NdotV, float shadow, MaterialProperties material)
	{
		float3 S = 0;

		float diffuseKajiya = 1 - abs(NdotL);

		float3 fakeN = normalize(V - N * NdotV);
		const float wrap = 1;
		float wrappedNdotL = saturate((dot(fakeN, L) + wrap) / ((1 + wrap) * (1 + wrap)));
		float diffuseScatter = (1 / Math::PI) * lerp(wrappedNdotL, diffuseKajiya, 0.33);
		float luma = Color::RGBToLuminance(material.BaseColor);
		float3 scatterTint = pow(material.BaseColor / max(luma, 1e-5), 1 - shadow);
		S += sqrt(material.BaseColor) * diffuseScatter * scatterTint;

		return S;
	}

	float3 GetHairColorMarschner(float3 N, float3 V, float3 L, float NdotL, float NdotV, float VdotL, float shadow, float backlit, float area, MaterialProperties material)
	{
		float3 color = 0;

		color += GetHairDiffuseColorMarschner(N, V, L, NdotL, NdotV, VdotL, backlit, area, material);
		color += GetHairDiffuseAttenuationKajiyaKay(N, V, L, NdotL, NdotV, shadow, material);

		return color;
	}

	void GetDirectLightInput(out DirectLightingOutput lightingOutput, DirectContext context, MaterialProperties material, float3x3 tbnTr, float2 uv)
	{
		lightingOutput = (DirectLightingOutput)0;
		const float3 detailedLightColor = context.lightColor * context.detailedShadow;
		const float3 softLightColor = context.lightColor * context.softShadow;

		const float3 N = context.worldNormal;
		const float3 V = context.viewDir;
		const float3 L = context.lightDir;
		const float3 H = context.halfVector;

		const float3 coatN = context.coatWorldNormal;
		const float3 coatV = context.coatViewDir;
		const float3 coatL = context.coatLightDir;
		const float3 coatH = context.coatHalfVector;

		float NdotL = dot(N, L);
		float NdotV = dot(N, V);
		float VdotL = dot(V, L);
		float NdotH = dot(N, H);
		float VdotH = dot(V, H);

		float satNdotL = clamp(NdotL, EPSILON_DOT_CLAMP, 1);
		float satNdotV = saturate(abs(NdotV) + EPSILON_DOT_CLAMP);
		float satVdotL = saturate(VdotL);
		float satNdotH = saturate(NdotH);
		float satVdotH = saturate(VdotH);

#if !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
		[branch] if ((PBRFlags & Flags::HairMarschner) != 0)
		{
			lightingOutput.transmission += softLightColor * GetHairColorMarschner(N, V, L, NdotL, NdotV, VdotL, 0, 1, 0, material);
		}
		else
#endif
		{
			float3 F;  // Fresnel reflectance at current (V,H) angle
#if defined(GLINT)
			float3 Fr = SpecularMicrofacetWithGlint(material.Noise, material.Roughness, material.F0, satNdotL, satNdotV, satNdotH, satVdotH, mul(tbnTr, H).x,
				material.GlintLogMicrofacetDensity, material.GlintMicrofacetRoughness, material.GlintDensityRandomization, material.GlintCache, F);
#else
			float3 Fr = SpecularMicrofacet(material.Roughness, material.F0, satNdotL, satNdotV, satNdotH, satVdotH, F);
#endif
			float3 kD = 1 - F;

			lightingOutput.diffuse += detailedLightColor * satNdotL * BRDF::Diffuse_Lambert() * kD;
			lightingOutput.specular += Fr * detailedLightColor * satNdotL;

#if !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
			[branch] if ((PBRFlags & Flags::Fuzz) != 0)
			{
				float3 fuzzSpecular = SpecularMicroflakes(material.Roughness, material.FuzzColor, satNdotL, satNdotV, satNdotH, satVdotH) * detailedLightColor * satNdotL;
				lightingOutput.specular = lerp(lightingOutput.specular, fuzzSpecular, material.FuzzWeight);
			}

			[branch] if ((PBRFlags & Flags::Subsurface) != 0)
#	if !defined(TREE_ANIM)
			{
				const float subsurfacePower = 12.234;
				float forwardScatter = exp2(saturate(-VdotL) * subsurfacePower - subsurfacePower);
				float backScatter = saturate(satNdotL * material.Thickness + (1.0 - material.Thickness)) * 0.5;
				float subsurface = lerp(backScatter, 1, forwardScatter) * (1.0 - material.Thickness);
				lightingOutput.transmission += material.SubsurfaceColor * subsurface * softLightColor * BRDF::Diffuse_Lambert() * kD;
			}
#	else
			{
				// Diffuse output receives BaseColor at the call site, so split the color multipliers here.
				float3 reflectionTint, transmissionTint;
				GetFoliageSubsurfaceAlbedos(1.0f, saturate(material.SubsurfaceColor), material.Thickness,
					reflectionTint, transmissionTint);
				float3 scatteringThroughput = 1.0f - saturate(SpecularDirectionalAlbedo(material.F0, material.Roughness, satNdotV));
				float3 irradiance = detailedLightColor * BRDF::Diffuse_Lambert() * scatteringThroughput;
				GetFoliageDirectScattering(reflectionTint, transmissionTint, irradiance,
					NdotL, 0.0f, 1.0f, 1.0f, lightingOutput.diffuse, lightingOutput.transmission);
			}
#	endif
			else if ((PBRFlags & Flags::TwoLayer) != 0)
			{
				float coatNdotL = satNdotL;
				float coatNdotV = satNdotV;
				float coatNdotH = satNdotH;
				float coatVdotH = satVdotH;
				[branch] if ((PBRFlags & Flags::CoatNormal) != 0)
				{
					coatNdotL = clamp(dot(coatN, coatL), EPSILON_DOT_CLAMP, 1);
					coatNdotV = saturate(abs(dot(coatN, coatV)) + EPSILON_DOT_CLAMP);
					coatNdotH = saturate(dot(coatN, coatH));
					coatVdotH = saturate(dot(coatV, coatH));
				}

				float3 coatF;
				float3 coatFr = SpecularMicrofacet(material.CoatRoughness, material.CoatF0, coatNdotL, coatNdotV, coatNdotH, coatVdotH, coatF);

				float3 layerAttenuation = 1 - coatF * material.CoatStrength;
				lightingOutput.diffuse *= layerAttenuation;
				lightingOutput.specular *= layerAttenuation;

				lightingOutput.coatDiffuse += context.coatLightColor * coatNdotL * BRDF::Diffuse_Lambert();
				lightingOutput.specular += coatFr * context.coatLightColor * coatNdotL * material.CoatStrength;
			}
#endif
		}

		[branch] if (SharedData::truePBRSettings.EnableMicroShadows != 0)
		{
			float microShadow = lerp(1.0, ApproximateDirectOcculusion(material.AO, NdotL), SharedData::truePBRSettings.MicroShadowStrength);
			lightingOutput.diffuse *= microShadow;
			lightingOutput.specular *= microShadow;
			lightingOutput.coatDiffuse *= microShadow;
		}
	}

	void GetIndirectLobeWeights(out IndirectLobeWeights lobeWeights, IndirectContext context, MaterialProperties material)
	{
		lobeWeights = (IndirectLobeWeights)0;

		const float3 N = context.worldNormal;
		const float3 V = context.viewDir;

		float NdotV = saturate(dot(N, V));

#if !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
		[branch] if ((PBRFlags & Flags::HairMarschner) != 0)
		{
			float3 L = normalize(V - N * dot(V, N));
			float NdotL = dot(N, L);
			float VdotL = dot(V, L);
			lobeWeights.diffuse = GetHairColorMarschner(N, V, L, NdotL, NdotV, VdotL, 1, 0, 0.2, material);
		}
		else
#endif
		{
			lobeWeights.diffuse = material.BaseColor;

#if !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
			[branch] if ((PBRFlags & Flags::Subsurface) != 0)
			{
#	if defined(TREE_ANIM)
				GetFoliageSubsurfaceAlbedos(material.BaseColor, saturate(material.SubsurfaceColor), material.Thickness,
					lobeWeights.diffuse, lobeWeights.transmission);
#	else
				lobeWeights.diffuse += material.SubsurfaceColor * (1 - material.Thickness) / Math::PI;
#	endif
			}
			[branch] if ((PBRFlags & Flags::Fuzz) != 0)
			{
				lobeWeights.diffuse += material.FuzzColor * material.FuzzWeight;
			}
#endif
			float2 specularBRDF = BRDF::EnvBRDF(material.Roughness, NdotV);
			lobeWeights.specular = material.F0 * specularBRDF.x + specularBRDF.y;

			// Energy conservation: diffuse receives only what specular does not reflect
			lobeWeights.diffuse *= 1 - lobeWeights.specular;
#if defined(TREE_ANIM)
			lobeWeights.transmission *= 1 - lobeWeights.specular;
#endif

#if !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
			[branch] if ((PBRFlags & Flags::TwoLayer) != 0)
			{
				float2 coatSpecularBRDF = BRDF::EnvBRDF(material.CoatRoughness, NdotV);
				float3 coatSpecularLobeSpecular = material.CoatF0 * coatSpecularBRDF.x + coatSpecularBRDF.y;

				float3 layerAttenuation = 1 - coatSpecularLobeSpecular * material.CoatStrength;
				lobeWeights.diffuse *= layerAttenuation;
				lobeWeights.specular *= layerAttenuation;

				[branch] if ((PBRFlags & Flags::ColoredCoat) != 0)
				{
					float3 coatDiffuseLobeWeight = material.CoatColor * (1 - coatSpecularLobeSpecular);
					lobeWeights.diffuse += coatDiffuseLobeWeight * material.CoatStrength;
				}
				lobeWeights.specular += coatSpecularLobeSpecular * material.CoatStrength;
			}
#endif
		}

		// Apply ambient occlusion with multi-bounce approximation
#if defined(TREE_ANIM)
		float3 diffuseAO = MultiBounceAO(material.BaseColor, material.AO);
		// The AO fit can slightly exceed one; keep the thin-surface scattering budget bounded.
		[branch] if ((PBRFlags & Flags::Subsurface) != 0)
			diffuseAO = saturate(diffuseAO);
		lobeWeights.diffuse *= diffuseAO;
		lobeWeights.transmission *= diffuseAO;
#else
		lobeWeights.diffuse *= MultiBounceAO(material.BaseColor, material.AO);
#endif
		float alpha = material.Roughness * material.Roughness;
		lobeWeights.specular *= SpecularOcclusion(NdotV, alpha, material.AO);
	}

#if defined(GRASS_LIGHTING)
	struct GrassSurface
	{
		float3 reflectionAlbedo;
		float3 transmissionAlbedo;
		float3 specularAlbedo;
		float3 fuzzColor;
		float fuzzAlbedo;
		float fuzzRoughness;
	};

	/** @brief Prepares the scattering budget and OpenPBR sheen once for all lights. */
	GrassSurface CreateGrassSurface(MaterialProperties material, float NdotV, bool doSpecular)
	{
		GrassSurface surface = (GrassSurface)0;
		surface.reflectionAlbedo = saturate(material.BaseColor);
		[branch] if ((PBRFlags & Flags::Subsurface) != 0)
		{
			GetFoliageSubsurfaceAlbedos(surface.reflectionAlbedo, saturate(material.SubsurfaceColor), material.Thickness,
				surface.reflectionAlbedo, surface.transmissionAlbedo);
		}

		[branch] if (doSpecular)
		{
			surface.specularAlbedo = saturate(SpecularDirectionalAlbedo(material.F0, material.Roughness, NdotV));
			[branch] if ((PBRFlags & Flags::Fuzz) != 0)
			{
				surface.fuzzRoughness = clamp(material.Roughness, 0.01f, 1.0f);
				surface.fuzzAlbedo = saturate(saturate(material.FuzzWeight) * FuzzDirectionalAlbedo(NdotV, surface.fuzzRoughness));
				surface.fuzzColor = saturate(material.FuzzColor);
			}
		}
		return surface;
	}

	void GetDirectLightInputGrass(out DirectLightingOutput lightingOutput, DirectContext context, MaterialProperties material, GrassSurface surface, bool doSpecular)
	{
		lightingOutput = (DirectLightingOutput)0;
		const float3 detailedLightColor = context.lightColor * context.detailedShadow;

		const float3 N = context.worldNormal;
		const float3 V = context.viewDir;
		const float3 L = context.lightDir;

		float NdotL = dot(N, L);
		float satNdotL = saturate(NdotL);
		float NdotV = clamp(dot(N, V), EPSILON_DOT_CLAMP, 1.0f);
		float fuzzThroughput = 1.0f - surface.fuzzAlbedo;
		float3 scatteringThroughput = (1.0f - surface.specularAlbedo) * fuzzThroughput;

		float wrap = saturate(SharedData::grassLightingSettings.PBRWrappedLightingAmount);
		float3 irradiance = context.lightColor * BRDF::Diffuse_Lambert() * scatteringThroughput;
		GetFoliageDirectScattering(surface.reflectionAlbedo, surface.transmissionAlbedo, irradiance,
			NdotL, wrap, context.detailedShadow, context.softShadow, lightingOutput.diffuse, lightingOutput.transmission);

		float3 F = 0;
		[branch] if (doSpecular)
		{
			const float3 H = context.halfVector;
			float satNdotH = saturate(dot(N, H));
			float satVdotH = saturate(dot(V, H));
			float3 Fr = SpecularMicrofacet(material.Roughness, material.F0, max(satNdotL, EPSILON_DOT_CLAMP), NdotV, satNdotH, satVdotH, F);
			lightingOutput.specular = Fr * detailedLightColor * satNdotL * fuzzThroughput;
			[branch] if (surface.fuzzAlbedo > 0.0f)
			{
				// Back light reaches the viewer-side fuzz through the blade, using the same transmission budget.
				float3 fuzzReflection = surface.fuzzAlbedo * surface.fuzzColor *
				                        (FuzzLobe(L, V, N, NdotV, surface.fuzzRoughness) +
											surface.transmissionAlbedo * FuzzLobeTransmitted(L, V, N, NdotV, surface.fuzzRoughness));
				lightingOutput.specular += fuzzReflection * detailedLightColor;
			}
		}
	}

	void GetIndirectLobeWeightsGrass(out IndirectLobeWeights lobeWeights, out float3 transmissionWeight, IndirectContext context, MaterialProperties material, GrassSurface surface)
	{
		lobeWeights = (IndirectLobeWeights)0;
		float3 scatteringThroughput = MultiBounceAO(material.BaseColor, material.AO) *
		                              (1.0f - surface.specularAlbedo) * (1.0f - surface.fuzzAlbedo);
		lobeWeights.diffuse = surface.reflectionAlbedo * scatteringThroughput + surface.fuzzAlbedo * surface.fuzzColor * material.AO;
		transmissionWeight = surface.transmissionAlbedo * scatteringThroughput;
		float NdotV = clamp(dot(context.worldNormal, context.viewDir), EPSILON_DOT_CLAMP, 1.0f);
		lobeWeights.specular = surface.specularAlbedo * (1.0f - surface.fuzzAlbedo) *
		                       SpecularOcclusion(NdotV, material.Roughness * material.Roughness, material.AO);
	}
#endif
}

#endif  // __PBR_DEPENDENCY_HLSL__
