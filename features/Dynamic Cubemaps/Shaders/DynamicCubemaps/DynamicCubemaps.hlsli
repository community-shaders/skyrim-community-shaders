
#if defined(SKYLIGHTING)
#	include "Skylighting/Skylighting.hlsli"
#endif

namespace DynamicCubemaps
{
	TextureCube<float4> EnvReflectionsTexture : register(t30);
	TextureCube<float4> EnvTexture : register(t31);

	// https://www.unrealengine.com/en-US/blog/physically-based-shading-on-mobile
	half2 EnvBRDFApprox(half Roughness, half NoV)
	{
		const half4 c0 = { -1, -0.0275, -0.572, 0.022 };
		const half4 c1 = { 1, 0.0425, 1.04, -0.04 };
		half4 r = Roughness * c0 + c1;
		half a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
		half2 AB = half2(-1.04, 1.04) * a004 + r.zw;
		return AB;
	}

#if !defined(WATER)

#	if defined(SKYLIGHTING)
	float3 GetDynamicCubemapSpecularIrradiance(float2 uv, float3 N, float3 VN, float3 V, float roughness, sh2 skylighting)
#	else
	float3 GetDynamicCubemapSpecularIrradiance(float2 uv, float3 N, float3 VN, float3 V, float roughness)
#	endif
	{
		float3 R = reflect(-V, N);
		float NoV = saturate(dot(N, V));

		float level = roughness * 7.0;

		// Horizon specular occlusion
		// https://marmosetco.tumblr.com/post/81245981087
		float horizon = min(1.0 + dot(R, VN), 1.0);
		horizon *= horizon * horizon;

#	if defined(DEFERRED)
		return horizon;
#	else

		float3 finalIrradiance = 0;

#		if defined(SKYLIGHTING)
		if (SharedData::InInterior) {
			float3 specularIrradiance = Color::GammaToLinear(EnvReflectionsTexture.SampleLevel(SampColorSampler, R, level));

			return finalIrradiance;
		}

		sh2 specularLobe = SphericalHarmonics::FauxSpecularLobe(N, -V, roughness);

		float skylightingSpecular = SphericalHarmonics::FuncProductIntegral(skylighting, specularLobe);
		skylightingSpecular = Skylighting::mixSpecular(SharedData::skylightingSettings, skylightingSpecular);

		float3 specularIrradiance = 1;

		if (skylightingSpecular < 1.0)
			specularIrradiance = Color::GammaToLinear(EnvTexture.SampleLevel(SampColorSampler, R, level));

		float3 specularIrradianceReflections = 1.0;

		if (skylightingSpecular > 0.0)
			specularIrradianceReflections = Color::GammaToLinear(EnvReflectionsTexture.SampleLevel(SampColorSampler, R, level));

		finalIrradiance = lerp(specularIrradiance, specularIrradianceReflections, skylightingSpecular);
#		else
		float3 specularIrradiance = Color::GammaToLinear(EnvReflectionsTexture.SampleLevel(SampColorSampler, R, level));

		finalIrradiance += specularIrradiance;
#		endif
		return finalIrradiance;
#	endif
	}

#	if defined(SKYLIGHTING)
	float3 GetDynamicCubemap(float3 N, float3 VN, float3 V, float roughness, float3 F0, sh2 skylighting)
#	else
	float3 GetDynamicCubemap(float3 N, float3 VN, float3 V, float roughness, float3 F0)
#	endif
	{
		float3 R = reflect(-V, N);
		float NoV = saturate(dot(N, V));

		float level = roughness * 7.0;

		float2 specularBRDF = EnvBRDFApprox(roughness, NoV);

		// Horizon specular occlusion
		// https://marmosetco.tumblr.com/post/81245981087
		float horizon = min(1.0 + dot(R, VN), 1.0);
		horizon *= horizon * horizon;

#	if defined(DEFERRED)
		return horizon * (F0 * specularBRDF.x + specularBRDF.y);
#	else

		float3 finalIrradiance = 0;

#		if defined(SKYLIGHTING)
		if (SharedData::InInterior) {
			float3 specularIrradiance = Color::GammaToLinear(EnvReflectionsTexture.SampleLevel(SampColorSampler, R, level));

			finalIrradiance += specularIrradiance;

			return horizon * (F0 * specularBRDF.x + specularBRDF.y) * finalIrradiance;
		}

		sh2 specularLobe = SphericalHarmonics::FauxSpecularLobe(N, -V, roughness);

		float skylightingSpecular = SphericalHarmonics::FuncProductIntegral(skylighting, specularLobe);
		skylightingSpecular = Skylighting::mixSpecular(SharedData::skylightingSettings, skylightingSpecular);

		float3 specularIrradiance = 1;

		if (skylightingSpecular < 1.0)
			specularIrradiance = Color::GammaToLinear(EnvTexture.SampleLevel(SampColorSampler, R, level));

		float3 specularIrradianceReflections = 1.0;

		if (skylightingSpecular > 0.0)
			specularIrradianceReflections = Color::GammaToLinear(EnvReflectionsTexture.SampleLevel(SampColorSampler, R, level));

		finalIrradiance = lerp(specularIrradiance, specularIrradianceReflections, skylightingSpecular);
#		else
		float3 specularIrradiance = Color::GammaToLinear(EnvReflectionsTexture.SampleLevel(SampColorSampler, R, level));

		finalIrradiance += specularIrradiance;
#		endif
		return horizon * (F0 * specularBRDF.x + specularBRDF.y) * finalIrradiance;
#	endif
	}
#endif  // !WATER
}
