#ifndef EFFECTS11_PROCEDURAL_SUN_HLSLI
#define EFFECTS11_PROCEDURAL_SUN_HLSLI

#include "Common/SunDisc.hlsli"

// Effects 11 [PROCEDURALSUN] extras on top of the shared SunDisc profile.
namespace ProceduralSun
{
	// Below the FP16 and R11G11B10 maxima, so the disc never writes infinity
	static const float MaxRadiance = 60000.0f;
	// Cloud alpha is authored against the sky; toward the far brighter sun the cloud counts its optical depth this many times
	static const float CloudExtinction = 4.0f;
	// The cloud deepens once the sun behind it is this many times brighter, fully at twice that; below, deepening hides more sky than sun
	static const float CloudSunContrast = 4.0f;

	float GetInfluenceCos(float sunDiskCos, bool haloEnabled, float sunHaloCos, float haloIntensity)
	{
		bool haloActive = haloEnabled && haloIntensity > 0.0f && sunHaloCos < sunDiskCos;
		return haloActive ? sunHaloCos : sunDiskCos;
	}

	/** @brief The halo capped at its value one disc radius past the disc edge: smooth over the disc and its sharp rim. */
	float EvaluateHaloPlateau(float cosTheta, float sunDiskCos, float sunHaloCos, float haloFalloff)
	{
		float twiceDiscCos = 2.0f * sunDiskCos * sunDiskCos - 1.0f;
		return min(SunDisc::EvaluateHalo(cosTheta, sunDiskCos, sunHaloCos, haloFalloff), SunDisc::EvaluateHalo(twiceDiscCos, sunDiskCos, sunHaloCos, haloFalloff));
	}

	float4 ToAdditiveBlend(float4 color, float radianceLimit)
	{
		float3 emitted = clamp(color.xyz, 0.0f, MaxRadiance) * saturate(color.w);
		float peak = max(emitted.x, max(emitted.y, emitted.z));
		if (peak <= 0.0f)
			return 0.0f;

		float alpha = clamp(peak / max(radianceLimit, 1.0f), 1.0f / 256.0f, 1.0f);
		return float4(emitted / alpha, alpha);
	}
}

#endif
