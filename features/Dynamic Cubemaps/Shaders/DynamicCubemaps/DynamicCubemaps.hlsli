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
	float3 GetDynamicCubemapSpecularIrradiance(float3 N, float3 V, float roughness, sh2 skylighting)
#	else
	float3 GetDynamicCubemapSpecularIrradiance(float3 N, float3 V, float roughness)
#	endif
	{
#	if defined(DEFERRED)
		return 1.0;
#	else
		float3 R = reflect(-V, N);
		float NoV = saturate(dot(N, V));

		float level = roughness * 7.0;

		float3 finalIrradiance = 0;

		float directionalAmbientColorSpecular = Color::RGBToLuminance(Color::Ambient(
													max(0, SharedData::GetAmbient(R)))) *
		                                        Color::ReflectionNormalisationScale;

#		if defined(IBL) && defined(LIGHTING)
		const bool inWorld = (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InWorld);
		const bool inReflection = (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InReflection);
		const bool useStaticIBL = SharedData::iblSettings.EnableIBL && SharedData::iblSettings.UseStaticIBL && !inWorld && !inReflection;
#		else
		const bool useStaticIBL = false;
#		endif

		if (!useStaticIBL) {
#		if defined(SKYLIGHTING)
		float skylightingSpecular = 0.0;
		if (!SharedData::InInterior) {
			skylightingSpecular = Skylighting::EvaluateSpecular(skylighting, SphericalHarmonics::FauxSpecularLobe(N, V, roughness));
		}
#		endif

#		if defined(IBL)
		if (SharedData::iblSettings.EnableIBL) {
			float3 envSample = EnvTexture.SampleLevel(SampColorSampler, R, level);
			float3 fullSample = EnvReflectionsTexture.SampleLevel(SampColorSampler, R, level);
			float3 envSpecular = 0.0;
			float3 skySpecular = 0.0;

			if (SharedData::iblSettings.DALCMode == 2) {
				// Mode 2: DALC-normalized env scaled by DALCAmount + sky overlay
				float envLum = Color::RGBToLuminance(EnvTexture.SampleLevel(SampColorSampler, R, 15));
				envSpecular = Color::IrradianceToLinear((envSample / max(envLum, 0.001)) * directionalAmbientColorSpecular) * SharedData::iblSettings.DALCAmount;
				skySpecular = Color::IrradianceToLinear(max(0, fullSample - envSample)) * SharedData::iblSettings.SkyIBLScale;
#			if defined(SKYLIGHTING)
				skySpecular *= skylightingSpecular;
#			endif
			} else {
				// Mode 0/1: IBL ratio-based
				float3 ratio = ImageBasedLighting::GetIBLRatio();
				envSpecular = Color::IrradianceToLinear(envSample * ratio) * SharedData::iblSettings.EnvIBLScale;
				skySpecular = Color::IrradianceToLinear(max(0, fullSample - envSample)) * SharedData::iblSettings.SkyIBLScale;
#			if defined(SKYLIGHTING)
				skySpecular *= skylightingSpecular;
#			endif
			}
			if (SharedData::InInterior) {
				skySpecular = 0;
			}

			finalIrradiance = envSpecular + skySpecular;
		} else
#		endif
		{
			// Fallback without IBL: normalize-by-luminance with DALC
#		if defined(SKYLIGHTING)
			if (SharedData::InInterior) {
				float3 specularIrradiance = EnvTexture.SampleLevel(SampColorSampler, R, level);
				float specularIrradianceLuminance = Color::RGBToLuminance(EnvTexture.SampleLevel(SampColorSampler, R, 15));
				specularIrradiance = (specularIrradiance / max(specularIrradianceLuminance, 0.001)) * directionalAmbientColorSpecular;
				finalIrradiance = Color::IrradianceToLinear(specularIrradiance);
			} else {
				float3 specularIrradianceReflections = 0.0;
				if (skylightingSpecular > 0.0) {
					specularIrradianceReflections = EnvReflectionsTexture.SampleLevel(SampColorSampler, R, level);
					float lum = Color::RGBToLuminance(EnvReflectionsTexture.SampleLevel(SampColorSampler, R, 15));
					specularIrradianceReflections = (specularIrradianceReflections / max(lum, 0.001)) * directionalAmbientColorSpecular;
					specularIrradianceReflections = Color::IrradianceToLinear(specularIrradianceReflections);
				}
				float3 specularIrradiance = 0.0;
				if (skylightingSpecular < 1.0) {
					specularIrradiance = EnvTexture.SampleLevel(SampColorSampler, R, level);
					float lum = Color::RGBToLuminance(EnvTexture.SampleLevel(SampColorSampler, R, 15));
					float dalcScaled = Color::IrradianceToGamma(Color::IrradianceToLinear(directionalAmbientColorSpecular) * skylightingSpecular);
					specularIrradiance = (specularIrradiance / max(lum, 0.001)) * dalcScaled;
					specularIrradiance = Color::IrradianceToLinear(specularIrradiance);
				}
				finalIrradiance = lerp(specularIrradiance, specularIrradianceReflections, skylightingSpecular);
			}
#		else
			float3 specularIrradiance = EnvReflectionsTexture.SampleLevel(SampColorSampler, R, level);
			float specularIrradianceLuminance = Color::RGBToLuminance(EnvReflectionsTexture.SampleLevel(SampColorSampler, R, 15));
			specularIrradiance = (specularIrradiance / max(specularIrradianceLuminance, 0.001)) * directionalAmbientColorSpecular;
			finalIrradiance = Color::IrradianceToLinear(specularIrradiance);
#		endif
		}
		} else {
#		if defined(IBL) && defined(LIGHTING)
			float3 specularIrradiance = ImageBasedLighting::StaticSpecularIBLTexture.SampleLevel(SampColorSampler, R.xzy, level).xyz;
			finalIrradiance = specularIrradiance;
#		endif
		}

		return finalIrradiance;
#	endif
	}

#	if defined(SKYLIGHTING)
	float3 GetDynamicCubemap(float3 N, float3 V, float roughness, float3 F0, sh2 skylighting)
#	else
	float3 GetDynamicCubemap(float3 N, float3 V, float roughness, float3 F0)
#	endif
	{
#	if defined(DEFERRED)
		return 1.0;
#	else
		float3 R = reflect(-V, N);
		float NoV = saturate(dot(N, V));

		float level = roughness * 7.0;

		float2 specularBRDF = BRDF::EnvBRDF(roughness, NoV);

		float3 finalIrradiance = 0;
		float directionalAmbientColorSpecular = Color::RGBToLuminance(Color::Ambient(max(0, SharedData::GetAmbient(R)))) * Color::ReflectionNormalisationScale;

#		if defined(IBL) && defined(LIGHTING)
		const bool inWorld = (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InWorld);
		const bool inReflection = (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::InReflection);
		if (SharedData::iblSettings.EnableIBL && SharedData::iblSettings.UseStaticIBL && !inWorld && !inReflection) {
			float3 specularIrradiance = ImageBasedLighting::StaticSpecularIBLTexture.SampleLevel(SampColorSampler, R.xzy, level).xyz;
			return (F0 * specularBRDF.x + specularBRDF.y) * specularIrradiance;
		}
#		endif

#		if defined(SKYLIGHTING)
		float skylightingSpecular = 0.0;
		if (!SharedData::InInterior) {
			skylightingSpecular = Skylighting::EvaluateSpecular(skylighting, SphericalHarmonics::FauxSpecularLobe(N, V, roughness));
		}
#		endif

#		if defined(IBL)
		if (SharedData::iblSettings.EnableIBL) {
			float3 envSample = EnvTexture.SampleLevel(SampColorSampler, R, level);
			float3 fullSample = EnvReflectionsTexture.SampleLevel(SampColorSampler, R, level);
			float3 envSpecular = 0.0;
			float3 skySpecular = 0.0;

			if (SharedData::iblSettings.DALCMode == 2) {
				// Mode 2: DALC-normalized env scaled by DALCAmount + sky overlay
				float envLum = Color::RGBToLuminance(EnvTexture.SampleLevel(SampColorSampler, R, 15));
				envSpecular = Color::IrradianceToLinear((envSample / max(envLum, 0.001)) * directionalAmbientColorSpecular) * SharedData::iblSettings.DALCAmount;
				skySpecular = Color::IrradianceToLinear(max(0, fullSample - envSample)) * SharedData::iblSettings.SkyIBLScale;
#			if defined(SKYLIGHTING)
				skySpecular *= skylightingSpecular;
#			endif
			} else {
				// Mode 0/1: IBL ratio-based
				float3 ratio = ImageBasedLighting::GetIBLRatio();
				envSpecular = Color::IrradianceToLinear(envSample * ratio) * SharedData::iblSettings.EnvIBLScale;
				skySpecular = Color::IrradianceToLinear(max(0, fullSample - envSample)) * SharedData::iblSettings.SkyIBLScale;
#			if defined(SKYLIGHTING)
				skySpecular *= skylightingSpecular;
#			endif
			}
			if (SharedData::InInterior) {
				skySpecular = 0;
			}

			finalIrradiance = envSpecular + skySpecular;
		} else
#		endif
		{
			// Fallback without IBL: normalize-by-luminance with DALC
#		if defined(SKYLIGHTING)
			if (SharedData::InInterior) {
				float3 specularIrradiance = EnvTexture.SampleLevel(SampColorSampler, R, level);
				float specularIrradianceLuminance = Color::RGBToLuminance(EnvTexture.SampleLevel(SampColorSampler, R, 15));
				specularIrradiance = (specularIrradiance / max(specularIrradianceLuminance, 0.001)) * directionalAmbientColorSpecular;
				finalIrradiance = Color::IrradianceToLinear(specularIrradiance);
			} else {
				float3 specularIrradianceReflections = 0.0;
				if (skylightingSpecular > 0.0) {
					specularIrradianceReflections = EnvReflectionsTexture.SampleLevel(SampColorSampler, R, level);
					float lum = Color::RGBToLuminance(EnvReflectionsTexture.SampleLevel(SampColorSampler, R, 15));
					specularIrradianceReflections = (specularIrradianceReflections / max(lum, 0.001)) * directionalAmbientColorSpecular;
					specularIrradianceReflections = Color::IrradianceToLinear(specularIrradianceReflections);
				}
				float3 specularIrradiance = 0.0;
				if (skylightingSpecular < 1.0) {
					specularIrradiance = EnvTexture.SampleLevel(SampColorSampler, R, level);
					float lum = Color::RGBToLuminance(EnvTexture.SampleLevel(SampColorSampler, R, 15));
					float dalcScaled = Color::IrradianceToGamma(Color::IrradianceToLinear(directionalAmbientColorSpecular) * skylightingSpecular);
					specularIrradiance = (specularIrradiance / max(lum, 0.001)) * dalcScaled;
					specularIrradiance = Color::IrradianceToLinear(specularIrradiance);
				}
				finalIrradiance = lerp(specularIrradiance, specularIrradianceReflections, skylightingSpecular);
			}
#		else
			float3 specularIrradiance = EnvReflectionsTexture.SampleLevel(SampColorSampler, R, level);
			float specularIrradianceLuminance = Color::RGBToLuminance(EnvReflectionsTexture.SampleLevel(SampColorSampler, R, 15));
			specularIrradiance = (specularIrradiance / max(specularIrradianceLuminance, 0.001)) * directionalAmbientColorSpecular;
			finalIrradiance = Color::IrradianceToLinear(specularIrradiance);
#		endif
		}

		return (F0 * specularBRDF.x + specularBRDF.y) * finalIrradiance;
#	endif
	}
#endif  // !WATER
}
#endif  // DYNAMICCUBEMAPS_HLSLI
