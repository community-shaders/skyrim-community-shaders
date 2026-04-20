#ifdef HDR_OUTPUT

#	include "Common/Random.hlsli"

namespace HDRSun
{
	// SharedData::HDRData units used here:
	// .x - HDR enabled flag (0/1)
	// .z - display peak luminance in nits
	// .w - menu-state blend amount (>0 means menu/pause/map is active)
	// 203 nits ref for sun math only (not the paper-white UI slider).
	static const float kReferencePaperWhiteNits = 203.0f;
	// Menu / pause / map: HDRData.w > 0 — scale sun toward this nit level vs peak.
	static const float kMenuSunNits = 100.0f;
	// Keep a floor to avoid dimming below SDR behavior.
	static const float kMinHdrSunBoost = 1.0f;

	inline bool IsHdrSunActive()
	{
		return SharedData::HDRData.x > 0.5f && (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::IsSun);
	}

	// Returns an HDR sun gain in normalized linear space. Caller owns all sampling/tint/noise output logic.
	float GetHdrSunGain(
		float2 texCoord0_xy,
		float4 baseColor,
		Texture2D<float4> sunTex,
		SamplerState samp,
		float alphaPostScale)
	{
		if (!IsHdrSunActive())
			return 1.0f;

		// --- Max linear multiplier for this display ---
		// Scene target: peak/203, menu target: 100/peak (via kMenuSunNits).
		float peakNits = max(SharedData::HDRData.z, kReferencePaperWhiteNits + 1.0f);
		float peakRatio = peakNits / kReferencePaperWhiteNits;
		float menuSunMul = (SharedData::HDRData.w > 1e-3f) ? (kMenuSunNits / peakNits) : 1.0f;
		float maxBoost = max(kMinHdrSunBoost, peakRatio * menuSunMul);

		// --- weight 0..1: local brightness / alpha / UV rim, then damp if fine ≈ blurred mip (wide soft sun) ---
		float L = max(Color::RGBToLuminance(baseColor.xyz), 0.0f);
		float highlight = max(1.0f - exp(-L), saturate(L));
		float a = saturate(baseColor.w);
		float alphaWeight = a * a;

		float radialWeight = 1.0f;
#	if defined(TEX)
		float2 uv = saturate(texCoord0_xy);
		float r = saturate(length(uv - 0.5f) * 1.41421356f);
		radialWeight = saturate(1.0f - r);
#	endif

		float weight = saturate(highlight * alphaWeight * radialWeight);

		float fineLod = sunTex.CalculateLevelOfDetail(samp, texCoord0_xy);
		float coarseMip = max(fineLod + 2.0f, 0.0f);
		float4 coarseS = sunTex.SampleLevel(samp, texCoord0_xy, coarseMip);
		float3 coarseRgb = Color::Sky(coarseS.rgb);
		float Lc = max(Color::RGBToLuminance(coarseRgb), 1e-4f);
		float lac = Lc * saturate(coarseS.w * alphaPostScale);  // alphaPostScale = TEXFADE factor from Sky, else 1
		float laf = L * a;

		float mipQuell = 1.0f;
		if (L >= Lc) {
			float relL = (L - Lc) / max(Lc, 1e-4f);
			float relA = (laf - lac) / max(lac, 1e-4f);
			float spike = relL + relA;
			mipQuell = saturate(1.0f - exp(-spike));
		}
		weight *= max(mipQuell, saturate(L));

		// Pow shaping keeps the corona from exploding on high-nit displays.
		return pow(max(maxBoost, 1e-6f), weight);
	}
}

#endif
