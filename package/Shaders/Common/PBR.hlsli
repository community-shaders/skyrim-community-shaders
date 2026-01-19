#ifndef __PBR_DEPENDENCY_HLSL__
#define __PBR_DEPENDENCY_HLSL__
#include "Common/LightingCommon.hlsli"

#include "Common/BRDF.hlsli"
#include "Common/Color.hlsli"
#include "Common/Math.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/PBRMath.hlsli"

namespace PBR
{
#if defined(GLINT)
	float3 GetSpecularDirectLightMultiplierMicrofacetWithGlint(float noise, float roughness, float3 specularColor, float NdotL, float NdotV, float NdotH, float VdotH, float glintH,
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
		F = BRDF::F_Schlick(specularColor, VdotH);

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
		float specularColor = IORToF0(hairIOR);

		float3 Tp;
		float Mp, Np, Fp, a, h, f;
		float ThetaH = NdotL + NdotV;
		// R
		Mp = HairGaussian(B[0], ThetaH - Alpha[0]);
		Np = 0.25 * cosHalfPhi;
		Fp = BRDF::F_Schlick(specularColor, sqrt(saturate(0.5 + 0.5 * VdotL))).x;
		S += (Mp * Np) * (Fp * lerp(1, backlit, saturate(-VdotL)));

		// TT
		Mp = HairGaussian(B[1], ThetaH - Alpha[1]);
		a = (1.55f / hairIOR) * rcp(n_prime);
		h = cosHalfPhi * (1 + a * (0.6 - 0.8 * cosPhi));
		f = BRDF::F_Schlick(specularColor, cosThetaD * sqrt(saturate(1 - h * h))).x;
		Fp = (1 - f) * (1 - f);
		Tp = pow(abs(material.BaseColor), 0.5 * sqrt(1 - (h * a) * (h * a)) / cosThetaD);
		Np = exp(-3.65 * cosPhi - 3.98);
		S += (Mp * Np) * (Fp * Tp) * backlit;

		// TRT
		Mp = HairGaussian(B[2], ThetaH - Alpha[2]);
		f = BRDF::F_Schlick(specularColor, cosThetaD * 0.5f).x;
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
		float3 scatterTint = pow(material.BaseColor / luma, 1 - shadow);
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
			lightingOutput.transmission += context.lightColor * GetHairColorMarschner(N, V, L, NdotL, NdotV, VdotL, 0, 1, 0, material);
		}
		else
#endif
		{
			lightingOutput.diffuse += context.lightColor * satNdotL * BRDF::Diffuse_Lambert();

			float3 F;
#if defined(GLINT)
			lightingOutput.specular += GetSpecularDirectLightMultiplierMicrofacetWithGlint(material.Noise, material.Roughness, material.F0, satNdotL, satNdotV, satNdotH, satVdotH, mul(tbnTr, H).x,
							material.GlintLogMicrofacetDensity, material.GlintMicrofacetRoughness, material.GlintDensityRandomization, material.GlintCache, F) *
			            context.lightColor * satNdotL;
#else
			lightingOutput.specular += GetSpecularDirectLightMultiplierMicrofacet(material.Roughness, material.F0, satNdotL, satNdotV, satNdotH, satVdotH, F) * context.lightColor * satNdotL;
#endif

			float2 specularBRDF = BRDF::EnvBRDF(material.Roughness, satNdotV);
			lightingOutput.specular *= 1 + material.F0 * (1 / (specularBRDF.x + specularBRDF.y) - 1);

#if !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
			[branch] if ((PBRFlags & Flags::Fuzz) != 0)
			{
				float3 fuzzSpecular = GetSpecularDirectLightMultiplierMicroflakes(material.Roughness, material.FuzzColor, satNdotL, satNdotV, satNdotH, satVdotH) * context.lightColor * satNdotL;
				fuzzSpecular *= 1 + material.FuzzColor * (1 / (specularBRDF.x + specularBRDF.y) - 1);

				lightingOutput.specular = lerp(lightingOutput.specular, fuzzSpecular, material.FuzzWeight);
			}

			[branch] if ((PBRFlags & Flags::Subsurface) != 0)
			{
				const float subsurfacePower = 12.234;
				float forwardScatter = exp2(saturate(-VdotL) * subsurfacePower - subsurfacePower);
				float backScatter = saturate(satNdotL * material.Thickness + (1.0 - material.Thickness)) * 0.5;
				float subsurface = lerp(backScatter, 1, forwardScatter) * (1.0 - material.Thickness);
				lightingOutput.transmission += material.SubsurfaceColor * subsurface * context.lightColor * BRDF::Diffuse_Lambert();
			}
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
				float3 coatSpecular = GetSpecularDirectLightMultiplierMicrofacet(material.CoatRoughness, material.CoatF0, coatNdotL, coatNdotV, coatNdotH, coatVdotH, coatF) * context.coatLightColor * coatNdotL;

				float3 layerAttenuation = 1 - coatF * material.CoatStrength;
				lightingOutput.diffuse *= layerAttenuation;
				lightingOutput.specular *= layerAttenuation;

				lightingOutput.coatDiffuse += context.coatLightColor * coatNdotL * BRDF::Diffuse_Lambert();
				lightingOutput.specular += coatSpecular * material.CoatStrength;
			}
#endif
		}
	}

	void GetIndirectLobeWeights(out IndirectLobeWeights lobeWeights, IndirectContext context, MaterialProperties material)
	{
		lobeWeights = (IndirectLobeWeights)0;

		const float3 N = context.worldNormal;
		const float3 V = context.viewDir;
		const float3 VN = context.vertexNormal;

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
				lobeWeights.diffuse += material.SubsurfaceColor * (1 - material.Thickness) / Math::PI;
			}
			[branch] if ((PBRFlags & Flags::Fuzz) != 0)
			{
				lobeWeights.diffuse += material.FuzzColor * material.FuzzWeight;
			}
#endif

			float2 specularBRDF = BRDF::EnvBRDF(material.Roughness, NdotV);
			lobeWeights.specular = material.F0 * specularBRDF.x + specularBRDF.y;

			lobeWeights.diffuse *= (1 - lobeWeights.specular);
			lobeWeights.specular *= 1 + material.F0 * (1 / (specularBRDF.x + specularBRDF.y) - 1);

#if !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
			[branch] if ((PBRFlags & Flags::TwoLayer) != 0)
			{
				float2 coatSpecularBRDF = BRDF::EnvBRDF(material.CoatRoughness, NdotV);
				float3 coatSpecularLobeWeight = material.CoatF0 * coatSpecularBRDF.x + coatSpecularBRDF.y;
				coatSpecularLobeWeight *= 1 + material.CoatF0 * (1 / (coatSpecularBRDF.x + coatSpecularBRDF.y) - 1);

				float3 coatF = BRDF::F_Schlick(material.CoatF0, NdotV);

				float3 layerAttenuation = 1 - coatF * material.CoatStrength;
				lobeWeights.diffuse *= layerAttenuation;
				lobeWeights.specular *= layerAttenuation;

				[branch] if ((PBRFlags & Flags::ColoredCoat) != 0)
				{
					float3 coatDiffuseLobeWeight = material.CoatColor * (1 - coatSpecularLobeWeight);
					lobeWeights.diffuse += coatDiffuseLobeWeight * material.CoatStrength;
				}
				lobeWeights.specular += coatSpecularLobeWeight * material.CoatStrength;
			}
#endif
		}

		// Horizon specular occlusion
		// https://marmosetco.tumblr.com/post/81245981087
		float3 R = reflect(-V, N);
		float horizon = min(1.0 + dot(R, VN), 1.0);
		horizon = horizon * horizon;
		lobeWeights.specular *= horizon;

		float3 diffuseAO = material.AO;
		float3 specularAO = Color::SpecularAOLagarde(NdotV, material.AO, material.Roughness);

		diffuseAO = Color::MultiBounceAO(material.BaseColor, diffuseAO.x).y;
		specularAO = Color::MultiBounceAO(material.F0, specularAO.x).y;

		lobeWeights.diffuse *= diffuseAO;
		lobeWeights.specular *= specularAO;
	}
}

#endif  // __PBR_DEPENDENCY_HLSL__