#ifndef __IBL_HLSLI__
#define __IBL_HLSLI__

#include "Common/Color.hlsli"
#include "Common/Math.hlsli"
#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/Spherical Harmonics/SphericalHarmonics.hlsli"

namespace ImageBasedLighting
{
#if defined(IBL_DEFERRED)
	Texture2D<sh2> EnvIBLTexture : register(t14);
	Texture2D<sh2> SkyIBLTexture : register(t15);
#else
	Texture2D<sh2> EnvIBLTexture : register(t76);
	Texture2D<sh2> SkyIBLTexture : register(t77);
	TextureCube<float4> StaticDiffuseIBLTexture : register(t78);
	TextureCube<float4> StaticSpecularIBLTexture : register(t79);
#endif

	/// Get Env IBL color from environment cubemap SH (without sky)
	float3 GetEnvIBL(float3 rayDir)
	{
		sh2 shR = EnvIBLTexture.Load(int3(0, 0, 0));
		sh2 shG = EnvIBLTexture.Load(int3(1, 0, 0));
		sh2 shB = EnvIBLTexture.Load(int3(2, 0, 0));
		float colorR = SphericalHarmonics::SHHallucinateZH3Irradiance(shR, rayDir);
		float colorG = SphericalHarmonics::SHHallucinateZH3Irradiance(shG, rayDir);
		float colorB = SphericalHarmonics::SHHallucinateZH3Irradiance(shB, rayDir);
		return float3(colorR, colorG, colorB) / Math::PI;
	}

	/// Get Sky-only IBL color from game's native reflections cubemap SH
	float3 GetSkyIBL(float3 rayDir)
	{
		sh2 shR = SkyIBLTexture.Load(int3(0, 0, 0));
		sh2 shG = SkyIBLTexture.Load(int3(1, 0, 0));
		sh2 shB = SkyIBLTexture.Load(int3(2, 0, 0));
		float colorR = SphericalHarmonics::SHHallucinateZH3Irradiance(shR, rayDir);
		float colorG = SphericalHarmonics::SHHallucinateZH3Irradiance(shG, rayDir);
		float colorB = SphericalHarmonics::SHHallucinateZH3Irradiance(shB, rayDir);
		return max(0, float3(colorR, colorG, colorB) / Math::PI);
	}

	/// Compute ratio between DALC and IBL for brightness/color matching.
	/// Mode 0 (Luminance Ratio): scalar ratio from luminance, broadcast to float3 (loses DALC tint).
	/// Mode 1 (Color Ratio): per-channel ratio, preserves DALC color tint.
	/// DALCAmount interpolates between 1.0 (no matching) and the computed ratio.
	float3 GetIBLRatio()
	{
		// 0th order DALC (DC term)
		float3 dalc0 = Color::Ambient(SharedData::GetAmbient(0.f));

		// 0th order IBL SH (DC term from env cubemap)
		sh2 iblSHR = EnvIBLTexture.Load(int3(0, 0, 0));
		sh2 iblSHG = EnvIBLTexture.Load(int3(1, 0, 0));
		sh2 iblSHB = EnvIBLTexture.Load(int3(2, 0, 0));

		float colorR = SphericalHarmonics::SHHallucinateZH3Irradiance(iblSHR, float3(0, 0, 0));
		float colorG = SphericalHarmonics::SHHallucinateZH3Irradiance(iblSHG, float3(0, 0, 0));
		float colorB = SphericalHarmonics::SHHallucinateZH3Irradiance(iblSHB, float3(0, 0, 0));
		float3 ibl0 = max(0, float3(colorR, colorG, colorB) / Math::PI);

		if (SharedData::iblSettings.DALCMode == 1) {
			// Mode 1: per-channel ratio preserving DALC color tint
			float3 ratio = dalc0 / max(ibl0, 0.001);
			return lerp(1.0, ratio, SharedData::iblSettings.DALCAmount);
		} else {
			// Mode 0: scalar luminance ratio (default)
			float dalcLum = Color::RGBToLuminance(dalc0);
			float iblLum = Color::RGBToLuminance(ibl0);
			float ratio = (iblLum > 0.001) ? (dalcLum / iblLum) : 1.0;
			return lerp(1.0, ratio, SharedData::iblSettings.DALCAmount);
		}
	}

	/// Get Env IBL color with settings applied (saturation, scale, ratio)
	float3 GetEnvIBLColor(float3 rayDir)
	{
		float3 ratio = GetIBLRatio();
		return Color::Saturation(GetEnvIBL(rayDir), SharedData::iblSettings.EnvIBLSaturation) * SharedData::iblSettings.EnvIBLScale * ratio;
	}

	/// Get Sky IBL color with settings applied (saturation, scale; no ratio)
	float3 GetSkyIBLColor(float3 rayDir)
	{
		return Color::Saturation(GetSkyIBL(rayDir), SharedData::iblSettings.SkyIBLSaturation) * SharedData::iblSettings.SkyIBLScale;
	}

	/// Get combined IBL color: Env IBL + Sky IBL (for contexts without skylighting)
	float3 GetIBLColor(float3 rayDir)
	{
		return GetEnvIBLColor(rayDir) + GetSkyIBLColor(rayDir);
	}

#if defined(LIGHTING)
	float3 GetStaticDiffuseIBL(float3 N, SamplerState samp)
	{
		return StaticDiffuseIBLTexture.SampleLevel(samp, N.xzy, 0).xyz / Math::PI;
	}
#endif

	float3 GetFogIBLColor(float3 fogColor)
	{
		float3 iblColor = GetEnvIBLColor(float3(0, 0, 0)) + GetSkyIBLColor(float3(0, 0, 0));
		if (SharedData::iblSettings.PreserveFogLuminance) {
			const float fogLuminance = Color::RGBToLuminance(fogColor);
			const float iblLuminance = Color::RGBToLuminance(iblColor);
			if (iblLuminance > 0) {
				const float scale = fogLuminance / iblLuminance;
				iblColor *= scale;
			} else {
				iblColor = fogColor;
			}
		}
		return lerp(fogColor, iblColor, SharedData::iblSettings.FogAmount);
	}
}

#endif  // __IBL_HLSLI__