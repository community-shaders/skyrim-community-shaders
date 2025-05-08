#include "Common/Color.hlsli"
#include "Common/Math.hlsli"
#include "Common/PBR.hlsli"

namespace Skin
{
	Texture2D<float4> TexSkinDetailNormal : register(t72);

	struct SkinSurfaceProperties
	{
		float RoughnessPrimary;
		float RoughnessSecondary;
		float3 F0;
		float SecondarySpecIntensity;
		float CurvatureScale;
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
		skin.CurvatureScale = 1.0;
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

	float DisneyDiffuse(float NdotV, float NdotL, float VdotH, float roughness)
	{
		const float FD90 = 0.5 + 2.0 * VdotH * VdotH * roughness;
		const float fdv = 1.0 + (FD90 - 1.0) * pow(1.0 - NdotV, 5.0);
		const float fdl = 1.0 + (FD90 - 1.0) * pow(1.0 - NdotL, 5.0);

		return fdv * fdl;
	}

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

	float3 SSSSTransmittanceBlurred(float translucency, float sssWidth, float3 worldNormal, float3 light, float d, float power, float noise)
	{
		// Create a wider blur with jittered samples to counteract dithering
		float3 totalSSS = 0.0;
		float totalWeight = 0.0;

		// Create a tangent space for sampling
		float3 u, v;
		if (abs(worldNormal.y) < 0.999)
			u = normalize(cross(float3(0, 1, 0), worldNormal));
		else
			u = normalize(cross(worldNormal, float3(0, 0, 1)));
		v = cross(worldNormal, u);

		// Use blue noise-based sampling with a large kernel
		// First sample at the exact position (highest weight)
		float3 baseSSS = SSSSTransmittance(translucency, sssWidth, worldNormal, light, d);
		float baseWeight = 1.0;
		totalSSS += baseSSS * baseWeight;
		totalWeight += baseWeight;

		// Pre-defined rotation angles to distribute samples evenly
		float rotations[6] = {
			0.0, 1.0472, 2.0944, 3.1416, 4.1888, 5.236
		};

		// Multi-ring sampling for better coverage and more blur integration
		const int NUM_RINGS = 3;
		const int SAMPLES_PER_RING = 6;

		for (int ring = 0; ring < NUM_RINGS; ring++) {
			float ringRadius = (ring + 1) * 0.4;  // Increase radius for each ring

			for (int sample = 0; sample < SAMPLES_PER_RING; sample++) {
				// Create a rotated angle for this sample to distribute evenly
				float angle = rotations[sample] + noise * 1.5 + ring * 0.7853;

				// Add some randomization to the radius too
				float jitteredRadius = ringRadius * (1.0 + (frac(noise * 7.13 + sample * 3.578 + ring * 1.789) * 0.3 - 0.15));

				// Calculate offset direction
				float2 offset = float2(cos(angle), sin(angle)) * jitteredRadius;

				// Vary the normal and light direction slightly to simulate view-dependent scattering
				float3 offsetNormal = normalize(worldNormal + (u * offset.x + v * offset.y) * 0.25);
				float3 offsetLight = normalize(light + (u * offset.y - v * offset.x) * 0.1);

				// Also vary thickness based on radius to simulate skin depth variance
				float thicknessFactor = 1.0 + jitteredRadius * 0.6;  // More thickness variation
				float offsetThickness = d * thicknessFactor;

				// Calculate SSS with these modified parameters
				float3 sampleSSS = SSSSTransmittance(
					translucency,
					sssWidth * (1.0 + jitteredRadius * 0.3),  // Vary width too for more blur
					offsetNormal,
					offsetLight,
					offsetThickness);

				// Weight using a smoother falloff (not pure Gaussian but visually effective)
				float weight = exp(-jitteredRadius * jitteredRadius * 2.0) / (1.0 + ring * 0.5);

				totalSSS += sampleSSS * weight;
				totalWeight += weight;
			}
		}

		// Add one final wide-radius blur sample for extra softening
		{
			float angle = noise * 6.283;
			float2 wideOffset = float2(cos(angle), sin(angle)) * 1.5;

			float3 wideNormal = normalize(worldNormal + (u * wideOffset.x + v * wideOffset.y) * 0.4);
			float wideThickness = d * 2.0;

			float3 wideSample = SSSSTransmittance(
				translucency,
				sssWidth * 1.5,
				wideNormal,
				light,
				wideThickness);

			float wideWeight = 0.15;  // Fixed low weight for the wide sample
			totalSSS += wideSample * wideWeight;
			totalWeight += wideWeight;
		}

		// Apply color space transformation to reduce harsh transitions
		float3 result = (totalSSS / totalWeight);

		// Slightly boost saturation of the result to compensate for the blur
		float luminance = dot(result, float3(0.299, 0.587, 0.114));
		result = lerp(float3(luminance, luminance, luminance), result, 1.2);

		return result * power;
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

		diffuse += light.LightColor * NdotL * DisneyDiffuse(NdotV, NdotL, VdotH, averageRoughness) / Math::PI;

		float3 F;

		specular += GetDualSpecularGGX(averageRoughness, skin.RoughnessPrimary, skin.RoughnessSecondary, skin.SecondarySpecIntensity, skin.F0, NdotL, NdotV, NdotH, VdotH, F) * light.LightColor * NdotL;

		float2 specularBRDF = PBR::GetEnvBRDFApproxLazarov(averageRoughness, NdotV);
		specular *= 1 + skin.F0 * (1 / (specularBRDF.x + specularBRDF.y) - 1);
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

		const float curvature = CalculateCurvature(N);
		specularWeight *= 1.0 - saturate(curvature * skin.CurvatureScale);

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
	}
}