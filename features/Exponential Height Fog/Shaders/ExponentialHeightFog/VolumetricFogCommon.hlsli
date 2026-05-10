#ifndef __EXPONENTIAL_HEIGHT_FOG_VOLUMETRIC_COMMON_HLSLI__
#define __EXPONENTIAL_HEIGHT_FOG_VOLUMETRIC_COMMON_HLSLI__

#include "Common/Math.hlsli"
#include "Common/SharedData.hlsli"

namespace ExponentialHeightFog
{
	float HenyeyGreenstein(float cosTheta, float g)
	{
		float g2 = g * g;
		float denom = 1.0f + g2 - 2.0f * g * cosTheta;
		return (1.0f - g2) / (4.0f * Math::PI * pow(max(denom, 1e-5f), 1.5f));
	}

	float GetHeightFogFalloff()
	{
		return exponentialHeightFogSettings.fogHeightFalloff * 0.001f;
	}

	float GetHeightFogDensity()
	{
		return exponentialHeightFogSettings.fogDensity * 0.001f;
	}

	float GetVolumetricStartDistance()
	{
		return max(0.0f, exponentialHeightFogSettings.volumetricFogStartDistance);
	}

	float GetVolumetricEndDistance()
	{
		return max(GetVolumetricStartDistance() + 1.0f, exponentialHeightFogSettings.volumetricFogDistance);
	}

	float GetVolumetricDepthDistributionScale()
	{
		return max(exponentialHeightFogSettings.volumetricDepthDistributionScale, 0.01f);
	}

	float ComputeVolumetricSliceDepth(float normalizedSlice)
	{
		float sliceT = pow(saturate(normalizedSlice), GetVolumetricDepthDistributionScale());
		return lerp(GetVolumetricStartDistance(), GetVolumetricEndDistance(), sliceT);
	}

	float ComputeVolumetricNormalizedSlice(float viewDistance)
	{
		float normalizedDistance = saturate((viewDistance - GetVolumetricStartDistance()) / max(GetVolumetricEndDistance() - GetVolumetricStartDistance(), 1.0f));
		return pow(normalizedDistance, rcp(GetVolumetricDepthDistributionScale()));
	}

	float EvaluateHeightFogExtinction(float3 positionWS, float3 cameraWS)
	{
		float fogDensity = GetHeightFogDensity();
		float fogHeightFalloff = GetHeightFogFalloff();
		float worldHeight = positionWS.z + cameraWS.z;
		float exponent = fogHeightFalloff * max(worldHeight - exponentialHeightFogSettings.fogHeight, 0.0f);
		float localDensity = fogDensity * exp2(-exponent);
		return max(localDensity * exponentialHeightFogSettings.volumetricFogExtinctionScale * 0.5f, 0.0f);
	}
}

#endif
