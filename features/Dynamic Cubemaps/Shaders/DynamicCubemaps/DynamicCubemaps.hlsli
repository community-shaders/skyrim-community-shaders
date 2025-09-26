#include "Common/BRDF.hlsli"

#if defined(SKYLIGHTING)
#	include "Skylighting/Skylighting.hlsli"
#endif

#if defined(IBL)
#	include "IBL/IBL.hlsli"
#endif

namespace DynamicCubemaps
{
	TextureCube<float4> EnvReflectionsTexture : register(t30);
	TextureCube<float4> EnvTexture : register(t31);

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

		float directionalAmbientColorSpecular = Color::RGBToLuminance(max(0, mul(SharedData::DirectionalAmbient, float4(R, 1.0))) * Color::ReflectionNormalisationScale);

#		if defined(IBL) && defined(LIGHTING)
		const bool inWorld = (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InWorld);
		const bool inReflection = (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InReflection);
		if (SharedData::iblSettings.EnableDiffuseIBL && SharedData::iblSettings.UseStaticIBL && !inWorld && !inReflection) {
			float3 specularIrradiance = ImageBasedLighting::StaticSpecularIBLTexture.SampleLevel(SampColorSampler, R.xzy, level).xyz;
			finalIrradiance += specularIrradiance;
			return finalIrradiance;
		}
#		endif

#		if defined(SKYLIGHTING)
		if (SharedData::InInterior) {
			float3 specularIrradiance = Color::GammaToLinear(EnvTexture.SampleLevel(SampColorSampler, R, level).xyz);

			float specularIrradianceLuminance = Color::RGBToLuminance(Color::GammaToLinear(EnvTexture.SampleLevel(SampColorSampler, R, 15).xyz));

			specularIrradiance = (specularIrradiance / specularIrradianceLuminance + 0.001) * directionalAmbientColorSpecular;

			finalIrradiance += specularIrradiance;
			return finalIrradiance;
		}

		sh2 specularLobe = SphericalHarmonics::FauxSpecularLobe(N, -V, roughness);

		float skylightingSpecular = SphericalHarmonics::FuncProductIntegral(skylighting, specularLobe);
		skylightingSpecular = Skylighting::mixSpecular(SharedData::skylightingSettings, skylightingSpecular);

		directionalAmbientColorSpecular *= skylightingSpecular;

		float3 specularIrradiance = 1;

		if (skylightingSpecular < 1.0){
			specularIrradiance = Color::GammaToLinear(EnvTexture.SampleLevel(SampColorSampler, R, level).xyz);

            float specularIrradianceLuminance = Color::RGBToLuminance(Color::GammaToLinear(EnvTexture.SampleLevel(SampColorSampler, R, 15).xyz));

			specularIrradiance = (specularIrradiance / specularIrradianceLuminance + 0.001) * directionalAmbientColorSpecular;
		}

		float3 specularIrradianceReflections = 1.0;

		if (skylightingSpecular > 0.0){
			specularIrradianceReflections = Color::GammaToLinear(EnvReflectionsTexture.SampleLevel(SampColorSampler, R, level).xyz);

		    float specularIrradianceReflectionsLuminance = Color::RGBToLuminance(Color::GammaToLinear(EnvReflectionsTexture.SampleLevel(SampColorSampler, R, 15).xyz));

			specularIrradianceReflections = (specularIrradianceReflections / specularIrradianceReflectionsLuminance + 0.001) * directionalAmbientColorSpecular;
		}

		finalIrradiance = lerp(specularIrradiance, specularIrradianceReflections, skylightingSpecular);
#		else
		float3 specularIrradiance = Color::GammaToLinear(EnvReflectionsTexture.SampleLevel(SampColorSampler, R, level).xyz);

		float specularIrradianceLuminance = Color::RGBToLuminance(Color::GammaToLinear(EnvTexture.SampleLevel(SampColorSampler, R, 15).xyz));

		specularIrradiance = (specularIrradiance / specularIrradianceLuminance + 0.001) * directionalAmbientColorSpecular;

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

		float2 specularBRDF = BRDF::EnvBRDF(roughness, NoV);

		// Horizon specular occlusion
		// https://marmosetco.tumblr.com/post/81245981087
		float horizon = min(1.0 + dot(R, VN), 1.0);
		horizon *= horizon * horizon;

#	if defined(DEFERRED)
		return horizon * (F0 * specularBRDF.x + specularBRDF.y);
#	else

		float3 finalIrradiance = 0;

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
			float3 specularIrradiance = Color::GammaToLinear(EnvTexture.SampleLevel(SampColorSampler, R, level).xyz);

			finalIrradiance += specularIrradiance;

			return horizon * (F0 * specularBRDF.x + specularBRDF.y) * finalIrradiance;
		}

		sh2 specularLobe = SphericalHarmonics::FauxSpecularLobe(N, -V, roughness);

		float skylightingSpecular = SphericalHarmonics::FuncProductIntegral(skylighting, specularLobe);
		skylightingSpecular = Skylighting::mixSpecular(SharedData::skylightingSettings, skylightingSpecular);

		float3 specularIrradiance = 1;

		if (skylightingSpecular < 1.0)
			specularIrradiance = Color::GammaToLinear(EnvTexture.SampleLevel(SampColorSampler, R, level).xyz);

		float3 specularIrradianceReflections = 1.0;

		if (skylightingSpecular > 0.0)
			specularIrradianceReflections = Color::GammaToLinear(EnvReflectionsTexture.SampleLevel(SampColorSampler, R, level).xyz);

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
