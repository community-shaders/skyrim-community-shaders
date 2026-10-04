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

	float GetHeightFogFalloff2()
	{
		return SharedData::exponentialHeightFogSettings.fogHeightFalloff2 * 0.001f;
	}

	float GetHeightFogDensity2()
	{
		return SharedData::exponentialHeightFogSettings.fogDensity2 * 0.001f;
	}

	float GetVolumetricStartDistance()
	{
		return max(0.0f, SharedData::exponentialHeightFogSettings.volumetricFogStartDistance);
	}

	float GetVolumetricEndDistance()
	{
		return max(GetVolumetricStartDistance() + 1.0f, SharedData::exponentialHeightFogSettings.volumetricFogDistance);
	}

	float GetVolumetricNearPlane()
	{
		return max(SharedData::CameraData.y, GetVolumetricStartDistance());
	}

	// Total depth reached by the far volume (view distance).
	float GetVolumetricTotalFarPlane()
	{
		const float nearPlane = GetVolumetricNearPlane();
		return max(nearPlane + 1.0f, GetVolumetricEndDistance());
	}

	// Depth where the near volume ends and the far volume begins.
	float GetVolumetricNearGridEndDistance()
	{
		const float nearPlane = GetVolumetricNearPlane();
		return min(
			max(max(SharedData::exponentialHeightFogSettings.volumetricNearGridDistance, 0.0f), nearPlane + 1.0f),
			GetVolumetricTotalFarPlane());
	}

	float GetVolumetricGridSizeZ()
	{
#if defined(EXP_HEIGHT_FOG_GRID_SIZE_Z)
		return clamp(float(EXP_HEIGHT_FOG_GRID_SIZE_Z), 16.0f, 160.0f);
#else
		return clamp(float(SharedData::exponentialHeightFogSettings.volumetricGridSizeZ), 16.0f, 160.0f);
#endif
	}

	float GetVolumetricFarGridSizeZ()
	{
		return clamp(float(SharedData::exponentialHeightFogSettings.volumetricFarGridSizeZ), 16.0f, 160.0f);
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
		const float nearPlane = GetVolumetricNearPlane();
		const float farPlane = max(GetVolumetricNearGridEndDistance(), nearPlane + 1.0f);
		const float nearWithOffset = nearPlane + 0.095f * 100.0f;
		const float farExp = exp2(min(gridSizeZ / GetVolumetricDepthDistributionScale(), 120.0f));
		const float gridZOffset = (farPlane - nearWithOffset * farExp) / (farPlane - nearWithOffset);
		const float gridZScale = (1.0f - gridZOffset) / nearWithOffset;
		return float3(gridZScale, gridZOffset, GetVolumetricDepthDistributionScale());
#endif
	}

	float3 GetVolumetricGridZParams()
	{
		return GetVolumetricGridZParams(GetVolumetricGridSizeZ());
	}

	// Far volume depth distribution, matching the constant-buffer values uploaded by the host.
	float3 GetVolumetricFarGridZParams(float gridSizeZ)
	{
		gridSizeZ = clamp(gridSizeZ, 16.0f, 160.0f);
		const float nearPlane = max(GetVolumetricNearGridEndDistance(), GetVolumetricNearPlane() + 1.0f);
		const float farPlane = max(nearPlane + 1.0f, GetVolumetricTotalFarPlane());
		const float nearWithOffset = nearPlane + 0.095f * 100.0f;
		const float depthDistributionScale = max(SharedData::exponentialHeightFogSettings.volumetricDepthDistributionScale, gridSizeZ / 120.0f);
		const float farExp = exp2(min(gridSizeZ / depthDistributionScale, 120.0f));
		const float gridZOffset = (farPlane - nearWithOffset * farExp) / (farPlane - nearWithOffset);
		const float gridZScale = (1.0f - gridZOffset) / nearWithOffset;
		return float3(gridZScale, gridZOffset, depthDistributionScale);
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

	// Far volume slice for a scene depth; mirrors ComputeVolumetricNormalizedSlice over the far volume.
	float ComputeVolumetricFarNormalizedSlice(float viewDepth, float gridSizeZ)
	{
		gridSizeZ = clamp(gridSizeZ, 16.0f, 160.0f);
		float3 gridZParams = GetVolumetricFarGridZParams(gridSizeZ);
		return log2(max(viewDepth * gridZParams.x + gridZParams.y, 1e-6f)) * gridZParams.z / gridSizeZ;
	}

	// Deterministic 3D hash for value noise in [0,1].
	float HashValueNoise3D(float3 p)
	{
		p = frac(p * 0.1031f);
		p += dot(p, p.zyx + 31.32f);
		return frac((p.x + p.y) * p.z);
	}

	// Smooth trilinear value noise in [0,1].
	float ValueNoise3D(float3 p)
	{
		const float3 cell = floor(p);
		const float3 f = frac(p);
		const float3 u = f * f * (3.0f - 2.0f * f);  // smoothstep

		const float n000 = HashValueNoise3D(cell);
		const float n100 = HashValueNoise3D(cell + float3(1, 0, 0));
		const float n010 = HashValueNoise3D(cell + float3(0, 1, 0));
		const float n110 = HashValueNoise3D(cell + float3(1, 1, 0));
		const float n001 = HashValueNoise3D(cell + float3(0, 0, 1));
		const float n101 = HashValueNoise3D(cell + float3(1, 0, 1));
		const float n011 = HashValueNoise3D(cell + float3(0, 1, 1));
		const float n111 = HashValueNoise3D(cell + float3(1, 1, 1));

		return lerp(
			lerp(lerp(n000, n100, u.x), lerp(n010, n110, u.x), u.y),
			lerp(lerp(n001, n101, u.x), lerp(n011, n111, u.x), u.y),
			u.z);
	}

	// Multiplicative density modulation from a world-space value noise field with a
	// soft-clip cutoff. Returns 1 when the feature is disabled.
	float EvaluateHeightFogNoiseModulation(float3 positionWS, float3 cameraWS)
	{
		const float noiseScale = SharedData::exponentialHeightFogSettings.volumetricFogNoiseScale;
		if (noiseScale <= 0.0f)
			return 1.0f;

		const float3 worldPosition = positionWS + cameraWS;
		const float3 noisePos =
			worldPosition * noiseScale -
			SharedData::exponentialHeightFogSettings.volumetricFogNoiseVelocity * SharedData::Timer;

		float noise = ValueNoise3D(noisePos);
		// detail octave
		noise = lerp(noise, ValueNoise3D(noisePos * 4.17f + 71.3f), 0.35f);

		const float threshold = saturate(SharedData::exponentialHeightFogSettings.volumetricFogNoiseThreshold);
		float t = saturate((noise - threshold) / max(1.0f - threshold, 1e-4f));
		return t * t * (3.0f - 2.0f * t);  // smoothstep for soft clumps
	}

	float EvaluateHeightFogExtinction(float3 positionWS, float3 cameraWS)
	{
		const float fogDensity = GetHeightFogDensity();
		const float fogHeightFalloff = GetHeightFogFalloff();
		const float fogDensity2 = GetHeightFogDensity2();
		const float fogHeightFalloff2 = GetHeightFogFalloff2();
		const float worldHeight = positionWS.z + cameraWS.z;
		const float exponent = fogHeightFalloff * max(worldHeight - SharedData::exponentialHeightFogSettings.fogHeight, 0.0f);
		const float exponent2 = fogHeightFalloff2 * max(worldHeight - SharedData::exponentialHeightFogSettings.fogHeight2, 0.0f);
		const float localDensity =
			(fogDensity * exp2(-exponent) + fogDensity2 * exp2(-exponent2)) *
			EvaluateHeightFogNoiseModulation(positionWS, cameraWS);
		return max(localDensity * SharedData::exponentialHeightFogSettings.volumetricFogExtinctionScale * 0.5f, 0.0f);
	}
}

#endif
