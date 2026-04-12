#ifdef HDR_OUTPUT

#	include "Common/Random.hlsli"

namespace HDRSun
{
	static const float kReferencePaperWhiteNits = 203.0f;
	static const float kMenuSunNits = 100.0f;

	inline bool IsHdrSunActive()
	{
		return SharedData::HDRData.x > 0.5f && (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::IsSun);
	}

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

		// TEST: skip HDR boost on sun glare (DITHER path). Remove this guard when done.
#if !defined(DITHER)
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
		float lac = Lc * saturate(coarseS.w * alphaPostScale);
		float laf = L * a;

		float mipQuell = 1.0f;
		if (L >= Lc) {
			float relL = (L - Lc) / max(Lc, 1e-4f);
			float relA = (laf - lac) / max(lac, 1e-4f);
			float spike = relL + relA;
			mipQuell = saturate(1.0f - exp(-spike));
		}
		weight *= max(mipQuell, saturate(L));

		float boost = pow(max(maxBoost, 1e-6f), weight);

		baseColor.xyz *= boost;
#endif

#if defined(DITHER)
		// Glare path — vertex tint after the boost.
		baseColor.xyz = Color::Sky(vertexColor.xyz) * baseColor.xyz;
#else
		// Disc path: tiny IGN, scales with how much we boosted.
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
