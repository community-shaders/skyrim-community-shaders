// Provides a boost to the nits of the sun irrespective of weather mod or sun texture, designed to push the sun
// to the peak nits set by the slider, without changing the look of the sun.

#ifdef HDR_OUTPUT

namespace HDRSun
{
	// SharedData::HDRData units used here:
	// .x - HDR enabled flag (0/1)
	// .y - paper white in nits
	// .z - display peak luminance in nits
	// .w - menu-state blend amount (>0 means menu/pause/map is active)
	// Menu / pause / map: HDRData.w > 0 — scale sun toward this nit level vs peak.
	static const float kMenuSunNits = 100.0f;
	// Keep a floor to avoid dimming below SDR behavior.
	static const float kMinHdrSunBoost = 1.0f;
	inline bool IsHdrSunActive()
	{
		return SharedData::HDRData.x > 0.5f && (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::IsSun);
	}

	// Returns an HDR sun gain in normalized linear space. Sky.hlsl applies it after depth/cloud occlusion.
	float GetHdrSunGain(
		float2 texCoord0_xy,
		float4 baseColor)
	{
		if (!IsHdrSunActive())
			return 1.0f;

		// --- Max linear multiplier for this display ---
		// Scene target: peak/paperWhite, menu target: 100/peak (via kMenuSunNits).
		float paperWhiteNits = max(SharedData::HDRData.y, 1.0f);
		float peakNits = max(SharedData::HDRData.z, paperWhiteNits + 1.0f);
		float peakRatio = peakNits / paperWhiteNits;
		float menuSunMul = (SharedData::HDRData.w > 1e-3f) ? (kMenuSunNits / peakNits) : 1.0f;
		float maxBoost = max(kMinHdrSunBoost, peakRatio * menuSunMul);

		// --- weight 0..1: local brightness / alpha / UV rim ---
		float L = max(Color::RGBToLuminance(baseColor.xyz), 0.0f);
		float highlight = max(1.0f - exp(-L), saturate(L));
		float a = saturate(baseColor.w);
		float alphaWeight = a * a;

		float radialWeight = 1.0f;
#	if defined(TEX)
		float2 uv = saturate(texCoord0_xy);
		float r = saturate(length(uv - 0.5f) * sqrt(2.0f));
		radialWeight = saturate(1.0f - r);
#	endif

		float weight = saturate(highlight * alphaWeight * radialWeight);

		// pow(maxBoost, weight) grows the boosted footprint as maxBoost rises (same weight → more
		// gain → sun reads larger). Tighten with wCurve = weight^sharpen; sharpen == 1 when
		// maxBoost == kMinHdrSunBoost so low-boost paths match plain pow(maxBoost, weight).
		float sharpen = max(1.0f, 1.0f + log2(maxBoost / kMinHdrSunBoost));
		float wCurve = pow(saturate(weight), sharpen);

		return pow(maxBoost, wCurve);
	}
}

#endif
