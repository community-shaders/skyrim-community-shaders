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
		return max(GetVolumetricStartDistance() + 1.0f, SharedData::viewAtmosphereSettings.localRange);
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
			max(max(SharedData::viewAtmosphereSettings.nearRange, 0.0f), nearPlane + 1.0f),
			GetVolumetricTotalFarPlane());
	}

	float GetVolumetricGridSizeZ()
	{
#if defined(EXP_HEIGHT_FOG_GRID_SIZE_Z)
		return clamp(float(EXP_HEIGHT_FOG_GRID_SIZE_Z), 16.0f, 160.0f);
#else
		return clamp(float(SharedData::viewAtmosphereSettings.nearSlices), 16.0f, 160.0f);
#endif
	}

	float GetVolumetricFarGridSizeZ()
	{
		return clamp(float(SharedData::viewAtmosphereSettings.middleSlices), 16.0f, 160.0f);
	}

	float GetVolumetricDepthDistributionScale()
	{
		return max(SharedData::viewAtmosphereSettings.depthDistribution, GetVolumetricGridSizeZ() / 120.0f);
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
		const float depthDistributionScale = max(SharedData::viewAtmosphereSettings.depthDistribution, gridSizeZ / 120.0f);
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

	float HeightDensityIntegral(float startHeight, float endHeight, float heightFalloff)
	{
		const float lower = min(startHeight, endHeight);
		const float upper = max(startHeight, endHeight);
		const float belowFraction = upper <= 0.0 ? 1.0 : saturate(-lower / max(upper - lower, 1e-6));
		const float falloff = heightFalloff * (max(upper, 0.0) - max(lower, 0.0));
		const float integral = falloff > 0.01 ? (1.0 - exp2(-falloff)) / max(falloff, 1e-6) :
		                                        0.69314718056 - 0.24022650695 * falloff;
		return lerp(exp2(-heightFalloff * max(lower, 0.0)) * integral, 0.69314718056, belowFraction);
	}

	float HeightFogVerticalOpticalDepth(float height, float baseHeight, float falloff, float density)
	{
		if (density <= 0.0f)
			return 0.0f;
		const float slope = falloff * 0.001f * 0.69314718056f;
		return density * 0.001f * (max(baseHeight - height, 0.0f) + exp(-slope * max(height - baseHeight, 0.0f)) / slope);
	}

	float HeightFogScaledErfc(float x)
	{
		if (x >= 4.0f) {
			const float q = rcp(x * x);
			return 0.56418958355f / x * (1.0f + q * (-0.5f + q * (0.75f + q * (-1.875f + q * 6.5625f))));
		}
		const float t = rcp(1.0f + 0.3275911f * x);
		return t * (0.254829592f + t * (-0.284496736f + t * (1.421413741f + t * (-1.453152027f + t * 1.061405429f))));
	}

	float HeightFogCurvedOpticalDepth(float height, float baseHeight, float falloff, float density, float cosine, float radius)
	{
		if (density <= 0.0f)
			return 0.0f;
		if (falloff <= 0.0f)
			return 1e20f;
		const float slope = falloff * 0.001f * 0.69314718056f;
		const float curvature = max(1.0f - cosine * cosine, 0.0f) / (2.0f * radius);
		if (curvature <= 1e-12f * slope)
			return cosine > 0.0f ? HeightFogVerticalOpticalDepth(height, baseHeight, falloff, density) / cosine : 1e20f;

		// The tangent expansion makes the exponential height integral Gaussian.
		const float root = sqrt(slope * curvature);
		const float x = cosine * slope / (2.0f * root);
		const float factor = 0.88622692545f / root;
		const float startTail = exp(-slope * max(height - baseHeight, 0.0f)) * HeightFogScaledErfc(abs(x)) * factor;
		float column;
		if (cosine >= 0.0f && height >= baseHeight) {
			column = startTail;
		} else {
			const float minimumHeight = height - cosine * cosine / (4.0f * curvature);
			if (minimumHeight >= baseHeight) {
				column = 2.0f * factor * exp(-slope * (minimumHeight - baseHeight)) - startTail;
			} else {
				const float edge = sqrt(slope * (baseHeight - minimumHeight));
				const float tail = factor * HeightFogScaledErfc(edge);
				if (height <= baseHeight) {
					const float exitCosine = sqrt(cosine * cosine + 4.0f * curvature * (baseHeight - height));
					const float flatDistance = cosine >= 0.0f ?
					                               2.0f * (baseHeight - height) / max(exitCosine + cosine, 1e-20f) :
					                               (exitCosine - cosine) / (2.0f * curvature);
					column = flatDistance + tail;
				} else {
					column = 2.0f * (tail + edge / root) - startTail;
				}
			}
		}
		return density * 0.001f * max(column, 0.0f);
	}

	float EvaluateHeightFogLightTransmittance(float3 positionWS, float3 cameraWS, float3 lightDirection)
	{
		const SharedData::ExponentialHeightFogSettings fog = SharedData::exponentialHeightFogSettings;
		const float scale = fog.volumetricFogEnabled ? fog.volumetricFogExtinctionScale * 0.5f : 0.48045301392f;
		if (scale <= 0.0f || fog.sunlightAttenuationAmount <= 0.0f || max(fog.fogDensity, fog.fogDensity2) <= 0.0f)
			return 1.0f;
		float height = positionWS.z + cameraWS.z;
		float cosine = lightDirection.z;
		float radius = 6371000.0f / 0.01428f;
#if defined(PHYSICAL_SKY)
		const bool physicalSky = SharedData::physSkyData.enabled != 0u;
		const float groundHeight = physicalSky ? SharedData::physSkyData.zBottom : 0.0f;
		const float planetRadius = physicalSky ? SharedData::physSkyData.rPlanet : radius;
#else
		const float groundHeight = 0.0f;
		const float planetRadius = radius;
#endif
		const float tangentHeight = height - groundHeight;
		const float verticalRadius = planetRadius + tangentHeight;
		const float3 planetPosition = float3(positionWS.xy, verticalRadius);
		radius = length(planetPosition);
		const float radialHeight = tangentHeight + dot(positionWS.xy, positionWS.xy) / max(radius + verticalRadius, 1.0f);
		height = groundHeight + radialHeight;
		cosine = clamp(dot(planetPosition, lightDirection) / max(radius, 1.0f), -1.0f, 1.0f);
		const float horizonSquared = max(radialHeight * (2.0f * planetRadius + radialHeight), 0.0f);
		if (cosine < 0.0f && radius * radius * cosine * cosine >= horizonSquared)
			return 1.0f - saturate(fog.sunlightAttenuationAmount);
		const float opticalDepth = HeightFogCurvedOpticalDepth(height, fog.fogHeight, fog.fogHeightFalloff, fog.fogDensity, cosine, radius) +
		                           HeightFogCurvedOpticalDepth(height, fog.fogHeight2, fog.fogHeightFalloff2, fog.fogDensity2, cosine, radius);
		const float transmittance = exp(-min(scale * opticalDepth, 80.0f));
		return lerp(1.0f, transmittance, saturate(fog.sunlightAttenuationAmount));
	}
}

#endif
