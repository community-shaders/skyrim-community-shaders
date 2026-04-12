#ifdef HDR_OUTPUT

#	include "Common/Random.hlsli"

namespace HDRSun
{
	inline bool IsHdrSunActive()
	{
		return SharedData::HDRData.x > 0.5f && (Permutation::ExtraShaderDescriptor & Permutation::ExtraFlags::IsSun);
	}

	// Sun boost uses 203 nits as reference white, not the user's paper-white setting.
	static const float kReferencePaperWhiteNits = 203.0f;
	// Menu / pause / map: cap sun drive toward this luminance on the display (nits).
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

		float menuSunMul = (SharedData::HDRData.w > 1e-3f) ? (kMenuSunNits / peakNits) : 1.0f;
		float maxBoost = pow(peakRatio, 2.0f) * menuSunMul;

		float L = max(Color::RGBToLuminance(baseColor.xyz), 0.0f);
		float highlight = max(1.0f - exp(-L), saturate(L));

		float a = saturate(baseColor.w);
		float alphaWeight = a * a;

		// Sun on a quad: fade boost from center so corners do not light up.
		float radialWeight = 1.0f;
#	if defined(TEX)
		float2 uv = saturate(texCoord0_xy);
		float r = saturate(length(uv - 0.5f) * 1.41421356f);
		radialWeight = pow(saturate(1.0f - r), 1.85f);
#	endif

		float weight = saturate(highlight * alphaWeight * radialWeight);

		// Wide sun: fine luminance matches a blurred mip (plateau). Tight core: fine > coarse. LOD 2, no mod list.
		// If L < Lc (fine dimmer than coarse), skip quell — usually a slope, not a broad flat sheet.
		float4 coarseS = sunTex.SampleLevel(samp, texCoord0_xy, 2.0);
		float3 coarseRgb = Color::Sky(coarseS.rgb);
		float Lc = max(Color::RGBToLuminance(coarseRgb), 1e-4);
		float lac = Lc * saturate(coarseS.w * alphaPostScale);
		float laf = L * a;
		float mipQuell = 1.0f;
		if (L >= Lc) {
			float relDelta = (L - Lc) / max(Lc, 0.015);
			float relDeltaA = (laf - lac) / max(lac, 1e-4);
			mipQuell = saturate(max(relDelta * 3.0f, relDeltaA * 2.0f));
		}
		float hotBypass = saturate((L - 0.96f) * 100.0f);
		weight *= saturate(max(mipQuell, hotBypass));

		// pow(maxBoost, weight): same endpoints as lerp(1,maxBoost), less mid-ring growth when peak is high.
		float boost = pow(max(maxBoost, 1e-6f), weight);

		baseColor.xyz *= boost;

#	if defined(DITHER)
		baseColor.xyz = Color::Sky(vertexColor.xyz) * baseColor.xyz;
#	else
		// Dither bright output to reduce banding.
		baseColor.xyz += (Random::InterleavedGradientNoise(position.xy) - 0.5f) * (saturate(boost - 1.0f) / 255.0f);
		yyy = 0.0f;
#	endif

#	if defined(CLOUD_SHADOWS)
		float3 cloudSampleDir = CloudShadows::GetCloudShadowSampleDir(worldPosition.xyz, SharedData::DirLightDirection.xyz);
		float cloudCube0 = CloudShadows::CloudShadowsTexture.SampleLevel(samp, cloudSampleDir, 0).x;
		float cloudCube1 = CloudShadows::CloudShadowsTexture.SampleLevel(samp, cloudSampleDir, 1).x;
		float cloudCube = lerp(cloudCube0, cloudCube1, 0.5f);
		float cloudMult = lerp(1.0f, 1.0f - cloudCube, SharedData::cloudShadowsSettings.Opacity);
		baseColor.xyz *= cloudMult;
		baseColor.w *= cloudMult;
#	endif
	}
}

#endif
