#ifndef __PBR_MATH_HLSL__
#define __PBR_MATH_HLSL__

#include "Common/Math.hlsli"
#include "Common/BRDF.hlsli"

namespace PBR
{
	namespace Constants
	{
		static const float MinRoughness = 0.04f;
		static const float MaxRoughness = 1.0f;
		static const float MinGlintDensity = 1.0f;
		static const float MaxGlintDensity = 40.0f;
		static const float MinGlintRoughness = 0.005f;
		static const float MaxGlintRoughness = 0.3f;
		static const float MinGlintDensityRandomization = 0.0f;
		static const float MaxGlintDensityRandomization = 5.0f;
	}

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

	/// @brief Calculate specular reflection using GGX microfacet model
	/// @param roughness Surface roughness [0,1]
	/// @param specularColor F0 reflectance at normal incidence
	/// @param NdotL Dot product of normal and light direction
	/// @param NdotV Dot product of normal and view direction
	/// @param NdotH Dot product of normal and half vector
	/// @param VdotH Dot product of view and half vector
	/// @param F Output Fresnel term
	/// @return Specular BRDF term (D * G * F)
	float3 GetSpecularDirectLightMultiplierMicrofacet(float roughness, float3 specularColor, float NdotL, float NdotV, float NdotH, float VdotH, out float3 F)
	{
		float D = BRDF::D_GGX(roughness, NdotH);
		float G = BRDF::Vis_SmithJointApprox(roughness, NdotV, NdotL);
		F = BRDF::F_Schlick(specularColor, VdotH);

		return D * G * F;
	}

	/// @brief Calculate specular reflection using Charlie microflake model (for sheen/fabric)
	/// @param roughness Surface roughness [0,1]
	/// @param specularColor F0 reflectance at normal incidence
	/// @param NdotL Dot product of normal and light direction
	/// @param NdotV Dot product of normal and view direction
	/// @param NdotH Dot product of normal and half vector
	/// @param VdotH Dot product of view and half vector
	/// @return Specular BRDF term (D * G * F)
	float3 GetSpecularDirectLightMultiplierMicroflakes(float roughness, float3 specularColor, float NdotL, float NdotV, float NdotH, float VdotH)
	{
		float D = BRDF::D_Charlie(roughness, NdotH);
		float G = BRDF::Vis_Neubelt(NdotV, NdotL);
		float3 F = BRDF::F_Schlick(specularColor, VdotH);

		return D * G * F;
	}

	/// @brief Calculate index of refraction for hair using Marschner model
	/// @return Effective IOR for hair (approximately 1.55)
	float HairIOR()
	{
		const float n = 1.55;
		const float a = 1;

		float ior1 = 2 * (n - 1) * (a * a) - n + 2;
		float ior2 = 2 * (n - 1) / (a * a) - n + 2;
		return 0.5f * ((ior1 + ior2) + 0.5f * (ior1 - ior2));  //assume cos2PhiH = 0.5f
	}

	/// @brief Convert index of refraction to F0 (reflectance at normal incidence)
	/// @param IOR Index of refraction
	/// @return F0 reflectance value
	float IORToF0(float IOR)
	{
		return pow((1 - IOR) / (1 + IOR), 2);
	}

	/// @brief Gaussian distribution for hair scattering
	/// @param B Standard deviation (roughness parameter)
	/// @param Theta Angle offset from peak
	/// @return Gaussian weight
	inline float HairGaussian(float B, float Theta)
	{
		// Guard against division by zero: clamp B to a minimum value
		float B_safe = max(B, 1e-6);
		return exp(-0.5 * Theta * Theta / (B_safe * B_safe)) / (sqrt(Math::TAU) * B_safe);
	}

	/// @brief Calculate wetness specular contribution for direct lighting
	/// @param N Surface normal
	/// @param V View direction
	/// @param L Light direction
	/// @param lightColor Light color/intensity
	/// @param roughness Surface roughness
	/// @return Wetness specular color contribution
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

	/// @brief Calculate wetness specular lobe weight for indirect lighting
	/// @param N Surface normal
	/// @param V View direction
	/// @param VN Vertex normal (for horizon occlusion)
	/// @param roughness Surface roughness
	/// @return Wetness specular lobe weight
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

#endif  // __PBR_MATH_HLSL__
