#ifdef HDR_OUTPUT

#	include "Common/Random.hlsli"

namespace HDRSun
{
	// 203 nits ref for sun math only (not the paper-white UI slider).
	static const float kReferencePaperWhiteNits = 203.0f;
	// Menu / pause / map: HDRData.w > 0 — scale sun toward this nit level vs peak.
	static const float kMenuSunNits = 100.0f;

	inline bool IsHdrSunActive()
	{
		return SharedData::HDRData.x > 0.5f && (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::IsSun);
	}

	// Sky PS already sampled sunTex and ran Color::Sky on rgb. We only adjust when HDR+IsSun.
	// Pipeline: peak → maxBoost → per-pixel weight → rgb *= pow(maxBoost, weight)
	//           → DITHER: vertex tint | else: IGN dither + clear yyy → optional cloud shadow on rgba.
	void ApplyHdrSunToBaseColor(
		float4 position,
		float2 texCoord0_xy,
		float4 vertexColor,
		float3 worldPosition,
		Texture2D<float4> sunTex,
		SamplerState samp,
		float alphaPostScale,
		inout float4 baseColor,
		inout float3 yyy)
	{
		if (!IsHdrSunActive())
			return;

		// --- max linear multiplier for this display (menu scales it down when HDRData.w > 0) ---
		float peakNits = max(SharedData::HDRData.z, kReferencePaperWhiteNits + 1.0f);
		float peakRatio = peakNits / kReferencePaperWhiteNits;
		float menuSunMul = (SharedData::HDRData.w > 1e-3f) ? (kMenuSunNits / peakNits) : 1.0f;
		float maxBoost = pow(peakRatio, 2.0f) * menuSunMul;

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

		// --- apply boost (pow keeps mid-ring from exploding when peak is high) ---
		float boost = pow(max(maxBoost, 1e-6f), weight);
		baseColor.xyz *= boost;

		// --- finish: Sky PS output path differs for glare vs disc ---
#if defined(DITHER)
		baseColor.xyz = Color::Sky(vertexColor.xyz) * baseColor.xyz;
#else
		baseColor.xyz += (Random::InterleavedGradientNoise(position.xy) - 0.5f) * (saturate(boost - 1.0f) / 255.0f);
		yyy = 0.0f;
#endif

#if defined(CLOUD_SHADOWS)
		float3 cloudSampleDir = CloudShadows::GetCloudShadowSampleDir(worldPosition.xyz, SharedData::DirLightDirection.xyz);
		float cloudCube0 = CloudShadows::CloudShadowsTexture.SampleLevel(samp, cloudSampleDir, 0).x;
		float cloudCube1 = CloudShadows::CloudShadowsTexture.SampleLevel(samp, cloudSampleDir, 1).x;
		float cloudCube = lerp(cloudCube0, cloudCube1, 0.5f);
		float cloudMult = lerp(1.0f, 1.0f - cloudCube, SharedData::cloudShadowsSettings.Opacity);
		baseColor.xyz *= cloudMult;
		baseColor.w *= cloudMult;
#endif
	}
}

#endif
