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
		return SharedData::exponentialHeightFogSettings.fogHeightFalloff * 0.001f;
	}

	float GetHeightFogDensity()
	{
		return SharedData::exponentialHeightFogSettings.fogDensity * 0.001f;
	}

	float GetVolumetricStartDistance()
	{
		return max(0.0f, SharedData::exponentialHeightFogSettings.volumetricFogStartDistance);
	}

	float GetVolumetricEndDistance()
	{
		return max(GetVolumetricStartDistance() + 1.0f, SharedData::exponentialHeightFogSettings.volumetricFogDistance);
	}

	float GetVolumetricGridSizeZ()
	{
#if defined(EXP_HEIGHT_FOG_GRID_SIZE_Z)
		return clamp(float(EXP_HEIGHT_FOG_GRID_SIZE_Z), 16.0f, 160.0f);
#else
		return clamp(float(SharedData::exponentialHeightFogSettings.volumetricGridSizeZ), 16.0f, 160.0f);
#endif
	}

	float GetVolumetricDepthDistributionScale()
	{
		return max(SharedData::exponentialHeightFogSettings.volumetricDepthDistributionScale, GetVolumetricGridSizeZ() / 120.0f);
	}

	float3 GetVolumetricGridZParams(float gridSizeZ)
	{
#if defined(EXP_HEIGHT_FOG_GRID_Z_PARAMS)
		return EXP_HEIGHT_FOG_GRID_Z_PARAMS;
#else
		gridSizeZ = clamp(gridSizeZ, 16.0f, 160.0f);
		float nearPlane = max(SharedData::CameraData.y, GetVolumetricStartDistance());
		float farPlane = max(nearPlane + 1.0f, GetVolumetricEndDistance());
		float nearWithOffset = nearPlane + 0.095f * 100.0f;
		float farExp = exp2(min(gridSizeZ / GetVolumetricDepthDistributionScale(), 120.0f));
		float gridZOffset = (farPlane - nearWithOffset * farExp) / (farPlane - nearWithOffset);
		float gridZScale = (1.0f - gridZOffset) / nearWithOffset;
		return float3(gridZScale, gridZOffset, GetVolumetricDepthDistributionScale());
#endif
	}

	float3 GetVolumetricGridZParams()
	{
		return GetVolumetricGridZParams(GetVolumetricGridSizeZ());
	}

	float ComputeVolumetricSliceDepth(float slice)
	{
		float3 gridZParams = GetVolumetricGridZParams();
		float sliceExp = exp2(min(slice / max(gridZParams.z, 1e-4f), 120.0f));
		return (sliceExp - gridZParams.y) / max(gridZParams.x, 1e-20f);
	}

	float ComputeVolumetricNormalizedSlice(float viewDepth, float gridSizeZ)
	{
		gridSizeZ = clamp(gridSizeZ, 16.0f, 160.0f);
		float3 gridZParams = GetVolumetricGridZParams(gridSizeZ);
		return log2(max(viewDepth * gridZParams.x + gridZParams.y, 1e-6f)) * gridZParams.z / gridSizeZ;
	}

	float ComputeVolumetricNormalizedSlice(float viewDepth)
	{
		return ComputeVolumetricNormalizedSlice(viewDepth, GetVolumetricGridSizeZ());
	}

	float EvaluateHeightFogExtinction(float3 positionWS, float3 cameraWS)
	{
		float fogDensity = GetHeightFogDensity();
		float fogHeightFalloff = GetHeightFogFalloff();
		float worldHeight = positionWS.z + cameraWS.z;
		float exponent = fogHeightFalloff * max(worldHeight - SharedData::exponentialHeightFogSettings.fogHeight, 0.0f);
		float localDensity = fogDensity * exp2(-exponent);
		return max(localDensity * SharedData::exponentialHeightFogSettings.volumetricFogExtinctionScale * 0.5f, 0.0f);
	}
}

#endif
