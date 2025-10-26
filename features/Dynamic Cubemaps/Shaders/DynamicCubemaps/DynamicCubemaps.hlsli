#ifndef DYNAMICCUBEMAPS_HLSLI
#define DYNAMICCUBEMAPS_HLSLI

#include "Common/BRDF.hlsli"

#if defined(SKYLIGHTING)
#	include "Skylighting/Skylighting.hlsli"
#endif

#if defined(IBL)
#	include "IBL/IBL.hlsli"
#endif

namespace DynamicCubemaps
{
	TextureCube<float3> EnvReflectionsTexture : register(t30);
	TextureCube<float3> EnvTexture : register(t31);

#if !defined(WATER)

#	if defined(SKYLIGHTING)
	float3 GetDynamicCubemapSpecularIrradiance(float2 uv, float3 N, float3 VN, float3 V, float roughness, sh2 skylighting)
#	else
	float3 GetDynamicCubemapSpecularIrradiance(float2 uv, float3 N, float3 VN, float3 V, float roughness)
#	endif
	{
		float3 R = reflect(-V, N);

		// Horizon specular occlusion
		// https://marmosetco.tumblr.com/post/81245981087
		float horizon = min(1.0 + dot(R, VN), 1.0);
		horizon *= horizon * horizon;

#	if defined(DEFERRED)
		return horizon;
#	else

		float NoV = saturate(dot(N, V));

		float level = roughness * 7.0;

		float3 finalIrradiance = 0;

        float directionalAmbientColorSpecular = Color::RGBToLuminance(
            max(0, mul(SharedData::DirectionalAmbient, float4(R, 1.0)))
        ) * Color::ReflectionNormalisationScale;

#		if defined(IBL) && defined(LIGHTING)
		const bool inWorld = (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InWorld);
		const bool inReflection = (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InReflection);
		if (SharedData::iblSettings.EnableDiffuseIBL && SharedData::iblSettings.UseStaticIBL && !inWorld && !inReflection) {
			float3 specularIrradiance = ImageBasedLighting::StaticSpecularIBLTexture.SampleLevel(SampColorSampler, R.xzy, level).xyz;
			finalIrradiance = specularIrradiance;
			return finalIrradiance;
		}
#		endif

#		if defined(SKYLIGHTING)
		if (SharedData::InInterior) {
			float3 specularIrradiance = EnvTexture.SampleLevel(SampColorSampler, R, level).xyz;

			float specularIrradianceLuminance = Color::RGBToLuminance(EnvTexture.SampleLevel(SampColorSampler, R, 15));

			specularIrradiance = (specularIrradiance / max(specularIrradianceLuminance, 0.001)) * directionalAmbientColorSpecular;
			specularIrradiance = Color::GammaToLinear(specularIrradiance);

			finalIrradiance = specularIrradiance;
			return finalIrradiance;
		}

		sh2 specularLobe = SphericalHarmonics::FauxSpecularLobe(N, -V, roughness);

		float skylightingSpecular = SphericalHarmonics::FuncProductIntegral(skylighting, specularLobe);
		skylightingSpecular = saturate(skylightingSpecular);
		skylightingSpecular = Skylighting::mixSpecular(SharedData::skylightingSettings, skylightingSpecular);

		float3 specularIrradianceReflections = 0.0;

		if (skylightingSpecular > 0.0){
			specularIrradianceReflections = EnvReflectionsTexture.SampleLevel(SampColorSampler, R, level);

		    float specularIrradianceReflectionsLuminance = Color::RGBToLuminance(EnvReflectionsTexture.SampleLevel(SampColorSampler, R, 15));

			specularIrradianceReflections = (specularIrradianceReflections / max(specularIrradianceReflectionsLuminance, 0.001)) * directionalAmbientColorSpecular;
			specularIrradianceReflections = Color::GammaToLinear(specularIrradianceReflections);
		}

		float3 specularIrradiance = 0.0;

		if (skylightingSpecular < 1.0){
			specularIrradiance = EnvTexture.SampleLevel(SampColorSampler, R, level);

            float specularIrradianceLuminance = Color::RGBToLuminance(EnvTexture.SampleLevel(SampColorSampler, R, 15));

			directionalAmbientColorSpecular = Color::GammaToLinear(directionalAmbientColorSpecular);
			directionalAmbientColorSpecular *= skylightingSpecular;
			directionalAmbientColorSpecular = Color::LinearToGamma(directionalAmbientColorSpecular);

			specularIrradiance = (specularIrradiance / max(specularIrradianceLuminance, 0.001)) * directionalAmbientColorSpecular;
			specularIrradiance = Color::GammaToLinear(specularIrradiance);
		}

		finalIrradiance = lerp(specularIrradiance, specularIrradianceReflections, skylightingSpecular);
#		else
		float3 specularIrradiance = EnvReflectionsTexture.SampleLevel(SampColorSampler, R, level);

		float specularIrradianceLuminance = Color::RGBToLuminance(EnvReflectionsTexture.SampleLevel(SampColorSampler, R, 15));

		specularIrradiance = (specularIrradiance / max(specularIrradianceLuminance, 0.001)) * directionalAmbientColorSpecular;
		specularIrradiance = Color::GammaToLinear(specularIrradiance);

		finalIrradiance = specularIrradiance;
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

		float2 specularBRDF = BRDF::EnvBRDF(roughness, NoV);

		// Horizon specular occlusion
		// https://marmosetco.tumblr.com/post/81245981087
		float horizon = min(1.0 + dot(R, VN), 1.0);
		horizon *= horizon * horizon;

#	if defined(DEFERRED)
		return horizon * (F0 * specularBRDF.x + specularBRDF.y);
#	else

		float3 finalIrradiance = 0;
		float directionalAmbientColorSpecular = Color::RGBToLuminance(max(0, mul(SharedData::DirectionalAmbient, float4(R, 1.0)))) * Color::ReflectionNormalisationScale;

#		if defined(IBL) && defined(LIGHTING)
		const bool inWorld = (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InWorld);
		const bool inReflection = (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InReflection);
		if (SharedData::iblSettings.EnableDiffuseIBL && SharedData::iblSettings.UseStaticIBL && !inWorld && !inReflection) {
			float3 specularIrradiance = ImageBasedLighting::StaticSpecularIBLTexture.SampleLevel(SampColorSampler, R.xzy, level).xyz;
			finalIrradiance += specularIrradiance;
			return horizon * (F0 * specularBRDF.x + specularBRDF.y) * finalIrradiance;
		}
#		endif

#		if defined(SKYLIGHTING)
		if (SharedData::InInterior) {
			float3 specularIrradiance = EnvTexture.SampleLevel(SampColorSampler, R, level).xyz;

			float specularIrradianceLuminance = Color::RGBToLuminance(EnvTexture.SampleLevel(SampColorSampler, R, 15));

			specularIrradiance = (specularIrradiance / max(specularIrradianceLuminance, 0.001)) * directionalAmbientColorSpecular;
			specularIrradiance = Color::GammaToLinear(specularIrradiance);

			finalIrradiance = specularIrradiance;
			return horizon * (F0 * specularBRDF.x + specularBRDF.y) * finalIrradiance;
		}

		sh2 specularLobe = SphericalHarmonics::FauxSpecularLobe(N, -V, roughness);

		float skylightingSpecular = SphericalHarmonics::FuncProductIntegral(skylighting, specularLobe);
		skylightingSpecular = saturate(skylightingSpecular);
		skylightingSpecular = Skylighting::mixSpecular(SharedData::skylightingSettings, skylightingSpecular);

		directionalAmbientColorSpecular *= skylightingSpecular;

		float3 specularIrradiance = 1.0;

		if (skylightingSpecular < 1.0){
			specularIrradiance = EnvTexture.SampleLevel(SampColorSampler, R, level);

            float specularIrradianceLuminance = Color::RGBToLuminance(EnvTexture.SampleLevel(SampColorSampler, R, 15));

			specularIrradiance = (specularIrradiance / max(specularIrradianceLuminance, 0.001)) * directionalAmbientColorSpecular;
			specularIrradiance = Color::GammaToLinear(specularIrradiance);
		}

		float3 specularIrradianceReflections = 1.0;

		if (skylightingSpecular > 0.0){
			specularIrradianceReflections = EnvReflectionsTexture.SampleLevel(SampColorSampler, R, level);

		    float specularIrradianceReflectionsLuminance = Color::RGBToLuminance(EnvReflectionsTexture.SampleLevel(SampColorSampler, R, 15));

			specularIrradianceReflections = (specularIrradianceReflections / max(specularIrradianceReflectionsLuminance, 0.001)) * directionalAmbientColorSpecular;
			specularIrradianceReflections = Color::GammaToLinear(specularIrradianceReflections);
		}

		finalIrradiance = lerp(specularIrradiance, specularIrradianceReflections, skylightingSpecular);
#		else
		float3 specularIrradiance = EnvReflectionsTexture.SampleLevel(SampColorSampler, R, level);

		float specularIrradianceLuminance = Color::RGBToLuminance(EnvReflectionsTexture.SampleLevel(SampColorSampler, R, 15));

		specularIrradiance = (specularIrradiance / max(specularIrradianceLuminance, 0.001)) * directionalAmbientColorSpecular;
		specularIrradiance = Color::GammaToLinear(specularIrradiance);

		finalIrradiance = specularIrradiance;
#		endif
		return horizon * (F0 * specularBRDF.x + specularBRDF.y) * finalIrradiance;
#	endif
	}
#endif  // !WATER
}
#endif  // DYNAMICCUBEMAPS_HLSLI