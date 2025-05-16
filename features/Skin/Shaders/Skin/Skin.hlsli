#include "Common/Color.hlsli"
#include "Common/Math.hlsli"
#include "Common/PBR.hlsli"
#include "Common/SharedData.hlsli"

namespace Skin
{
	Texture2D<float4> TexSkinDetailNormal : register(t72);

	struct SkinSurfaceProperties
	{
		float RoughnessPrimary;
		float RoughnessSecondary;
		float3 F0;
		float SecondarySpecIntensity;
		float Curvature;
		float3 Albedo;
		float Thickness;
		float3 SubsurfaceColor;
		float AO;
	};

	SkinSurfaceProperties InitSkinSurfaceProperties()
	{
		SkinSurfaceProperties skin;
		skin.RoughnessPrimary = 0.55;
		skin.RoughnessSecondary = 0.35;
		skin.F0 = float3(0.0277, 0.0277, 0.0277);
		skin.SecondarySpecIntensity = 0.15;
		skin.Curvature = 0.0;
		skin.Albedo = float3(0.8, 0.6, 0.5);
		skin.Thickness = 0.15;
		skin.SubsurfaceColor = float3(0.6, 0.3, 0.2);
		skin.AO = 0.0;
		return skin;
	}

	float CalculateCurvature(float3 N)
	{
		const float3 dNdx = ddx(N);
		const float3 dNdy = ddy(N);
		return length(float2(dot(dNdx, dNdx), dot(dNdy, dNdy)));
	}

	// [Jorge Jimenez, Diego Gutierrez 2015, "Separable Subsurface Scattering"]
	// https://www.iryoku.com/separable-sss/
	float3 SSSSTransmittance(float translucency, float sssWidth, float3 worldNormal, float3 light, float d)
	{
		/**
		* Calculate the scale of the effect.
		*/
		float scale = 8.25 * (1.0 - translucency) / sssWidth;

		/**
		* First we shrink the position inwards the surface to avoid artifacts:
		* (Note that this can be done once for all the lights)
		*/
		// float4 shrinkedPos = float4(worldPosition - 0.005 * worldNormal, 1.0);

		/**
		* Now we calculate the thickness from the light point of view:
		*/
		// float4 shadowPosition = mul(shrinkedPos, lightViewProjection);
		// float d1 = SSSSSampleShadowmap(shadowPosition.xy / shadowPosition.w).r; // 'd1' has a range of 0..1
		// float d2 = shadowPosition.z; // 'd2' has a range of 0..'lightFarPlane'
		// d1 *= lightFarPlane; // So we scale 'd1' accordingly:
		// float d = scale * abs(d1 - d2);
		d = scale * abs(d);  // Use the passed 'd' value instead of calculating it here.

		/**
		* Armed with the thickness, we can now calculate the color by means of the
		* precalculated transmittance profile.
		* (It can be precomputed into a texture, for maximum performance):
		*/
		float dd = -d * d;
		float3 profile = float3(0.233, 0.455, 0.649) * exp(dd / 0.0064) +
		                 float3(0.1, 0.336, 0.344) * exp(dd / 0.0484) +
		                 float3(0.118, 0.198, 0.0) * exp(dd / 0.187) +
		                 float3(0.113, 0.007, 0.007) * exp(dd / 0.567) +
		                 float3(0.358, 0.004, 0.0) * exp(dd / 1.99) +
		                 float3(0.078, 0.0, 0.0) * exp(dd / 7.41);

		/** 
		* Using the profile, we finally approximate the transmitted lighting from
		* the back of the object:
		*/
		return profile * saturate(0.3 + dot(light, -worldNormal));
	}

	float3 GetDualSpecularGGX(float AverageRoughness, float Lobe0Roughness, float Lobe1Roughness, float LobeMix, float3 SpecularColor, float NdotL, float NdotV, float NdotH, float VdotH, out float3 F)
	{
		float D = lerp(PBR::GetNormalDistributionFunctionGGX(Lobe0Roughness, NdotH), PBR::GetNormalDistributionFunctionGGX(Lobe1Roughness, NdotH), LobeMix);
		float G = PBR::GetVisibilityFunctionSmithJointApprox(AverageRoughness, NdotV, NdotL);
		F = PBR::GetFresnelFactorSchlick(SpecularColor, VdotH);

		return D * G * F;
	}

	void SkinDirectLightInput(
		out float3 diffuse,
		out float3 transmission,
		out float3 specular,
		PBR::LightProperties light,
		SkinSurfaceProperties skin,
		float3 N, float3 V, float3 L)
	{
		diffuse = 0;
		transmission = 0;
		specular = 0;

		light.LightColor *= Math::PI;

		const float3 H = normalize(V + L);
		const float oNdotL = dot(N, L);
		const float NdotL = clamp(oNdotL, 1e-5, 1.0);
		const float NdotV = saturate(abs(dot(N, V)) + 1e-5);
		const float NdotH = saturate(dot(N, H));
		const float VdotH = saturate(dot(V, H));
		const float VdotL = dot(V, L);

		float averageRoughness = lerp(skin.RoughnessPrimary, skin.RoughnessSecondary, skin.SecondarySpecIntensity);

		diffuse += light.LightColor * NdotL * PBR::GetDiffuseDirectLightMultiplierChan(averageRoughness, NdotV, NdotL, VdotH, NdotH);

		float3 F;

		specular += GetDualSpecularGGX(averageRoughness, skin.RoughnessPrimary, skin.RoughnessSecondary, skin.SecondarySpecIntensity, skin.F0, NdotL, NdotV, NdotH, VdotH, F) * light.LightColor * NdotL;

		float2 specularBRDF = PBR::GetEnvBRDFApproxLazarov(averageRoughness, NdotV);
		specular *= 1 + skin.F0 * (1 / (specularBRDF.x + specularBRDF.y) - 1);

		specular *= 1 - skin.Curvature;
	}

	void SkinIndirectLobeWeights(
		out float3 diffuseWeight,
		out float3 specularWeight,
		SkinSurfaceProperties skin,
		float3 N, float3 V, float3 VN)
	{
		const float NdotV = saturate(dot(N, V));

		float averageRoughness = lerp(skin.RoughnessPrimary, skin.RoughnessSecondary, skin.SecondarySpecIntensity);

		float2 specularBRDF = PBR::GetEnvBRDFApproxLazarov(averageRoughness, NdotV);
		specularWeight = skin.F0 * specularBRDF.x + specularBRDF.y;

		diffuseWeight = skin.Albedo * (1.0 - specularWeight);

		specularWeight *= 1 + skin.F0 * (1 / (specularBRDF.x + specularBRDF.y) - 1);
		float3 R = reflect(-V, N);
		float horizon = min(1.0 + dot(R, VN), 1.0);
		horizon *= horizon;
		specularWeight *= horizon;

		float3 diffuseAO = skin.AO;
		float3 specularAO = PBR::SpecularAOLagarde(NdotV, skin.AO, averageRoughness);

		diffuseAO = PBR::MultiBounceAO(skin.Albedo, diffuseAO.x).y;
		specularAO = PBR::MultiBounceAO(skin.F0, specularAO.x).y;

		diffuseWeight *= diffuseAO;
		specularWeight *= specularAO;

		specularWeight *= 1 - skin.Curvature;
	}
}