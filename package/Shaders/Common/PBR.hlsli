#ifndef __PBR_DEPENDENCY_HLSL__
#define __PBR_DEPENDENCY_HLSL__
#include "Common/LightingCommon.hlsli"

#include "Common/BRDF.hlsli"
#include "Common/Color.hlsli"
#include "Common/LightDisk.hlsli"
#include "Common/Math.hlsli"
#include "Common/PBRMath.hlsli"
#include "Common/Shading.hlsli"
#include "Common/SharedData.hlsli"

namespace PBR
{
#if defined(GLINT)
	float3 SpecularMicrofacetWithGlint(float noise, float roughness, float3 F0, float NdotL, float NdotV, float NdotH, float VdotH, float3 glintH,
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

	float3 GetHairDiffuseColorMarschner(float3 N, float3 V, float3 L, float NdotL, float NdotV, float VdotL, float backlit, float area, MaterialProperties material, float2 disk)
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
		float3 B = area + material.Roughness * float3(1.0, 0.5, 2.0);
		// The longitudinal Gaussians are parameterized by the sum of the two tangent cosines.
		B = sqrt(B * B + LightDisk::CosineVariance(NdotL, disk));

		float hairIOR = HairIOR();
		float F0 = IORToF0(hairIOR);

		float3 Tp;
		float Mp, Np, Fp, a, h, f;
		float ThetaH = LightDisk::MeanCosine(NdotL, disk) + NdotV;
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

	float3 GetHairDiffuseAttenuationKajiyaKay(float3 N, float3 V, float3 L, float NdotL, float NdotV, float shadow, MaterialProperties material, float2 disk)
	{
		float3 S = 0;

		float diffuseKajiya = 1.0 - LightDisk::MeanAbsCosine(NdotL, disk);

		float3 fakeN = normalize(V - N * NdotV);
		const float wrap = 1;
		float wrappedNdotL = LightDisk::WrappedCosine(dot(fakeN, L), wrap, 0.25, disk);
		float diffuseScatter = (1 / Math::PI) * lerp(wrappedNdotL, diffuseKajiya, 0.33);
		float luma = Color::RGBToLuminance(material.BaseColor);
		float3 scatterTint = pow(material.BaseColor / max(luma, 1e-5), 1 - shadow);
		S += sqrt(material.BaseColor) * diffuseScatter * scatterTint;

		return S;
	}

	float3 GetHairColorMarschner(float3 N, float3 V, float3 L, float NdotL, float NdotV, float VdotL, float shadow, float backlit, float area, MaterialProperties material, float2 disk = float2(0.0, 1.0))
	{
		float3 color = 0;

		color += GetHairDiffuseColorMarschner(N, V, L, NdotL, NdotV, VdotL, backlit, area, material, disk);
		color += GetHairDiffuseAttenuationKajiyaKay(N, V, L, NdotL, NdotV, shadow, material, disk);

		return color;
	}

#if defined(TRUE_PBR)
	/// @brief Resolve the fuzz layer for a view angle
	/// @param material Material with FuzzColor, FuzzWeight and FuzzRoughness (negative uses Roughness)
	/// @param NdotV Dot product of normal and view direction
	/// @param enabled False returns an empty layer that leaves the substrate unchanged
	/// @return Layer with no cached LTC; call CacheFuzzLTC before evaluating direct lights
	FuzzLayer CreateFuzzLayer(MaterialProperties material, float NdotV, bool enabled = true)
	{
		FuzzLayer fuzz = (FuzzLayer)0;
		fuzz.specularThroughput = 1.0f;

		[branch] if (enabled && (PBRFlags & Flags::Fuzz) != 0)
		{
			fuzz.roughness = clamp(material.FuzzRoughness >= 0.0f ? material.FuzzRoughness : material.Roughness, 0.01f, 1.0f);
			fuzz.weight = saturate(material.FuzzWeight);
			fuzz.weightedAlbedo = saturate(fuzz.weight * FuzzDirectionalAlbedo(NdotV, fuzz.roughness));
			fuzz.color = saturate(material.FuzzColor);
			// Without a fuzz roughness (legacy materials) the fuzz mask replaces substrate specular, as the old sheen did.
			fuzz.specularThroughput = material.FuzzRoughness < 0.0f ? 1.0f - fuzz.weight : 1.0f - fuzz.weightedAlbedo;
		}

		return fuzz;
	}

	/// @brief Cache the view-dependent fuzz LTC for direct lighting; skipped for an invisible layer
	/// @param N Surface normal, flipped to face the viewer
	/// @param V View direction
	/// @param NdotV Dot product of normal and view direction
	void CacheFuzzLTC(inout FuzzLayer fuzz, float3 N, float3 V, float NdotV)
	{
		[branch] if (fuzz.weightedAlbedo > 0.0f)
			fuzz.ltc = CreateFuzzLTC(V, GetThinSurfaceNormal(N, V), max(NdotV, EPSILON_DOT_CLAMP), fuzz.roughness);
	}

	struct ThinSurface
	{
		float3 reflectionAlbedo;
		float3 frontTransmissionAlbedo;
		float3 backTransmissionAlbedo;
		float3 specularAlbedo;
	};

	ThinSurface CreateThinSurface(MaterialProperties material, float NdotV, bool doSpecular, bool reflectionIncludesBaseColor = true)
	{
		ThinSurface surface = (ThinSurface)0;
		surface.reflectionAlbedo = reflectionIncludesBaseColor ? saturate(material.BaseColor) : 1.0f;

		[branch] if ((PBRFlags & Flags::Subsurface) != 0)
		{
			float reflectionWeight;
			GetThinSurfaceSubsurfaceAlbedos(saturate(material.BaseColor), saturate(material.SubsurfaceColor), material.Thickness, material.SubsurfaceBalance,
				reflectionWeight, surface.frontTransmissionAlbedo, surface.backTransmissionAlbedo);
			surface.reflectionAlbedo *= reflectionWeight;
		}

		[branch] if (doSpecular)
		{
			surface.specularAlbedo = saturate(material.ThinSurfaceSpecularWeight) * saturate(SpecularDirectionalAlbedo(material.F0, material.Roughness, NdotV));
		}

		return surface;
	}

	void GetDirectLightInputThinSurface(out DirectLightingOutput lightingOutput, DirectContext context, MaterialProperties material,
		ThinSurface surface, bool doSpecular, float wrap, float3 glintH = 0.0f)
	{
		lightingOutput = (DirectLightingOutput)0;

		float3 N = context.worldNormal;
		float3 V = context.viewDir;
		float3 L = context.lightDir;

		float NdotL = dot(N, L);
		float3 specularNormal = GetThinSurfaceNormal(N, V);
		float NdotV = clamp(dot(specularNormal, V), EPSILON_DOT_CLAMP, 1.0f);
		float specularNdotL = saturate(dot(specularNormal, L));

		float fuzzThroughput = 1.0f - material.Fuzz.weightedAlbedo;
		float3 scatteringThroughput = (1.0f - surface.specularAlbedo) * Color::LinearToPBRMaterial(fuzzThroughput.xxx);

		float3 irradiance = context.lightColor * BRDF::Diffuse_Lambert() * scatteringThroughput;
		float3 shineThroughAlbedo = (PBRFlags & Flags::Subsurface) != 0 ? material.BaseColor : 0.0f;

		GetThinSurfaceDirectScattering(surface.reflectionAlbedo, surface.frontTransmissionAlbedo, surface.backTransmissionAlbedo, shineThroughAlbedo,
			material.Thickness, irradiance, NdotL, wrap, context.detailedShadow, float2(context.softShadow, context.softShadow * context.backTransmissionVisibility),
			lightingOutput.diffuse, lightingOutput.transmission, context.lightDisk);

		[branch] if (doSpecular)
		{
			float3 specularLight = LightDisk::SpecularDirection(specularNormal, V, L, context.lightDisk);
			float3 H = normalize(V + specularLight);
			specularNdotL = saturate(dot(specularNormal, specularLight));
			float NdotH = saturate(dot(specularNormal, H));
			float VdotH = saturate(dot(V, H));
			float D = BRDF::D_GGX(material.Roughness, NdotH);

#	if defined(GLINT)
			[branch] if (material.GlintLogMicrofacetDensity > 1.1f)
			{
				float Dmax = BRDF::D_GGX(material.Roughness, 1.0f);
				D = Glints::SampleGlints2023NDF(material.Noise, material.GlintLogMicrofacetDensity, material.GlintMicrofacetRoughness,
					material.GlintDensityRandomization, material.GlintCache, glintH, D, Dmax)
				        .x;
			}
#	endif
			D *= LightDisk::GGXNormalization(material.Roughness, context.lightDisk);
			float visibility = BRDF::Vis_SmithJoint(material.Roughness, NdotV, max(specularNdotL, EPSILON_DOT_CLAMP));
			float3 F = BRDF::F_Schlick(material.F0, VdotH);
			float3 detailedLightColor = context.lightColor * context.detailedShadow;
			lightingOutput.specular = saturate(material.ThinSurfaceSpecularWeight) * D * visibility * F * detailedLightColor * specularNdotL * material.Fuzz.specularThroughput;
			[branch] if (material.Fuzz.weightedAlbedo > 0.0f)
			{
				float3 fuzzReflection = FuzzLobe(L, material.Fuzz.ltc, context.lightDisk) * detailedLightColor;
				float3 fuzzTransmission = Color::PBRMaterialToLinear(surface.backTransmissionAlbedo) * FuzzLobeTransmitted(L, material.Fuzz.ltc, context.lightDisk) *
				                          context.lightColor * context.softShadow * context.backTransmissionVisibility;
				lightingOutput.specular += material.Fuzz.weightedAlbedo * material.Fuzz.color * (fuzzReflection + fuzzTransmission);
			}
		}
	}

	void GetIndirectLobeWeightsThinSurface(out IndirectLobeWeights lobeWeights, IndirectContext context, MaterialProperties material, ThinSurface surface)
	{
		lobeWeights = (IndirectLobeWeights)0;
		float3 diffuseAO = MultiBounceAO(material.BaseColor, material.AO);

		if (IsThinSurfaceMaterial(PBRFlags))
			diffuseAO = saturate(diffuseAO);

		float fuzzThroughput = 1.0f - material.Fuzz.weightedAlbedo;
		float3 substrateThroughput = diffuseAO * (1.0f - surface.specularAlbedo);
		float3 scatteringThroughput = substrateThroughput * Color::LinearToPBRMaterial(fuzzThroughput.xxx);
		lobeWeights.diffuse = Color::LinearToPBRMaterial(Color::PBRMaterialToLinear(surface.reflectionAlbedo * substrateThroughput) * fuzzThroughput +
														 material.Fuzz.weightedAlbedo * material.Fuzz.color * material.AO);
		lobeWeights.frontTransmission = surface.frontTransmissionAlbedo * scatteringThroughput;
		lobeWeights.backTransmission = surface.backTransmissionAlbedo * scatteringThroughput;

		float NdotV = clamp(abs(dot(context.worldNormal, context.viewDir)), EPSILON_DOT_CLAMP, 1.0f);
		lobeWeights.specular = surface.specularAlbedo * material.Fuzz.specularThroughput *
		                       SpecularOcclusion(NdotV, material.Roughness * material.Roughness, material.AO);
	}
#endif

	float GetDirectSpecularNdotV(float NdotV)
	{
#if defined(TREE_ANIM)
		return clamp(NdotV, EPSILON_DOT_CLAMP, 1.0f);
#else
		return saturate(abs(NdotV) + EPSILON_DOT_CLAMP);
#endif
	}

	void GetDirectLightInput(out DirectLightingOutput lightingOutput, DirectContext context, MaterialProperties material, float3x3 tbnTr, float2 uv)
	{
		lightingOutput = (DirectLightingOutput)0;

#if defined(TRUE_PBR)
		if (IsThinSurfaceMaterial(PBRFlags)) {
			float NdotV = saturate(abs(dot(context.worldNormal, context.viewDir)));

			ThinSurface surface = CreateThinSurface(material, NdotV, true, false);

			GetDirectLightInputThinSurface(lightingOutput, context, material, surface, true, 0.0f, mul(tbnTr, context.halfVector));

			[branch] if (SharedData::truePBRSettings.EnableMicroShadows != 0)
			{
				float microShadow = lerp(1.0f, ApproximateDirectOcculusion(material.AO, dot(context.worldNormal, context.lightDir)), SharedData::truePBRSettings.MicroShadowStrength);
				lightingOutput.diffuse *= microShadow;
				lightingOutput.specular *= microShadow;
			}

			return;
		}
#endif

		const float3 detailedLightColor = context.lightColor * context.detailedShadow;
		const float3 softLightColor = context.lightColor * context.softShadow;

		const float3 N = context.worldNormal;
		const float3 V = context.viewDir;
		const float3 L = context.lightDir;

#if defined(TRUE_PBR)
		// Coat members exist only on the TRUE_PBR DirectContext.
		const float3 coatN = context.coatWorldNormal;
		const float3 coatV = context.coatViewDir;
		const float3 coatL = context.coatLightDir;
#endif

		float NdotL = dot(N, L);
		float NdotV = dot(N, V);
		float VdotL = dot(V, L);

		float satNdotL = clamp(NdotL, EPSILON_DOT_CLAMP, 1);
		float satVdotL = saturate(VdotL);

#if defined(TREE_ANIM)
		float specularNdotL = saturate(NdotL);
#else
		float specularNdotL = satNdotL;
#endif
		float specularNdotV = GetDirectSpecularNdotV(NdotV);

#if defined(TRUE_PBR) && !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
		[branch] if ((PBRFlags & Flags::HairMarschner) != 0)
		{
			lightingOutput.transmission += softLightColor * GetHairColorMarschner(N, V, L, NdotL, NdotV, VdotL, 0, 1, 0, material, context.lightDisk);
		}
		else
#endif
		{
			const float3 specularLight = LightDisk::SpecularDirection(N, V, L, context.lightDisk);
			const float3 specularH = normalize(V + specularLight);
			const float diskNdotH = saturate(dot(N, specularH));
			const float diskVdotH = saturate(dot(V, specularH));
#if defined(TREE_ANIM)
			specularNdotL = saturate(dot(N, specularLight));
#else
			specularNdotL = clamp(dot(N, specularLight), EPSILON_DOT_CLAMP, 1.0f);
#endif
			float3 F;  // Fresnel reflectance at current (V,H) angle
#if defined(GLINT)
			float3 Fr = SpecularMicrofacetWithGlint(material.Noise, material.Roughness, material.F0, max(specularNdotL, EPSILON_DOT_CLAMP), specularNdotV, diskNdotH, diskVdotH, mul(tbnTr, specularH),
				material.GlintLogMicrofacetDensity, material.GlintMicrofacetRoughness, material.GlintDensityRandomization, material.GlintCache, F);
#else
			float3 Fr = SpecularMicrofacet(material.Roughness, material.F0, max(specularNdotL, EPSILON_DOT_CLAMP), specularNdotV, diskNdotH, diskVdotH, F);
#endif
			Fr *= LightDisk::GGXNormalization(material.Roughness, context.lightDisk);
#if defined(TREE_ANIM)
			float3 kD = 1.0f - saturate(SpecularDirectionalAlbedo(material.F0, material.Roughness, saturate(abs(NdotV))));
#else
			float3 kD = 1 - F;
#endif

			lightingOutput.diffuse += detailedLightColor * max(LightDisk::DiffuseCosine(NdotL, context.lightDisk), EPSILON_DOT_CLAMP) * BRDF::Diffuse_Lambert() * kD;
			lightingOutput.specular += Fr * material.DirectSpecularEnergyCompensation * detailedLightColor * specularNdotL;

#if defined(TRUE_PBR) && !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
#	if !defined(TREE_ANIM)
			[branch] if ((PBRFlags & Flags::Subsurface) != 0)
			{
				const float subsurfacePower = 12.234;
				float forwardScatter = exp2(saturate(-VdotL) * subsurfacePower - subsurfacePower);
				if (context.lightDisk.x > 0.0) {
					const float3 cosine = saturate(LightDisk::CosineSamples(-VdotL, context.lightDisk));
					forwardScatter = dot(exp2((cosine - 1.0) * subsurfacePower), float3(0.25, 0.5, 0.25));
				}
				const float incidentCosine = max(LightDisk::DiffuseCosine(NdotL, context.lightDisk), EPSILON_DOT_CLAMP);
				float backScatter = saturate(incidentCosine * material.Thickness + (1.0 - material.Thickness)) * 0.5;
				float subsurface = lerp(backScatter, 1, forwardScatter) * (1.0 - material.Thickness);
				float3 transmissionKD = 1.0f - saturate(SpecularDirectionalAlbedo(material.F0, material.Roughness, saturate(abs(NdotV))));
				lightingOutput.transmission += material.SubsurfaceColor * subsurface * softLightColor * BRDF::Diffuse_Lambert() * transmissionKD;
			}
			else
#	endif
				if ((PBRFlags & Flags::TwoLayer) != 0)
			{
				float coatNdotL = max(specularNdotL, EPSILON_DOT_CLAMP);
				float coatNdotV = saturate(abs(NdotV) + EPSILON_DOT_CLAMP);
				float coatDiffuseCosine = LightDisk::DiffuseCosine(NdotL, context.lightDisk);
				float coatNdotH = diskNdotH;
				float coatVdotH = diskVdotH;
				[branch] if ((PBRFlags & Flags::CoatNormal) != 0)
				{
					coatDiffuseCosine = LightDisk::DiffuseCosine(dot(coatN, coatL), context.lightDisk);
					const float3 coatSpecularLight = LightDisk::SpecularDirection(coatN, coatV, coatL, context.lightDisk);
					const float3 coatSpecularH = normalize(coatV + coatSpecularLight);
					coatNdotL = clamp(dot(coatN, coatSpecularLight), EPSILON_DOT_CLAMP, 1);
					coatNdotV = saturate(abs(dot(coatN, coatV)) + EPSILON_DOT_CLAMP);
					coatNdotH = saturate(dot(coatN, coatSpecularH));
					coatVdotH = saturate(dot(coatV, coatSpecularH));
				}

				float3 coatF;
				float3 coatFr = SpecularMicrofacet(material.CoatRoughness, material.CoatF0, coatNdotL, coatNdotV, coatNdotH, coatVdotH, coatF);

				coatFr *= LightDisk::GGXNormalization(material.CoatRoughness, context.lightDisk);

				float3 layerAttenuation = 1 - coatF * material.CoatStrength;
				lightingOutput.diffuse *= layerAttenuation;
				lightingOutput.specular *= layerAttenuation;

				lightingOutput.coatDiffuse += context.coatLightColor * max(coatDiffuseCosine, EPSILON_DOT_CLAMP) * BRDF::Diffuse_Lambert();
				lightingOutput.specular += coatFr * context.coatLightColor * coatNdotL * material.CoatStrength;
			}

			[branch] if ((PBRFlags & Flags::Fuzz) != 0)
			{
				float throughput = 1.0f - material.Fuzz.weightedAlbedo;
				float3 diffuseThroughput = Color::LinearToPBRMaterial(throughput.xxx);

				lightingOutput.diffuse *= diffuseThroughput;
				lightingOutput.specular *= material.Fuzz.specularThroughput;
				lightingOutput.transmission *= diffuseThroughput;
				lightingOutput.coatDiffuse *= diffuseThroughput;

				// An invisible layer has no cached LTC, and evaluating the zeroed one is 0 / 0.
				[branch] if (material.Fuzz.weightedAlbedo > 0.0f)
					lightingOutput.specular += material.Fuzz.weightedAlbedo * material.Fuzz.color * FuzzLobe(L, material.Fuzz.ltc, context.lightDisk) * detailedLightColor;
			}
#endif
		}

		[branch] if (SharedData::truePBRSettings.EnableMicroShadows != 0)
		{
			float microShadow = lerp(1.0, ApproximateDirectOcculusion(material.AO, NdotL), SharedData::truePBRSettings.MicroShadowStrength);
			lightingOutput.diffuse *= microShadow;
			lightingOutput.specular *= microShadow;
#if defined(TRUE_PBR)
			lightingOutput.coatDiffuse *= microShadow;
#endif
		}
	}

	void GetIndirectLobeWeights(out IndirectLobeWeights lobeWeights, IndirectContext context, MaterialProperties material)
	{
		lobeWeights = (IndirectLobeWeights)0;

#if defined(TRUE_PBR)
		if (IsThinSurfaceMaterial(PBRFlags)) {
			float NdotV = saturate(abs(dot(context.worldNormal, context.viewDir)));
			ThinSurface surface = CreateThinSurface(material, NdotV, true);

			GetIndirectLobeWeightsThinSurface(lobeWeights, context, material, surface);

			return;
		}
#endif

		const float3 N = context.worldNormal;
		const float3 V = context.viewDir;

#if defined(TREE_ANIM)
		float NdotV = saturate(dot(GetThinSurfaceNormal(N, V), V));
#else
		float NdotV = saturate(dot(N, V));
#endif

#if defined(TRUE_PBR) && !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
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

#if defined(TRUE_PBR) && !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
#	if !defined(TREE_ANIM)
			[branch] if ((PBRFlags & Flags::Subsurface) != 0)
			{
				lobeWeights.diffuse += material.SubsurfaceColor * (1 - material.Thickness) / Math::PI;
			}
#	endif
#endif
#if defined(TREE_ANIM)
			lobeWeights.specular = saturate(SpecularDirectionalAlbedo(material.F0, material.Roughness, NdotV));
#else
			lobeWeights.specular = SpecularDirectionalAlbedo(material.F0, material.Roughness, NdotV);
#endif

			// Energy conservation: diffuse receives only what specular does not reflect
			lobeWeights.diffuse *= 1 - lobeWeights.specular;

#if defined(TRUE_PBR) && !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
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
		lobeWeights.diffuse *= MultiBounceAO(material.BaseColor, material.AO);
		float alpha = material.Roughness * material.Roughness;
		lobeWeights.specular *= SpecularOcclusion(NdotV, alpha, material.AO);

#if defined(TRUE_PBR) && !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
		[branch] if ((PBRFlags & Flags::Fuzz) != 0 && (PBRFlags & Flags::HairMarschner) == 0)
		{
			lobeWeights.diffuse = Color::LinearToPBRMaterial(Color::PBRMaterialToLinear(lobeWeights.diffuse) * (1.0f - material.Fuzz.weightedAlbedo) + material.Fuzz.weightedAlbedo * material.Fuzz.color * material.AO);
			lobeWeights.specular *= material.Fuzz.specularThroughput;
		}
#endif
	}
}

#endif  // __PBR_DEPENDENCY_HLSL__
