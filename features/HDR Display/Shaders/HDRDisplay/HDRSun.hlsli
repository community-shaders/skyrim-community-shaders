#ifdef HDR_OUTPUT

#	include "Common/Random.hlsli"

namespace HDRSun
{
	// HDR on and this pass is sun / glare, not moon or clouds.
	inline bool IsHdrSunActive()
	{
		return SharedData::HDRData.x > 0.5f && (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::IsSun);
	}

	// Fixed 203 for sun math only (paper white slider doesn't touch this).
	static const float kReferencePaperWhiteNits = 203.0f;
	// Pause / map / main menu: pull sun back so it isn't fighting the UI.
	static const float kMenuSunNits = 100.0f;

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

		float peakNits = max(SharedData::HDRData.z, kReferencePaperWhiteNits + 1.0f);
		float peakRatio = peakNits / kReferencePaperWhiteNits;

		// HDRData.w encodes menu-ish scenes; scale sun vs real peak so it lands near kMenuSunNits.
		float menuSunMul = (SharedData::HDRData.w > 1e-3f) ? (kMenuSunNits / peakNits) : 1.0f;
		// Squared vs 203: enough headroom at low peak nits; old vanilla gamma leg was starving it.
		float maxBoost = pow(peakRatio, 2.0f) * menuSunMul;

		float L = max(Color::RGBToLuminance(baseColor.xyz), 0.0f);
		// Soft shoulder + don't strand pixels just under 1.0.
		float highlight = max(1.0f - exp(-L), saturate(L));

		float a = saturate(baseColor.w);
		// Low alpha shouldn't get the full HDR shove (wide glow mods were a mess without this).
		float alphaWeight = a * a;

		// Billboard is a square; UV falloff keeps the corners from blowing out.
		float radialWeight = 1.0f;
#if defined(TEX)
		float2 uv = saturate(texCoord0_xy);
		float r = saturate(length(uv - 0.5f) * 1.41421356f);
		radialWeight = pow(saturate(1.0f - r), 1.85f);
#endif

		// 0 = no boost, 1 = full maxBoost. Everything else is fringe.
		float weight = saturate(highlight * alphaWeight * radialWeight);

		// Second sample, blurred mip: if fine ≈ coarse over a big area it's probably a fat soft sun, ease off.
		float4 coarseS = sunTex.SampleLevel(samp, texCoord0_xy, 2.0);
		float3 coarseRgb = Color::Sky(coarseS.rgb);
		float Lc = max(Color::RGBToLuminance(coarseRgb), 1e-4);
		// TEXFADE: Sky passes PParams.x so coarse alpha matches fine after the fade.
		float lac = Lc * saturate(coarseS.w * alphaPostScale);
		float laf = L * a;
		float mipQuell = 1.0f;
		// L < Lc: fine darker than the mip average — usually a slope, leave mipQuell at 1.
		if (L >= Lc) {
			float relDelta = (L - Lc) / max(Lc, 0.015);
			float relDeltaA = (laf - lac) / max(lac, 1e-4);
			mipQuell = saturate(max(relDelta * 3.0f, relDeltaA * 2.0f));
		}
		// Hot core: mip compare lies a bit on a tiny bright dot, let it through anyway.
		float hotBypass = saturate((L - 0.96f) * 100.0f);
		weight *= saturate(max(mipQuell, hotBypass));

		// pow beats linear 1→maxBoost here: stops the whole disc fattening when you crank peak nits.
		float boost = pow(max(maxBoost, 1e-6f), weight);

		baseColor.xyz *= boost;

#if defined(DITHER)
		// Glare path — vertex tint after the boost.
		baseColor.xyz = Color::Sky(vertexColor.xyz) * baseColor.xyz;
#else
		// Disc path: tiny IGN, scales with how much we boosted.
		baseColor.xyz += (Random::InterleavedGradientNoise(position.xy) - 0.5f) * (saturate(boost - 1.0f) / 255.0f);
		yyy = 0.0f;
#endif

#if defined(CLOUD_SHADOWS)
		// Same factor on rgb and a so the edge doesn't look wrong.
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
