#ifndef __IBL_HLSLI__
#define __IBL_HLSLI__

#include "Common/ColorManagement.hlsli"
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
		return float3(colorR, colorG, colorB) / Math::PI;
	}

	// ============================================================================
	// Reflection cubemap distance fallback
	// ============================================================================

	/// Mirrors Water.hlsl's dynamic -> static reflection distance fallback.
	/// Returns 0 near the camera (full dynamic env IBL) and 1 at/after the
	/// configured distance (pure ReflectionsCubemap IBL).
	float GetIBLReflectionFallbackFactor(float3 positionMS)
	{
		// Interiors have no sky IBL, so the fallback would fade to black.
		if (SharedData::iblSettings.EnableReflectionFallback == 0 || SharedData::InInterior)
			return 0.0f;
		return saturate(length(positionMS) / max(SharedData::iblSettings.ReflectionFallbackDistance, 1.0f));
	}

	float3 ApplyIBLReflectionFallback(float3 iblColor, float3 reflectionsColor, float3 positionMS = float3(0, 0, 0))
	{
		return lerp(iblColor, reflectionsColor, GetIBLReflectionFallbackFactor(positionMS));
	}

	/** @brief Distance fallback from env + sky toward linReflections; DALC modes (2+) keep their vanilla ambient term, which has no dynamic env to fall back from. */
	float3 ApplyAmbientReflectionFallback(float3 linEnv, float3 linSky, float3 linReflections, float3 positionMS)
	{
		if (SharedData::iblSettings.DALCMode >= 2)
			return linEnv + linSky;
		return ApplyIBLReflectionFallback(linEnv + linSky, linReflections, positionMS);
	}

	/** @brief Diffuse distance fallback, where the sky SH is already the native reflections cubemap term. */
	float3 ApplyAmbientReflectionFallback(float3 linEnv, float3 linSky, float3 positionMS)
	{
		return ApplyAmbientReflectionFallback(linEnv, linSky, linSky, positionMS);
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
		float3 ibl0 = float3(colorR, colorG, colorB) / Math::PI;

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

	// ============================================================================
	// High-level: compute the full diffuse ambient replacement
	// ============================================================================

	/// Compute diffuse IBL ambient (gamma-space) without directional occlusion.
	/// Falls back toward ReflectionCubemap-only IBL at the configured distance.
	float3 GetDiffuseIBL(float3 vanillaDALC, float3 rayDir, float3 positionMS = float3(0, 0, 0))
	{
		float3 linEnv, linSky;
		if (SharedData::iblSettings.DALCMode >= 2) {
			linEnv = vanillaDALC * SharedData::iblSettings.DALCAmount;
			linSky = GetSkyIBLColor(rayDir);
		} else {
			linEnv = GetEnvIBLColor(rayDir);
			linSky = GetSkyIBLColor(rayDir);
		}
#if defined(EFFECTS11)
		if (SharedData::iblSettings.ENBSkyFalloff)
			linSky *= saturate(-rayDir.z * 0.65 + 0.35);
#endif
		return ApplyAmbientReflectionFallback(linEnv, linSky, positionMS);
	}

	/// Compute diffuse IBL ambient with a skylighting visibility factor applied per DALCMode
	/// (mode 3 dims both DALC and sky; modes 0-2 dim only the sky contribution).
	float3 GetDiffuseIBLOccluded(float3 vanillaDALC, float3 rayDir, float visibility, float3 positionMS = float3(0, 0, 0))
	{
		float3 linEnv, linSky;
		if (SharedData::iblSettings.DALCMode == 3) {
			linEnv = vanillaDALC * SharedData::iblSettings.DALCAmount * visibility;
		} else if (SharedData::iblSettings.DALCMode == 2) {
			linEnv = vanillaDALC * SharedData::iblSettings.DALCAmount;
		} else {
			linEnv = GetEnvIBLColor(rayDir);
		}
		linSky = GetSkyIBLColor(rayDir) * visibility;
		return ApplyAmbientReflectionFallback(linEnv, linSky, positionMS);
	}

	/// Combined env + sky IBL color with a visibility factor applied to the sky term.
	float3 GetIBLColorOccluded(float3 rayDir, float visibility, float3 positionMS = float3(0, 0, 0))
	{
		const float3 skyColor = GetSkyIBLColor(rayDir) * visibility;
		return ApplyIBLReflectionFallback(GetEnvIBLColor(rayDir) + skyColor, skyColor, positionMS);
	}

	// Integrate radiance against the normalized HG phase, rather than a surface cosine lobe.
	// DiffuseIBLCS projects Sample(-rayDir), so its SH direction is opposite the cubemap direction.
	float3 GetPhaseIBLColorOccluded(float3 viewDirection, float phaseG, float visibility, float3 positionMS)
	{
		const sh2 phase = SphericalHarmonics::EvaluatePhaseHG(-viewDirection, phaseG);
		float3 envColor = max(0.0, float3(
									   dot(EnvIBLTexture.Load(int3(0, 0, 0)), phase),
									   dot(EnvIBLTexture.Load(int3(1, 0, 0)), phase),
									   dot(EnvIBLTexture.Load(int3(2, 0, 0)), phase)));
		envColor = Color::Saturation(envColor, SharedData::iblSettings.EnvIBLSaturation) * SharedData::iblSettings.EnvIBLScale * GetIBLRatio();
		float3 skyColor = 0.0;
		if (!SharedData::InInterior) {
			skyColor = max(0.0, float3(
									dot(SkyIBLTexture.Load(int3(0, 0, 0)), phase),
									dot(SkyIBLTexture.Load(int3(1, 0, 0)), phase),
									dot(SkyIBLTexture.Load(int3(2, 0, 0)), phase)));
			skyColor = Color::Saturation(skyColor, SharedData::iblSettings.SkyIBLSaturation) * SharedData::iblSettings.SkyIBLScale * visibility;
		}
		return ApplyIBLReflectionFallback(envColor + skyColor, skyColor, positionMS);
	}

#if defined(LIGHTING)
	float3 GetStaticDiffuseIBL(float3 N, SamplerState samp)
	{
		return StaticDiffuseIBLTexture.SampleLevel(samp, N.xzy, 0).xyz / Math::PI;
	}
#endif

	float3 GetFogIBLColorBlend(float3 fogColor, float3 positionMS)
	{
		float3 linEnv;
		if (SharedData::iblSettings.DALCMode >= 2)
			linEnv = Color::Ambient(SharedData::GetAmbient(0.f)) * SharedData::iblSettings.DALCAmount;
		else
			linEnv = GetEnvIBLColor(float3(0, 0, 0));
		float3 iblColor = ApplyAmbientReflectionFallback(linEnv, GetSkyIBLColor(float3(0, 0, 0)), positionMS);
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

	float3 GetFogIBLColor(float3 fogColor, float3 positionMS = float3(0, 0, 0))
	{
		float3 result = fogColor;
		[branch] if (SharedData::iblSettings.FogAmount != 0.0)
			result = GetFogIBLColorBlend(fogColor, positionMS);
		return result;
	}
}

#endif  // __IBL_HLSLI__
