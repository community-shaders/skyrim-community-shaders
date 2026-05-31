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

	// ============================================================================
	// Low-level SH sampling (raw, no user settings applied)
	// ============================================================================

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

	float3 GetSkyIBLOccluded(float3 rayDir, float visibility)
	{
		return GetSkyIBL(rayDir) * visibility;
	}

	// ============================================================================
	// Ratio / settings helpers
	// ============================================================================

	/// Compute ratio between DALC and IBL for brightness/color matching.
	float3 GetIBLRatio()
	{
		float3 dalc0 = Color::Ambient(SharedData::GetAmbient(0.f));

		sh2 iblSHR = EnvIBLTexture.Load(int3(0, 0, 0));
		sh2 iblSHG = EnvIBLTexture.Load(int3(1, 0, 0));
		sh2 iblSHB = EnvIBLTexture.Load(int3(2, 0, 0));

		float colorR = SphericalHarmonics::SHHallucinateZH3Irradiance(iblSHR, float3(0, 0, 0));
		float colorG = SphericalHarmonics::SHHallucinateZH3Irradiance(iblSHG, float3(0, 0, 0));
		float colorB = SphericalHarmonics::SHHallucinateZH3Irradiance(iblSHB, float3(0, 0, 0));
		float3 ibl0 = max(0, float3(colorR, colorG, colorB) / Math::PI);

		if (SharedData::iblSettings.DALCMode == 1) {
			float3 ratio = dalc0 / max(ibl0, 0.001);
			return lerp(1.0, ratio, SharedData::iblSettings.DALCAmount);
		} else {
			float dalcLum = Color::RGBToLuminance(dalc0);
			float iblLum = Color::RGBToLuminance(ibl0);
			float ratio = (iblLum > 0.001) ? (dalcLum / iblLum) : 1.0;
			return lerp(1.0, ratio, SharedData::iblSettings.DALCAmount);
		}
	}

	// ============================================================================
	// Mid-level: individual components with user settings applied
	// ============================================================================

	float3 GetEnvIBLColor(float3 rayDir)
	{
		float3 ratio = GetIBLRatio();
		return Color::Saturation(GetEnvIBL(rayDir), SharedData::iblSettings.EnvIBLSaturation) * SharedData::iblSettings.EnvIBLScale * ratio;
	}

	float3 GetSkyIBLColor(float3 rayDir)
	{
		if (SharedData::InInterior) {
			return 0;
		}
		return Color::Saturation(GetSkyIBL(rayDir), SharedData::iblSettings.SkyIBLSaturation) * SharedData::iblSettings.SkyIBLScale;
	}

	float3 GetSkyIBLColorOccluded(float3 rayDir, float visibility)
	{
		if (SharedData::InInterior) {
			return 0;
		}
		return Color::Saturation(GetSkyIBLOccluded(rayDir, visibility), SharedData::iblSettings.SkyIBLSaturation) * SharedData::iblSettings.SkyIBLScale;
	}

	// ============================================================================
	// High-level: compute the full diffuse ambient replacement
	// ============================================================================

	/// Compute diffuse IBL ambient (gamma-space) without directional occlusion.
	float3 GetDiffuseIBL(float3 vanillaDALC, float3 rayDir)
	{
		float3 linEnv, linSky;
		if (SharedData::iblSettings.DALCMode >= 2) {
			linEnv = Color::IrradianceToLinear(vanillaDALC * SharedData::iblSettings.DALCAmount);
			linSky = GetSkyIBLColor(rayDir);
		} else {
			linEnv = GetEnvIBLColor(rayDir);
			linSky = GetSkyIBLColor(rayDir);
		}
		return Color::IrradianceToGamma(linEnv + linSky);
	}

	/// Compute diffuse IBL ambient (gamma-space) with visibility applied per DALCMode.
	/// visibility: scalar skylighting factor (already computed in Lighting.hlsl).
	float3 GetDiffuseIBLOccluded(float3 vanillaDALC, float3 rayDir, float visibility)
	{
		float3 linEnv, linSky;
		if (SharedData::iblSettings.DALCMode == 3) {
			// Mode 3: Skylighting dims both DALC and sky
			linEnv = Color::IrradianceToLinear(vanillaDALC * SharedData::iblSettings.DALCAmount) * visibility;
			linSky = GetSkyIBLColorOccluded(rayDir, visibility);
		} else if (SharedData::iblSettings.DALCMode == 2) {
			// Mode 2: Skylighting only dims sky, DALC unaffected
			linEnv = Color::IrradianceToLinear(vanillaDALC * SharedData::iblSettings.DALCAmount);
			linSky = GetSkyIBLColorOccluded(rayDir, visibility);
		} else {
			// Mode 0/1: Skylighting only dims sky, env IBL unaffected
			linEnv = GetEnvIBLColor(rayDir);
			linSky = GetSkyIBLColorOccluded(rayDir, visibility);
		}
		return Color::IrradianceToGamma(linEnv + linSky);
	}

	// ============================================================================
	// Convenience: combined IBL (for simple contexts)
	// ============================================================================

	float3 GetIBLColor(float3 rayDir)
	{
		return GetEnvIBLColor(rayDir) + GetSkyIBLColor(rayDir);
	}

	float3 GetIBLColorOccluded(float3 rayDir, float visibility)
	{
		return GetEnvIBLColor(rayDir) + GetSkyIBLColorOccluded(rayDir, visibility);
	}

#if defined(LIGHTING)
	float3 GetStaticDiffuseIBL(float3 N, SamplerState samp)
	{
		return StaticDiffuseIBLTexture.SampleLevel(samp, N.xzy, 0).xyz / Math::PI;
	}
#endif

	float3 GetFogIBLColor(float3 fogColor)
	{
		float3 iblColor;
		if (SharedData::iblSettings.DALCMode >= 2) {
			float3 dalc0 = Color::Ambient(SharedData::GetAmbient(0.f));
			iblColor = Color::IrradianceToLinear(dalc0 * SharedData::iblSettings.DALCAmount) + GetSkyIBLColor(float3(0, 0, 0));
		} else {
			iblColor = GetEnvIBLColor(float3(0, 0, 0)) + GetSkyIBLColor(float3(0, 0, 0));
		}
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
