#ifndef CLOUD_RELIGHT_HLSLI
#define CLOUD_RELIGHT_HLSLI

#include "Common/Math.hlsli"
#include "Common/SharedData.hlsli"

#if defined(CLOUD_SHADOWS)
#	include "CloudShadows/CloudShadows.hlsli"
#endif

namespace CloudRelight
{
	float Remap(float x, float inMin, float inMax, float outMin, float outMax)
	{
		if (inMin == inMax)
			return outMin;

		return lerp(outMin, outMax, saturate((x - inMin) / (inMax - inMin)));
	}

	namespace Phase
	{
		float SmoothstepUnchecked(float x)
		{
			return (x * x) * (3.0 - x * 2.0);
		}

		float SmoothBump(float center, float radius, float x)
		{
			return 1.0 - SmoothstepUnchecked(min(abs(x - center), radius) / radius);
		}

		float PowerfulSCurve(float x, float p1, float p2)
		{
			return pow(1.0 - pow(1.0 - saturate(x), p2), p1);
		}

		float3 CloudFit(float cosTheta)
		{
			float x = acos(cosTheta);
			float x2 = max(0.0, x - 2.45) / (Math::PI - 2.15);
			float x3 = max(0.0, x - 2.95) / (Math::PI - 2.95);
			float y =
				exp(-max(x * 1.5, 0.0) * 30.0) +
				smoothstep(1.7, 0.0, x) * 0.45 * 0.8 +
				SmoothBump(0.4, 0.5, cosTheta) * 0.02 -
				smoothstep(1.0, 0.2, x) * 0.06 +
				SmoothBump(2.18, 0.20, x) * 0.06 +
				smoothstep(2.28, 2.45, x) * 0.18 -
				PowerfulSCurve(x2 * 4.0, 3.5, 8.0) * 0.04 +
				x2 * -0.085 +
				x3 * x3 * 0.1;

			float3 result = y;
			result = lerp(result, result + 0.008 * 2.0, smoothstep(0.94, 1.0, cosTheta) * sin(x * 10.0 * float3(8, 4, 2)));
			result = lerp(result, result - 0.008 * 2.0, SmoothBump(-0.7, 0.14, cosTheta) * sin(x * 20.0 * float3(8, 4, 2)));
			result = lerp(result, result - 0.008 * 5.0, smoothstep(-0.994, -1.0, cosTheta) * sin(x * 30.0 * float3(3, 4, 2)));

			result += 0.13 * 1.4;
			return result * 3.9 * 0.25 * Math::INV_PI;
		}

		float ThomasSchander(float cosTheta)
		{
			float bestParams[10];
			bestParams[0] = 9.805233e-06;
			bestParams[1] = -6.500000e+01;
			bestParams[2] = -5.500000e+01;
			bestParams[3] = 8.194068e-01;
			bestParams[4] = 1.388198e-01;
			bestParams[5] = -8.370334e+01;
			bestParams[6] = 7.810083e+00;
			bestParams[7] = 2.054747e-03;
			bestParams[8] = 2.600563e-02;
			bestParams[9] = -4.552125e-12;

			float p1 = cosTheta + bestParams[3];
			float4 expValues = exp(float4(bestParams[1] * cosTheta + bestParams[2], bestParams[5] * p1 * p1, bestParams[6] * cosTheta, bestParams[9] * cosTheta));
			float4 expValWeight = float4(bestParams[0], bestParams[4], bestParams[7], bestParams[8]);
			return dot(expValues, expValWeight) * 0.25;
		}
	}

#if defined(CLOUD_SHADOWS)
	float GetInnerShadow(float3 viewDir, float3 dirLightDir, SamplerState textureSampler)
	{
		static const float kRayStep = 1.0 / 32.0;
		float rayPos = kRayStep * 0.5;
		float4 rayShadow = 0.0;

		static const float3 kPoissonDisc[4] = {
			float3(0.460921f, 0.615192f, 0.887539f),
			float3(0.757347f, 0.911008f, 0.189581f),
			float3(0.548753f, 0.145482f, 0.0548723f),
			float3(0.90051f, 0.157048f, 0.623493f)
		};

		[unroll] for (int i = 0; i < 4; i++)
		{
			float3 raySample = normalize(lerp(viewDir, dirLightDir, rayPos));
			raySample += (kPoissonDisc[i] * 2.0 - 1.0) * 0.01;

			if (raySample.z < 0.0)
				rayShadow[i] += -raySample.z;
			else
				rayShadow[i] = max(rayShadow[i], CloudShadows::CloudSelfShadowTexture.SampleLevel(textureSampler, raySample, 0).x);

			rayPos += kRayStep;
		}

		return 1.0 - saturate(dot(rayShadow, 0.25));
	}

	float3 RelightCloud(float4 baseColor, float3 viewDir, SamplerState textureSampler)
	{
		if (baseColor.w <= 0.0)
			return baseColor.rgb;

		SharedData::CloudRelightSettings data = SharedData::cloudRelightSettings;

		float3 dirLightDir = normalize(SharedData::DirLightDirection.xyz);
		float3 dirLightColor = SharedData::DirLightColor.rgb;
		float cosTheta = dot(viewDir, dirLightDir);

		float phaseCloud =
			Remap(
				baseColor.w,
				data.silverLiningSpread > 0.0 ? data.silverLiningSpread : 0.0,
				data.silverLiningSpread < 0.0 ? 1.0 + data.silverLiningSpread : 1.0,
				lerp(0.25 * Math::INV_PI, Phase::ThomasSchander(cosTheta), data.silverLiningMix),
				0.25 * Math::INV_PI) *
			Math::TAU * data.cloudRelightMix;
		phaseCloud = min(phaseCloud, 2.0);

		float sunIntensity = saturate(dot(dirLightColor, float3(0.2126, 0.7152, 0.0722)));
		float vanillaMix = lerp(1.0, data.cloudOriginalMix, sunIntensity);
		float3 cloudColor = baseColor.rgb * vanillaMix;

		float3 relitColor = baseColor.a * baseColor.rgb * phaseCloud * GetInnerShadow(viewDir, dirLightDir, textureSampler) * dirLightColor;
		float relitLuma = max(dot(relitColor, float3(0.2126, 0.7152, 0.0722)), 1e-5);
		relitColor *= min(1.0, 1.5 / relitLuma);
		cloudColor += relitColor;

		return cloudColor;
	}
#endif
}

#endif
