#ifndef __SKIN_HLSLI__
#define __SKIN_HLSLI__

#include "Common/BRDF.hlsli"
#include "Common/Color.hlsli"
#include "Common/LightingCommon.hlsli"
#include "Common/Math.hlsli"
#include "Common/Shading.hlsli"
#include "Common/SharedData.hlsli"

namespace Skin
{
	float CalculateCurvature(float3 N)
	{
		const float3 dNdx = ddx(N);
		const float3 dNdy = ddy(N);
		return length(float2(dot(dNdx, dNdx), dot(dNdy, dNdy)));
	}

#if defined(PSHADER)
	cbuffer SkinPerGeometry : register(b7)
	{
		float4 skinPerGeometry;
	};
#endif
#if defined(SKIN)
	Texture2D<float4> TexSkinDetailNormal : register(t72);

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

	float3 DualSpecularGGX(float AverageRoughness, float Lobe0Roughness, float Lobe1Roughness, float LobeMix, float3 SpecularColor, float NdotL, float NdotV, float NdotH, float VdotH, out float3 F)
	{
		float D = lerp(BRDF::D_GGX(Lobe0Roughness, NdotH), BRDF::D_GGX(Lobe1Roughness, NdotH), LobeMix);
		float G = BRDF::Vis_SmithJointApprox(AverageRoughness, NdotV, NdotL);
		F = BRDF::F_Schlick(SpecularColor, VdotH);

		return D * G * F;
	}

	// a contact shadow approximation, totally not physically correct; a riff on "Chan 2018, "Material Advances in Call of Duty: WWII" and "The Technical Art of Uncharted 4" http://advances.realtimerendering.com/other/2016/naughty_dog/NaughtyDog_TechArt_Final.pdf (microshadowing)"
	float ApproximateDirectOcculusion(float aoVisibility, float NdotL)
	{
		float aperture = rsqrt(1.0000001 - aoVisibility);
		NdotL += 0.1;  // when using bent normals, avoids overshadowing - bent normals are just approximation anyhow
		return saturate(NdotL * aperture);
	}

	void SkinDirectLightInput(
		out DirectLightingOutput lightingOutput,
		DirectContext context,
		MaterialProperties material)
	{
		lightingOutput = (DirectLightingOutput)0;
		context.lightColor *= Color::PBRLightingCompensation * context.detailedShadow;

		const float3 N = context.worldNormal;
		const float3 V = context.viewDir;
		const float3 L = context.lightDir;
		const float3 H = context.halfVector;

		const float oNdotL = dot(N, L);
		float NdotL = clamp(oNdotL, 1e-5, 1.0);
		float NdotV = saturate(abs(dot(N, V)) + 1e-5);
		float NdotH = saturate(dot(N, H));
		float VdotH = saturate(dot(V, H));

		context.lightColor *= ApproximateDirectOcculusion(material.AO, NdotL);

		float averageRoughness = lerp(material.Roughness, material.RoughnessSecondary, material.SecondarySpecIntensity);

		lightingOutput.diffuse += context.lightColor * NdotL * BRDF::Diffuse_Burley(averageRoughness, NdotV, NdotL, VdotH);
		float3 F;
		float3 F0 = material.F0 * saturate(1 - material.Curvature);

		lightingOutput.specular += DualSpecularGGX(averageRoughness, material.Roughness, material.RoughnessSecondary, material.SecondarySpecIntensity, F0, NdotL, NdotV, NdotH, VdotH, F) * context.lightColor * NdotL;

		float2 specularBRDF = BRDF::EnvBRDF(averageRoughness, NdotV);
		lightingOutput.specular *= 1 + F0 * (1 / (specularBRDF.x + specularBRDF.y) - 1);
		lightingOutput.diffuse *= 1 - F;

		if (material.FuzzWeight > 0.0) {
			float3 FuzzF0 = material.FuzzColor * saturate(1 - material.Curvature);
			float fuzzD = BRDF::D_Charlie(material.FuzzRoughness, NdotH);
			float fuzzG = BRDF::Vis_Neubelt(NdotV, NdotL);
			float3 fuzzF = BRDF::F_Schlick(FuzzF0, VdotH);
			float3 fuzzSpecular = fuzzD * fuzzG * fuzzF * context.lightColor * NdotL;
			float2 fuzzSpecularBRDF = BRDF::EnvBRDFApproxLazarov(material.FuzzRoughness, NdotV);
			fuzzSpecular *= 1 + material.FuzzColor * (1 / (fuzzSpecularBRDF.x + fuzzSpecularBRDF.y) - 1);

			lightingOutput.specular += fuzzSpecular * material.FuzzWeight;
		}

		float3 sssTransmittance = SSSSTransmittance(
									  SharedData::skinData.sssParams.x,
									  SharedData::skinData.sssParams.y,
									  N,
									  L,
									  material.Thickness) *
		                          SharedData::skinData.sssParams.w;
		lightingOutput.transmission = min(sssTransmittance * context.lightColor * context.softShadow * material.BaseColor, context.lightColor);
	}

	void SkinIndirectLobeWeights(
		out IndirectLobeWeights lobeWeights,
		MaterialProperties material,
		IndirectContext context)
	{
		lobeWeights = (IndirectLobeWeights)0;

		const float3 N = context.worldNormal;
		const float3 V = context.viewDir;
		const float3 VN = context.vertexNormal;

		float NdotV = saturate(dot(N, V));

		float averageRoughness = lerp(material.Roughness, material.RoughnessSecondary, material.SecondarySpecIntensity);

		float2 specularBRDF = BRDF::EnvBRDF(averageRoughness, NdotV);

		lobeWeights.specular = material.F0 * specularBRDF.x + specularBRDF.y;

		lobeWeights.diffuse = material.BaseColor * (1.0 - lobeWeights.specular.x - lobeWeights.specular.y);
		lobeWeights.specular *= 1 + material.F0 * (1 / (specularBRDF.x + specularBRDF.y) - 1);

		float3 R = reflect(-V, N);
		float horizon = min(1.0 + dot(R, VN), 1.0);
		horizon *= horizon;
		lobeWeights.specular *= horizon;

		float3 diffuseAO = material.AO;
		float3 specularAO = SpecularAOLagarde(NdotV, material.AO, averageRoughness);

		diffuseAO = MultiBounceAO(material.BaseColor, diffuseAO.x).y;
		specularAO = MultiBounceAO(material.F0, specularAO.x).y;

		lobeWeights.diffuse *= diffuseAO;
		lobeWeights.specular *= specularAO;

		lobeWeights.specular *= saturate(1 - material.Curvature);
	}

	// https://blog.selfshadow.com/publications/blending-in-detail/
	// geometric normal s, a base normal t and a secondary (or detail) normal u
	float3 ReorientNormal(float3 u, float3 t, float3 s)
	{
		// Build the shortest-arc quaternion
		float4 q = float4(cross(s, t), dot(s, t) + 1) / sqrt(2 * (dot(s, t) + 1));

		// Rotate the normal
		return u * (q.w * q.w - dot(q.xyz, q.xyz)) + 2 * q.xyz * dot(q.xyz, u) + 2 * q.w * cross(q.xyz, u);
	}

	// for when s = (0,0,1)
	float3 ReorientNormal(float3 n1, float3 n2)
	{
		n1 += float3(0, 0, 1);
		n2 *= float3(-1, -1, 1);

		return n1 * dot(n1, n2) / n1.z - n2;
	}

	float3x3 ReconstructTBN(float3 worldPos, float3 worldNormal, float2 uv)
	{
		float3 dFdx = ddx(worldPos);
		float3 dFdy = ddy(worldPos);
		float2 dUVdx = ddx(uv);
		float2 dUVdy = ddy(uv);
		float3 tangent = normalize(dFdx * dUVdy.y - dFdy * dUVdx.y);
		float3 bitangent = normalize(dFdy * dUVdx.x - dFdx * dUVdy.x);
		tangent = normalize(tangent - worldNormal * dot(worldNormal, tangent));
		bitangent = normalize(bitangent - worldNormal * dot(worldNormal, bitangent));

		return float3x3(tangent, bitangent, normalize(worldNormal));
	}

	float3 CalculateNormalFromHeight(float height, float heightScale, float2 uv)
	{
		float dHdx = ddx(height);
		float dHdy = ddy(height);
		float2 dUVdx = ddx(uv);
		float2 dUVdy = ddy(uv);

		float det = dUVdx.x * dUVdy.y - dUVdx.y * dUVdy.x;
		if (det == 0.0f) {
			return float3(0, 0, 1);  // Avoid division by zero
		}

		float dHdx_Tex = (dHdx * dUVdy.y - dHdy * dUVdx.y) / det;
		float dHdy_Tex = (dHdy * dUVdx.x - dHdx * dUVdy.x) / det;
		float3 normal = float3(-dHdx_Tex, -dHdy_Tex, 0);
		return normal * heightScale + float3(0, 0, 1);
	}

	float FBM(float2 uv, float base_scale, int octaves, float lacunarity, float persistence, float z_offset_multiplier)
	{
		float total = 0.0;
		float frequency = base_scale;
		float amplitude = 1.0;
		float max_amplitude = 0.0;
		for (int i = 0; i < octaves; i++) {
			total += amplitude * (Random::perlinNoise(float3(uv * frequency, (float)i * z_offset_multiplier)) + 1.0) * 0.5;

			max_amplitude += amplitude;
			amplitude *= persistence;
			frequency *= lacunarity;
		}
		if (max_amplitude > 0.0) {
			return total / max_amplitude;
		}
		return 0.0;
	}

	float PerlinNoise(float2 uv, float scale, float lacunarity, float persistence, float strength)
	{
		if (strength <= 0.001f) {
			return 0.0f;
		}
		if (strength >= 0.999f) {
			return 1.0f;
		}
		int octaves = 5;
		float z_offset_multiplier = 7.375f;

		float noise_value = FBM(uv, scale, octaves, lacunarity, persistence, z_offset_multiplier);

		float dynamic_threshold = 1.0f - strength;

		float sweat_intensity = saturate((noise_value - dynamic_threshold) / strength);

		sweat_intensity = pow(sweat_intensity, 1.5f);

		if (strength > 0.8f) {
			sweat_intensity = sweat_intensity * saturate(0.99f - (strength - 0.8f) * 5.0f) + (strength - 0.8f) * 5.0f;
		}
		return pow(sweat_intensity, 0.1f);
	}
#endif

	float2 GetWetness(float z, float3 modelNormal)
	{
		if (skinPerGeometry.x == 0.f && skinPerGeometry.y == 0.f)
			return 0.f;

		float waterWet = 0.0f;
		float waterLevel = skinPerGeometry.z + skinPerGeometry.w;

		waterWet = skinPerGeometry.y * (1 - smoothstep(waterLevel - 2.5f, waterLevel + 2.5f, z));

		float sweatWet = skinPerGeometry.x;
#if !defined(SKIN)
		sweatWet *= 1.0f - saturate(dot(modelNormal, float3(0, 0, 1)));
#endif
		return float2(sweatWet, waterWet);
	}
}

#endif  // __SKIN_HLSLI__
