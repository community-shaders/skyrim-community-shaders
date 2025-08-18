#ifndef __PBR_DEPENDENCY_HLSL__
#define __PBR_DEPENDENCY_HLSL__

#include "Common/BRDF.hlsli"
#include "Common/Color.hlsli"
#include "Common/Math.hlsli"
#include "Common/SharedData.hlsli"

namespace PBR
{
	namespace Flags
	{
		static const uint HasEmissive = (1 << 0);
		static const uint HasDisplacement = (1 << 1);
		static const uint HasFeatureTexture0 = (1 << 2);
		static const uint HasFeatureTexture1 = (1 << 3);
		static const uint Subsurface = (1 << 4);
		static const uint TwoLayer = (1 << 5);
		static const uint ColoredCoat = (1 << 6);
		static const uint InterlayerParallax = (1 << 7);
		static const uint CoatNormal = (1 << 8);
		static const uint Fuzz = (1 << 9);
		static const uint HairMarschner = (1 << 10);
		static const uint Glint = (1 << 11);
		static const uint ProjectedGlint = (1 << 12);
	}

	namespace TerrainFlags
	{
		static const uint LandTile0PBR = (1 << 0);
		static const uint LandTile1PBR = (1 << 1);
		static const uint LandTile2PBR = (1 << 2);
		static const uint LandTile3PBR = (1 << 3);
		static const uint LandTile4PBR = (1 << 4);
		static const uint LandTile5PBR = (1 << 5);
		static const uint LandTile0HasDisplacement = (1 << 6);
		static const uint LandTile1HasDisplacement = (1 << 7);
		static const uint LandTile2HasDisplacement = (1 << 8);
		static const uint LandTile3HasDisplacement = (1 << 9);
		static const uint LandTile4HasDisplacement = (1 << 10);
		static const uint LandTile5HasDisplacement = (1 << 11);
		static const uint LandTile0HasGlint = (1 << 12);
		static const uint LandTile1HasGlint = (1 << 13);
		static const uint LandTile2HasGlint = (1 << 14);
		static const uint LandTile3HasGlint = (1 << 15);
		static const uint LandTile4HasGlint = (1 << 16);
		static const uint LandTile5HasGlint = (1 << 17);
	}

#if defined(GLINT)
#	include "Common/Glints/Glints2023.hlsli"
#else
	namespace Glints
	{
		typedef float GlintCachedVars;
	}
#endif

	struct SurfaceProperties
	{
		float3 BaseColor;
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
	};

	SurfaceProperties InitSurfaceProperties()
	{
		SurfaceProperties surfaceProperties;

		surfaceProperties.Roughness = 1;
		surfaceProperties.Metallic = 0;
		surfaceProperties.AO = 1;
		surfaceProperties.F0 = 0;

		surfaceProperties.SubsurfaceColor = 0;
		surfaceProperties.Thickness = 0;

		surfaceProperties.CoatColor = 0;
		surfaceProperties.CoatStrength = 0;
		surfaceProperties.CoatRoughness = 0;
		surfaceProperties.CoatF0 = 0;

		surfaceProperties.FuzzColor = 0;
		surfaceProperties.FuzzWeight = 0;

		surfaceProperties.GlintScreenSpaceScale = 1.5;
		surfaceProperties.GlintLogMicrofacetDensity = 1.0;
		surfaceProperties.GlintMicrofacetRoughness = 0.015;
		surfaceProperties.GlintDensityRandomization = 2.0;

#ifdef GLINT
		surfaceProperties.GlintCache.uv = 0;
		surfaceProperties.GlintCache.gridSeed = 0;
		surfaceProperties.GlintCache.footprintArea = 0;
		surfaceProperties.Noise = 0;
#endif

		return surfaceProperties;
	}

	struct LightProperties
	{
		float3 LightColor;
		float3 CoatLightColor;
	};

	LightProperties InitLightProperties(float3 lightColor, float3 nonParallaxShadow, float3 parallaxShadow)
	{
		LightProperties result;
		result.LightColor = lightColor * nonParallaxShadow * parallaxShadow;
		[branch] if ((PBRFlags & Flags::InterlayerParallax) != 0)
		{
			result.CoatLightColor = lightColor * nonParallaxShadow;
		}
		else
		{
			result.CoatLightColor = result.LightColor;
		}
		return result;
	}

	// [Jimenez et al. 2016, "Practical Realtime Strategies for Accurate Indirect Occlusion"]
	float3 MultiBounceAO(float3 baseColor, float ao)
	{
		float3 a = 2.0404 * baseColor - 0.3324;
		float3 b = -4.7951 * baseColor + 0.6417;
		float3 c = 2.7552 * baseColor + 0.6903;
		return max(ao, ((ao * a + b) * ao + c) * ao);
	}

	// [Lagarde et al. 2014, "Moving Frostbite to Physically Based Rendering 3.0"]
	float SpecularAOLagarde(float NdotV, float ao, float roughness)
	{
		return saturate(pow(abs(NdotV + ao), exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
	}

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

	float3 GetSpecularDirectLightMultiplierMicrofacet(float roughness, float3 specularColor, float NdotL, float NdotV, float NdotH, float VdotH, out float3 F)
	{
		float D = BRDF::D_GGX(roughness, NdotH);
		float G = BRDF::Vis_SmithJointApprox(roughness, NdotV, NdotL);
		F = BRDF::F_Schlick(specularColor, VdotH);

		return D * G * F;
	}

	float3 GetSpecularDirectLightMultiplierMicroflakes(float roughness, float3 specularColor, float NdotL, float NdotV, float NdotH, float VdotH)
	{
		float D = BRDF::D_Charlie(roughness, NdotH);
		float G = BRDF::Vis_Neubelt(NdotV, NdotL);
		float3 F = BRDF::F_Schlick(specularColor, VdotH);

		return D * G * F;
	}

	float HairIOR()
	{
		const float n = 1.55;
		const float a = 1;

		float ior1 = 2 * (n - 1) * (a * a) - n + 2;
		float ior2 = 2 * (n - 1) / (a * a) - n + 2;
		return 0.5f * ((ior1 + ior2) + 0.5f * (ior1 - ior2));  //assume cos2PhiH = 0.5f
	}

	float IORToF0(float IOF)
	{
		return pow((1 - IOF) / (1 + IOF), 2);
	}

	inline float HairGaussian(float B, float Theta)
	{
		return exp(-0.5 * Theta * Theta / (B * B)) / (sqrt(Math::TAU) * B);
	}

	float3 GetHairDiffuseColorMarschner(float3 N, float3 V, float3 L, float NdotL, float NdotV, float VdotL, float backlit, float area, SurfaceProperties surfaceProperties)
	{
		float3 S = 0;

		float cosThetaL = sqrt(max(0, 1 - NdotL * NdotL));
		float cosThetaV = sqrt(max(0, 1 - NdotV * NdotV));
		float cosThetaD = sqrt((1 + cosThetaL * cosThetaV + NdotV * NdotL) / 2.0);

		const float3 Lp = L - NdotL * N;
		const float3 Vp = V - NdotL * N;
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
			area + surfaceProperties.Roughness,
			area + surfaceProperties.Roughness / 2,
			area + surfaceProperties.Roughness * 2
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
		Tp = pow(abs(surfaceProperties.BaseColor), 0.5 * sqrt(1 - (h * a) * (h * a)) / cosThetaD);
		Np = exp(-3.65 * cosPhi - 3.98);
		S += (Mp * Np) * (Fp * Tp) * backlit;

		// TRT
		Mp = HairGaussian(B[2], ThetaH - Alpha[2]);
		f = BRDF::F_Schlick(specularColor, cosThetaD * 0.5f).x;
		Fp = (1 - f) * (1 - f) * f;
		Tp = pow(abs(surfaceProperties.BaseColor), 0.8 / cosThetaD);
		Np = exp(17 * cosPhi - 16.78);
		S += (Mp * Np) * (Fp * Tp);

		return S;
	}

	float3 GetHairDiffuseAttenuationKajiyaKay(float3 N, float3 V, float3 L, float NdotL, float NdotV, float shadow, SurfaceProperties surfaceProperties)
	{
		float3 S = 0;

		float diffuseKajiya = 1 - abs(NdotL);

		float3 fakeN = normalize(V - N * NdotV);
		const float wrap = 1;
		float wrappedNdotL = saturate((dot(fakeN, L) + wrap) / ((1 + wrap) * (1 + wrap)));
		float diffuseScatter = (1 / Math::PI) * lerp(wrappedNdotL, diffuseKajiya, 0.33);
		float luma = Color::RGBToLuminance(surfaceProperties.BaseColor);
		float3 scatterTint = pow(surfaceProperties.BaseColor / luma, 1 - shadow);
		S += sqrt(surfaceProperties.BaseColor) * diffuseScatter * scatterTint;

		return S;
	}

	float3 GetHairColorMarschner(float3 N, float3 V, float3 L, float NdotL, float NdotV, float VdotL, float shadow, float backlit, float area, SurfaceProperties surfaceProperties)
	{
		float3 color = 0;

		color += GetHairDiffuseColorMarschner(N, V, L, NdotL, NdotV, VdotL, backlit, area, surfaceProperties);
		color += GetHairDiffuseAttenuationKajiyaKay(N, V, L, NdotL, NdotV, shadow, surfaceProperties);

		return color;
	}

	void GetDirectLightInput(out float3 diffuse, out float3 coatDiffuse, out float3 transmission, out float3 specular, float3 N, float3 coatN, float3 V, float3 coatV, float3 L, float3 coatL, LightProperties lightProperties, SurfaceProperties surfaceProperties,
		float3x3 tbnTr, float2 uv)
	{
		diffuse = 0;
		coatDiffuse = 0;
		transmission = 0;
		specular = 0;

		float3 H = normalize(V + L);

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
			transmission += lightProperties.LightColor * GetHairColorMarschner(N, V, L, NdotL, NdotV, VdotL, 0, 1, 0, surfaceProperties);
		}
		else
#endif
		{
			diffuse += lightProperties.LightColor * satNdotL * BRDF::Diffuse_Lambert();

			float3 F;
#if defined(GLINT)
			specular += GetSpecularDirectLightMultiplierMicrofacetWithGlint(surfaceProperties.Noise, surfaceProperties.Roughness, surfaceProperties.F0, satNdotL, satNdotV, satNdotH, satVdotH, mul(tbnTr, H).x,
							surfaceProperties.GlintLogMicrofacetDensity, surfaceProperties.GlintMicrofacetRoughness, surfaceProperties.GlintDensityRandomization, surfaceProperties.GlintCache, F) *
			            lightProperties.LightColor * satNdotL;
#else
			specular += GetSpecularDirectLightMultiplierMicrofacet(surfaceProperties.Roughness, surfaceProperties.F0, satNdotL, satNdotV, satNdotH, satVdotH, F) * lightProperties.LightColor * satNdotL;
#endif

			float2 specularBRDF = BRDF::EnvBRDF(surfaceProperties.Roughness, satNdotV);
			specular *= 1 + surfaceProperties.F0 * (1 / (specularBRDF.x + specularBRDF.y) - 1);

#if !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
			[branch] if ((PBRFlags & Flags::Fuzz) != 0)
			{
				float3 fuzzSpecular = GetSpecularDirectLightMultiplierMicroflakes(surfaceProperties.Roughness, surfaceProperties.FuzzColor, satNdotL, satNdotV, satNdotH, satVdotH) * lightProperties.LightColor * satNdotL;
				fuzzSpecular *= 1 + surfaceProperties.FuzzColor * (1 / (specularBRDF.x + specularBRDF.y) - 1);

				specular = lerp(specular, fuzzSpecular, surfaceProperties.FuzzWeight);
			}

			[branch] if ((PBRFlags & Flags::Subsurface) != 0)
			{
				const float subsurfacePower = 12.234;
				float forwardScatter = exp2(saturate(-VdotL) * subsurfacePower - subsurfacePower);
				float backScatter = saturate(satNdotL * surfaceProperties.Thickness + (1.0 - surfaceProperties.Thickness)) * 0.5;
				float subsurface = lerp(backScatter, 1, forwardScatter) * (1.0 - surfaceProperties.Thickness);
				transmission += surfaceProperties.SubsurfaceColor * subsurface * lightProperties.LightColor * BRDF::Diffuse_Lambert();
			}
			else if ((PBRFlags & Flags::TwoLayer) != 0)
			{
				float3 coatH = normalize(coatV + coatL);

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
				float3 coatSpecular = GetSpecularDirectLightMultiplierMicrofacet(surfaceProperties.CoatRoughness, surfaceProperties.CoatF0, coatNdotL, coatNdotV, coatNdotH, coatVdotH, coatF) * lightProperties.CoatLightColor * coatNdotL;

				float3 layerAttenuation = 1 - coatF * surfaceProperties.CoatStrength;
				diffuse *= layerAttenuation;
				specular *= layerAttenuation;

				coatDiffuse += lightProperties.CoatLightColor * coatNdotL * BRDF::Diffuse_Lambert();
				specular += coatSpecular * surfaceProperties.CoatStrength;
			}
#endif
		}
	}

	float3 GetWetnessDirectLightSpecularInput(float3 N, float3 V, float3 L, float3 lightColor, float roughness)
	{
		const float wetnessStrength = 1;
		const float wetnessF0 = 0.02;

		float3 H = normalize(V + L);
		float NdotL = clamp(dot(N, L), EPSILON_DOT_CLAMP, 1);
		float NdotV = saturate(abs(dot(N, V)) + EPSILON_DOT_CLAMP);
		float NdotH = saturate(dot(N, H));
		float VdotH = saturate(dot(V, H));

		float3 wetnessF;
		float3 wetnessSpecular = GetSpecularDirectLightMultiplierMicrofacet(roughness, wetnessF0, NdotL, NdotV, NdotH, VdotH, wetnessF) * lightColor * NdotL;

		return wetnessSpecular * wetnessStrength;
	}

	void GetIndirectLobeWeights(out float3 diffuseLobeWeight, out float3 specularLobeWeight, float3 N, float3 V, float3 VN, float3 diffuseColor, SurfaceProperties surfaceProperties)
	{
		diffuseLobeWeight = 0;
		specularLobeWeight = 0;

		float NdotV = saturate(dot(N, V));

#if !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
		[branch] if ((PBRFlags & Flags::HairMarschner) != 0)
		{
			float3 L = normalize(V - N * dot(V, N));
			float NdotL = dot(N, L);
			float VdotL = dot(V, L);
			diffuseLobeWeight = GetHairColorMarschner(N, V, L, NdotL, NdotV, VdotL, 1, 0, 0.2, surfaceProperties);
		}
		else
#endif
		{
			diffuseLobeWeight = diffuseColor;

#if !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
			[branch] if ((PBRFlags & Flags::Subsurface) != 0)
			{
				diffuseLobeWeight += surfaceProperties.SubsurfaceColor * (1 - surfaceProperties.Thickness) / Math::PI;
			}
			[branch] if ((PBRFlags & Flags::Fuzz) != 0)
			{
				diffuseLobeWeight += surfaceProperties.FuzzColor * surfaceProperties.FuzzWeight;
			}
#endif

			float2 specularBRDF = BRDF::EnvBRDF(surfaceProperties.Roughness, NdotV);
			specularLobeWeight = surfaceProperties.F0 * specularBRDF.x + specularBRDF.y;

			diffuseLobeWeight *= (1 - specularLobeWeight);
			specularLobeWeight *= 1 + surfaceProperties.F0 * (1 / (specularBRDF.x + specularBRDF.y) - 1);

#if !defined(LANDSCAPE) && !defined(LODLANDSCAPE)
			[branch] if ((PBRFlags & Flags::TwoLayer) != 0)
			{
				float2 coatSpecularBRDF = BRDF::EnvBRDF(surfaceProperties.CoatRoughness, NdotV);
				float3 coatSpecularLobeWeight = surfaceProperties.CoatF0 * coatSpecularBRDF.x + coatSpecularBRDF.y;
				coatSpecularLobeWeight *= 1 + surfaceProperties.CoatF0 * (1 / (coatSpecularBRDF.x + coatSpecularBRDF.y) - 1);

				float3 coatF = BRDF::F_Schlick(surfaceProperties.CoatF0, NdotV);

				float3 layerAttenuation = 1 - coatF * surfaceProperties.CoatStrength;
				diffuseLobeWeight *= layerAttenuation;
				specularLobeWeight *= layerAttenuation;

				[branch] if ((PBRFlags & Flags::ColoredCoat) != 0)
				{
					float3 coatDiffuseLobeWeight = surfaceProperties.CoatColor * (1 - coatSpecularLobeWeight);
					diffuseLobeWeight += coatDiffuseLobeWeight * surfaceProperties.CoatStrength;
				}
				specularLobeWeight += coatSpecularLobeWeight * surfaceProperties.CoatStrength;
			}
#endif
		}

		// Horizon specular occlusion
		// https://marmosetco.tumblr.com/post/81245981087
		float3 R = reflect(-V, N);
		float horizon = min(1.0 + dot(R, VN), 1.0);
		horizon = horizon * horizon;
		specularLobeWeight *= horizon;

		float3 diffuseAO = surfaceProperties.AO;
		float3 specularAO = SpecularAOLagarde(NdotV, surfaceProperties.AO, surfaceProperties.Roughness);

		diffuseAO = MultiBounceAO(diffuseColor, diffuseAO.x).y;
		specularAO = MultiBounceAO(surfaceProperties.F0, specularAO.x).y;

		diffuseLobeWeight *= diffuseAO * Color::PBRLightingScale;
		specularLobeWeight *= specularAO;
	}

	float3 GetWetnessIndirectSpecularLobeWeight(float3 N, float3 V, float3 VN, float roughness)
	{
		const float wetnessStrength = 1;
		const float wetnessF0 = 0.02;

		float NdotV = saturate(abs(dot(N, V)) + EPSILON_DOT_CLAMP);
		float2 specularBRDF = BRDF::EnvBRDF(roughness, NdotV);
		float3 specularLobeWeight = wetnessF0 * specularBRDF.x + specularBRDF.y;

		// Horizon specular occlusion
		// https://marmosetco.tumblr.com/post/81245981087
		float3 R = reflect(-V, N);
		float horizon = min(1.0 + dot(R, VN), 1.0);
		horizon = horizon * horizon;
		specularLobeWeight *= horizon;

		return specularLobeWeight * wetnessStrength;
	}
}

#endif  // __PBR_DEPENDENCY_HLSL__