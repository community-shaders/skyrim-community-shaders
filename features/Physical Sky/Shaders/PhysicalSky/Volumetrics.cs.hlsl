// Community Shaders
// Authors: Profjack, Jiaye
//
// References: Nubis Evolved (2022), Nubis Cubed (2023).

#define PHYSICAL_SKY_VOLUMETRICS
#ifndef COMPUTESHADER
#	define COMPUTESHADER
#endif
#define PS_SKY_SAMPLERS
#define PS_PREPASS_RSRCS
#define PS_NO_RSRCS
#define OMIT_PS_NAMESPACE
#ifdef CLOUD_AP_SHADOW
#	define PS_ENABLE_DIRLIGHT_TRANSMITTANCE
#	define PS_LINEAR_SHADOW_SAMPLER
#	define AP_DIRECT_SHADOW_REGISTER t7
#	define TERRAIN_SHADOW_REGISTER t36
#	define CLOUD_SHADOW_REGISTER t37
#	define CLOUD_SELFSHADOW_REGISTER t38
#endif
Texture3D<float> TexShadowVolume : register(t14);
#include "Common/Spherical Harmonics/SphericalHarmonics.hlsli"
#include "PhysicalSky/CloudGeometry.hlsli"
#include "PhysicalSky/CloudMotion.hlsli"
#include "PhysicalSky/CloudNoise.hlsli"
#include "PhysicalSky/Common.hlsli"
#include "PhysicalSky/Lightning.hlsli"
#ifdef CLOUD_AP_SHADOW
#	include "PhysicalSky/ApShadow.hlsli"
#endif

static const float GAME_UNITS_PER_METER = 1.0 / GAME_UNIT_TO_M;

float EncodeCloudDepth(float gameUnitDepth)
{
	return gameUnitDepth * GAME_UNIT_TO_M * 0.001;
}

float DecodeCloudDepth(float encodedDepth)
{
	return encodedDepth * 1000.0 * GAME_UNITS_PER_METER;
}

SamplerState TileableSampler : register(s2);
#define TransmittanceSampler SampTr
#define SkyViewSampler SampSv

struct CloudLayer
{
	float lowestAltitude;
	float highestAltitude;
};

#include "PhysicalSky/StormData.hlsli"

struct VolumetricCloudData
{
	float rayMarchRange;
	float shadowVolumeRange;
	float marchStepScale;
	uint cloudFrameIndex;

	float2 rcpFrameDim;
	float2 activeFrameDim;
	float2 lowFrameDim;
	float2 previousFrameDim;

	float3 dirlightDir;
	float bottomZ;
	float3 previousCamera;
	float planetRadius;

	float lowestCloudAltitude;
	float highestCloudAltitude;
	float lowCloudBaseAltitude;
	float lowCloudTopAltitude;

	float lowCloudTraceBottomAltitude;
	float lowCloudTraceTopAltitude;
	float cloudCurvatureRadius;
	float cloudEvolutionDelta;

	float shadowVolumeBottom;
	float shadowVolumeTop;
	uint historyValid;
	uint cirrusEnabled;

	float2 lowNdfFrequency;
	float2 noiseWindOffset;
	float2 cloudWindDelta;
	float2 cloudShapeShear;
	float2 previousShapeShear;
	float2 cirrusWindOffset;

	float2 cirrusWindDelta;
	float cirrusAltitude;
	float cirrusUvInvSize;

	float cirrusDensityScale;
	float cirrusLightThickness;
	float phaseForwardG;
	float phaseBackwardG;

	float lowDensityScale;
	float coverageBottomPower;
	float coverageHeightRange;
	float bottomDensityPower;

	float bottomDensityWidth;
	float topExpansion;
	float scatterDensityThreshold;
	float scatterDensityPower;

	float cloudLightingScale;
	float sunExtinction;
	float viewExtinction;
	float ambientStrength;

	float msAlbedo;
	float msStrength;
	float msBoundaryWidth;
	float msTransportExtinction;

	float4 cloudEvolution;
	row_major float4x4 previousViewProj;

	float4 ndfBoundaryRect;
	float4 localNdfRect;
	float4 localNdfAltitude;
	float4 localNdfAuxiliary;
	float4 lowCloudPhase;
	uint stormCount;
	uint voxelCount;
	float2 voxelAltitude;
	uint skipShapeNoise;
	uint useVoxelNoise;
	float2 noisePadding;  // previous curvature radius, weather-history refresh weight
	float4 middleLayer;   // bottom/top (game units), extinction/metre, weather UV/game unit
	float4 middleShape;   // edge softness, reserved, detail UV/game unit, 3D detail
};

cbuffer VolumetricCloudConstants : register(b1)
{
	VolumetricCloudData VolumetricCloudBuffer[1];
	float4 CloudMsCalibration[49];
	float4 MiddleCloudCells;      // frequency, Ac modulation, As modulation, warp
	float4 MiddleCloudBands;      // world wave vector (cycles/game unit), bend (wavelengths), organization
	float4 MiddleCloudBandShape;  // thickness contrast, displacement (game units), wavelength (game units), enabled
};
Texture2D<float4> TexTransmittance : register(t1);
Texture3D<float4> TexAerialPerspective : register(t2);

Texture2D<float> TexDepth : register(t3);

Texture3D<unorm float4> TexCloudShapeNoise : register(t4);
Texture3D<float4> NoiseVoxel : register(t76);
Texture2D<float4> TexCloudHeight : register(t5);
Texture2D<float3> TexCloudModeling : register(t6);
Texture2D<float4> TexCloudAtmosphereMs : register(t8);
Texture2D<float2> TexCirrusWeather : register(t9);
Texture2D<float3> TexCirrusPatterns : register(t10);
Texture2D<unorm float2> TexCloudProfileLUT : register(t11);
Texture2D<unorm float3> TexCloudAdjustmentLUT : register(t12);
Texture2D<sh2> TexCloudAmbientSH : register(t13);
TextureCube<float4> TexCloudGroundReflection : register(t39);

Texture2D<float4> TexCloudBoundary : register(t15);
Texture2D<float2> TexCloudHeightBounds : register(t16);
Texture2D<float4> TexCloudHeightBoundsTiles : register(t54);
Texture2D<float2> TexCloudWeatherBounds : register(t89);
Texture2D<float2> TexLightningTrace : register(t90);
Texture2D<float2> TexLightningHistory : register(t91);
TextureCube<float2> TexLightningCubeTrace : register(t92);
TextureCube<float2> TexLightningCubeHistory : register(t93);
RWTexture2D<float2> RWLightningMask : register(u3);
RWTexture2DArray<float2> RWLightningCubeMask : register(u3);

CloudLayer GetCloudLayer(VolumetricCloudData info)
{
	CloudLayer cloud;
	cloud.lowestAltitude = info.lowCloudTraceBottomAltitude;
#ifdef NO_CLOUD_STORMS
	cloud.highestAltitude = info.lowCloudTraceTopAltitude;
#else
	cloud.highestAltitude = info.shadowVolumeTop;
#endif
#ifdef CLOUD_MAIN_DISPATCH
	// Share the weather support with primary and light probes, including probes
	// inside a storm. Local modeling is not part of the weather-map reduction.
	if (info.localNdfAltitude.z <= 0.0) {
		const float2 altitude = lerp(info.lowCloudBaseAltitude, info.lowCloudTopAltitude, TexCloudWeatherBounds[uint2(0, 0)]);
		cloud.lowestAltitude = max(cloud.lowestAltitude, altitude.x - GAME_UNITS_PER_METER);
		cloud.highestAltitude = min(cloud.highestAltitude, altitude.y + GAME_UNITS_PER_METER);
	}
#endif
	return cloud;
}

Texture2D<float> TexVolHistoryTr : register(t17);
Texture2D<float4> TexVolHistoryLum : register(t18);
Texture2D<float4> TexVolHistoryAux : register(t19);
Texture2D<float> TexVolLowTr : register(t20);
Texture2D<float4> TexVolLowLum : register(t21);
Texture2D<float4> TexVolLowAux : register(t22);
Texture2D<float4> TexVolLowStormMotion : register(t58);
TextureCube<float4> TexCubeStormMotion : register(t59);
RWTexture2D<float4> RWStormMotion : register(u6);
RWTexture2DArray<float4> RWCubeStormMotion : register(u7);
TextureCube<float> TexCubeHistoryTr : register(t23);
TextureCube<float4> TexCubeHistoryLum : register(t24);
TextureCube<float4> TexCubeHistoryAux : register(t25);
TextureCube<float> TexCubeTraceTr : register(t26);
TextureCube<float4> TexCubeTraceLum : register(t27);
TextureCube<float4> TexCubeTraceAux : register(t28);
Texture2DArray<float4> TexLocalNdfHeight : register(t32);
Texture2DArray<float4> TexLocalNdfModeling : register(t33);
Texture2DArray<float4> TexLocalNdfMaximum : register(t34);
StructuredBuffer<float4> LocalNdfWeights : register(t35);
uint CloudStormCount()
{
#ifdef NO_CLOUD_STORMS
	return 0u;
#elif defined(SINGLE_CLOUD_STORM)
	return 1u;
#else
	return VolumetricCloudBuffer[0].stormCount;
#endif
}

Texture2DArray<float4> TexStormHeight : register(t55);
Texture2DArray<float4> TexStormModeling : register(t56);
#ifdef SINGLE_CLOUD_STORM
cbuffer StormInstanceConstants : register(b0) { CloudStormData CloudStorms[1]; };
#else
StructuredBuffer<CloudStormData> CloudStorms : register(t57);
#endif

float2 LocalNdfUV(float2 worldXY, VolumetricCloudData info)
{
	return (worldXY * GAME_UNIT_TO_M - info.localNdfRect.xy) * info.localNdfRect.zw;
}

bool InLocalNdf(float2 uv, VolumetricCloudData info)
{
#ifdef NO_LOCAL_NDF
	return false;
#else
	return info.localNdfAltitude.z > 0.0 && all(uv >= 0.0) && all(uv <= 1.0);
#endif
}

struct LocalNdfModelSample
{
	float2 uv;
	uint count;
};

LocalNdfModelSample SampleLocalNdfModel(float2 worldXY, VolumetricCloudData info)
{
	LocalNdfModelSample value;
	value.uv = LocalNdfUV(worldXY, info);
	value.count = InLocalNdf(value.uv, info) ? (uint)info.localNdfAltitude.w : 0u;
	return value;
}

float3 ApplyLocalNdfModel(float3 model, LocalNdfModelSample local)
{
	float3 result = model;
	[loop] for (uint i = 0u; i < local.count; ++i)
	{
		const float4 endpoint = LocalNdfWeights[i];
		float3 shaped;
		[branch] if (endpoint.y > 0.0)
			shaped = max(model, TexLocalNdfMaximum.SampleLevel(TransmittanceSampler, float3(local.uv, i), 0).rgb);
		else
		{
			const float4 layer = TexLocalNdfModeling.SampleLevel(TransmittanceSampler, float3(local.uv, i), 0);
			shaped = model * (1.0 - layer.a) + layer.rgb;
		}
		result += endpoint.x * (shaped - model);
	}
	return saturate(result);
}

bool HasLocalNdfInfluence(LocalNdfModelSample local)
{
	[loop] for (uint i = 0u; i < local.count; ++i)
	{
		[branch] if (LocalNdfWeights[i].y > 0.0)
		{
			if (any(TexLocalNdfMaximum.SampleLevel(TransmittanceSampler, float3(local.uv, i), 0).rgb > 0.0))
				return true;
		}
		else if (TexLocalNdfModeling.SampleLevel(TransmittanceSampler, float3(local.uv, i), 0).a > 0.0) return true;
	}
	return false;
}

float3 LocalNdfHeight(float3 height, float2 worldXY, VolumetricCloudData info)
{
	const float2 uv = LocalNdfUV(worldXY, info);
	[branch] if (info.localNdfAltitude.y > 0.0 && InLocalNdf(uv, info))
	{
		float4 local = 0.0;
		[loop] for (uint i = 0u; i < (uint)info.localNdfAltitude.w; ++i)
			local += LocalNdfWeights[i].x * TexLocalNdfHeight.SampleLevel(TransmittanceSampler, float3(uv, i), 0);
		height.xy = height.xy * (1.0 - local.b) + (local.rg * info.localNdfAltitude.y + info.localNdfAltitude.x * local.b) * GAME_UNITS_PER_METER;
	}
	return height;
}

float LocalNdfMotionWeight(float2 worldXY, float height, VolumetricCloudData info)
{
	const float2 shear = info.cloudShapeShear * smoothstep(0.0, 1.0, height);
	const LocalNdfModelSample local = SampleLocalNdfModel(worldXY - shear, info);
	if (local.count == 0u)
		return 0.0;
	float coverage = -1.0;
	float weight = 0.0;
	[loop] for (uint i = 0u; i < local.count; ++i)
	{
		const float4 endpoint = LocalNdfWeights[i];
		[branch] if (endpoint.y > 0.0)
		{
			if (coverage < 0.0) {
				const float2 field = CloudFieldPosition((worldXY - info.noiseWindOffset) * GAME_UNIT_TO_M, info.cloudEvolution.xy) * GAME_UNITS_PER_METER;
				coverage = saturate(TexCloudModeling.SampleLevel(TileableSampler, (field - shear) * info.lowNdfFrequency + 0.5, 0).r);
			}
			const float floor = TexLocalNdfMaximum.SampleLevel(TransmittanceSampler, float3(local.uv, i), 0).r;
			weight += endpoint.x * (floor > coverage ? 1.0 : 0.0);
		}
		else weight += endpoint.x * TexLocalNdfModeling.SampleLevel(TransmittanceSampler, float3(local.uv, i), 0).a;
	}
	return saturate(weight);
}

bool LocalNdfIntersectsRay(float2 origin, float2 direction, VolumetricCloudData info)
{
#ifdef NO_LOCAL_NDF
	return false;
#endif
	if (info.localNdfAltitude.z <= 0.0)
		return false;
	const float2 uv = LocalNdfUV(origin, info);
	const float2 velocity = direction * GAME_UNIT_TO_M * info.localNdfRect.zw;
	const float2 shear = info.cloudShapeShear * 61.0 * GAME_UNIT_TO_M * info.localNdfRect.zw;
	const float2 lower = min(shear, 0.0);
	const float2 upper = 1.0 + max(shear, 0.0);
	float enter = 0.0;
	float rayExit = 3.402823466e+38;
	[unroll] for (uint axis = 0u; axis < 2u; ++axis)
	{
		if (abs(velocity[axis]) < 1e-20) {
			if (uv[axis] < lower[axis] || uv[axis] > upper[axis])
				return false;
		} else {
			const float2 interval = (float2(lower[axis], upper[axis]) - uv[axis]) / velocity[axis];
			enter = max(enter, min(interval.x, interval.y));
			rayExit = min(rayExit, max(interval.x, interval.y));
		}
	}
	return rayExit >= enter;
}

float3 GetCloudDirectionalLightColor()
{
	// Top-of-atmosphere irradiance, before ground sunset dimming or attenuation.
	if (SharedData::physSkyData.volCloudUseSun != 0)
		return SharedData::physSkyData.sunlightColor;
	return Color::Light(SharedData::DirLightColor.xyz);
}

RWTexture2D<float> RWTexTr : register(u0);
RWTexture2D<float4> RWTexLum : register(u1);
RWTexture2D<float4> RWTexAux : register(u2);

RWTexture3D<float> RWShadowVolume : register(u0);

RWTexture2DArray<float> RWTexCubeTr : register(u0);
RWTexture2DArray<float4> RWTexCubeLum : register(u1);
RWTexture2DArray<float4> RWTexCubeAux : register(u2);
RWTexture2D<sh2> RWCloudAmbientSH : register(u0);

float RayIntersectSphereCentered(float3 orig, float3 dir, float r)
{
	return RayIntersectSphere(orig, dir, 0, r);
}

float3 SampleCloudAmbientRadiance()
{
	// The upper hemisphere's first moment gives irradiance / pi.
	return max(0.0, -float3(TexCloudAmbientSH[int2(0, 0)].y,
						TexCloudAmbientSH[int2(1, 0)].y, TexCloudAmbientSH[int2(2, 0)].y) *
						(RCP_PI / 0.4886025119));
}

float4 SampleCloudAerialPerspective(float3 viewDir, float distance, float shadow)
{
	const SharedData::PhysSkyData data = SharedData::physSkyData;
	uint3 apDims;
	TexAerialPerspective.GetDimensions(apDims.x, apDims.y, apDims.z);
	if (any(apDims == 0u))
		return float4(0.0, 0.0, 0.0, 1.0);
	const float depthSlice = ApDepthUv(distance, apDims.z);
	const float3 apUv = float3(SkyViewLutUv(viewDir, apDims.y), depthSlice);
	float4 ap = TexAerialPerspective.SampleLevel(SkyViewSampler, apUv, 0);
	ap.rgb *= 1.0 - saturate(shadow);
	ap.rgb *= data.apLumMix;
	ap.a = lerp(1.0, ap.a, data.apTrMix);
	return ap;
}

RWTexture2D<float4> RWCloudAmbientPartial : register(u1);
Texture2D<float4> TexCloudAmbientPartial : register(t90);
groupshared float4 gCloudAmbientPartial[8][32];

float3 CloudAmbientAtmosphereSource(float3 position, float3 direction, float3 lightDirection, float3 color, float3 rayleigh, float3 mie)
{
	if (!any(color > 0.0) || dot(lightDirection, lightDirection) < 0.5)
		return 0.0;
	const float cosine = dot(direction, lightDirection);
	const float3 direct = SampleAtmosphereLightTr(TexTransmittance, TransmittanceSampler, position, lightDirection) *
	                      (rayleigh * Phase::Rayleigh(cosine) + mie * Phase::CornetteShanks(cosine, SharedData::physSkyData.aerosolPhaseG));
	const float radius = length(position);
	const float3 multiple = TexCloudAtmosphereMs.SampleLevel(TransmittanceSampler, MsLutUv(radius, dot(position / radius, lightDirection)), 0).rgb;
	return color * (direct + (rayleigh + mie) * multiple);
}

float3 CloudAmbientAtmosphere(float radius, float boundaryRadius, float3 direction, uint steps, out float3 transmission, out float boundaryDistance)
{
	const SharedData::PhysSkyData sky = SharedData::physSkyData;
	const float projected = radius * direction.z;
	const float difference = (sky.rAtmosphere - radius) * (sky.rAtmosphere + radius);
	const float atmosphereRoot = sqrt(max(projected * projected + difference, 0.0));
	float distance = projected >= 0.0 ? difference / max(atmosphereRoot + projected, 1e-6) : atmosphereRoot - projected;
	const float groundOffset = (radius - boundaryRadius) * (radius + boundaryRadius);
	const float groundDiscriminant = projected * projected - groundOffset;
	boundaryDistance = projected < 0.0 && groundDiscriminant >= 0.0 ?
	                       groundOffset / max(-projected + sqrt(max(groundDiscriminant, 0.0)), 1e-6) :
	                       -1.0;
	if (boundaryDistance >= 0.0)
		distance = boundaryDistance;
	transmission = 1.0;
	if (distance <= 0.0)
		return 0.0;
	float3 radiance = 0.0;
	float start = 0.0;
	[loop] for (uint i = 0u; i < steps; ++i)
	{
		const float fraction = float(i + 1u) / float(steps);
		const float end = distance * fraction * fraction;
		const float segmentLength = end - start;
		const float3 position = float3(0, 0, radius) + direction * (0.5 * (start + end));
		float r, m, o;
		SampleAtmosphere(max(length(position) - sky.rPlanet, 0.0), r, m, o);
		const float3 rayleigh = r * sky.rayleighScatter;
		const float3 mie = m * sky.aerosolScatter;
		const float3 extinction = rayleigh + mie + m * sky.aerosolAbsorption + o * sky.ozoneAbsorption;
		const float3 stepTransmission = exp(-extinction * segmentLength);
		const float3 opticalDepth = extinction * segmentLength;
		const float3 factor = lerp((1.0 - stepTransmission) / max(extinction, 1e-20),
			segmentLength * (1.0 - 0.5 * opticalDepth), float3(opticalDepth < 1e-3));
		float3 source = CloudAmbientAtmosphereSource(position, direction, sky.sunDir, sky.sunlightColor, rayleigh, mie);
		source += CloudAmbientAtmosphereSource(position, direction, sky.masserDir, sky.masserColor, rayleigh, mie);
		source += CloudAmbientAtmosphereSource(position, direction, sky.secundaDir, sky.secundaColor, rayleigh, mie);
		radiance += transmission * source * factor;
		transmission *= stepTransmission;
		start = end;
	}
	return radiance;
}

float3 CloudAmbientSky(float3 direction, float altitude)
{
	float3 transmission;
	float boundaryDistance;
	return CloudAmbientAtmosphere(SharedData::physSkyData.rPlanet + max(altitude, GAME_UNITS_PER_METER),
		SharedData::physSkyData.rPlanet, direction, 32u, transmission, boundaryDistance);
}

float3 CloudAmbientGround(float3 direction, float3 groundRadiance, float altitude)
{
	const SharedData::PhysSkyData sky = SharedData::physSkyData;
	const float radius = sky.rPlanet + max(altitude, GAME_UNITS_PER_METER);
	// The reflection already includes transport below the probe.
	const float probeRadius = clamp(sky.zCameraPlanet, sky.rPlanet, radius);
	float3 transmission;
	float boundaryDistance;
	float3 radiance = CloudAmbientAtmosphere(radius, probeRadius, direction, 16u, transmission, boundaryDistance);
	if (boundaryDistance >= 0.0)
		radiance += transmission * groundRadiance;
	return radiance;
}

// Keep the same 256 directions, distributed over eight independent groups.
// Ground reflection is linear in the transport integral, so its average can
// be applied after reduction rather than serializing every direction on it.
[numthreads(32, 1, 1)] void buildCloudAmbientSH(uint lane : SV_GroupIndex, uint3 group : SV_GroupID) {
	const uint directionIndex = group.x * 32u + lane;
	const uint2 tid = uint2(directionIndex % 16u, directionIndex / 16u);
	uint reflectionWidth, reflectionHeight;
	TexCloudGroundReflection.GetDimensions(reflectionWidth, reflectionHeight);
	const float2 faceSize = max(float2(reflectionWidth, reflectionHeight), 1.0);
	const float2 faceTexel = floor((float2(tid) + 0.5) * faceSize / 16.0);
	const float2 faceUv = 2.0 * (faceTexel + 0.5) / faceSize - 1.0;
	const float3 reflectionDirection = float3(-faceUv.x, -faceUv.y, -1.0);
	const float inverseLength = rsqrt(1.0 + dot(faceUv, faceUv));
	const float solidAngleWeight = inverseLength * inverseLength * inverseLength;
	float3 groundRadiance;
	if (reflectionWidth > 0u && reflectionHeight > 0u)
		groundRadiance = ColorManagement::SceneToLinear(max(TexCloudGroundReflection.SampleLevel(TileableSampler, reflectionDirection, 0).rgb, 0.0));
	else
		groundRadiance = ColorManagement::SceneToLinear(Color::Ambient(max(SharedData::GetAmbient(float3(0, 0, -1)), 0.0)));
	gCloudAmbientPartial[7][lane] = float4(groundRadiance * solidAngleWeight, solidAngleWeight);

	const float2 sampleCoord = (float2(tid) + 0.5) / 16.0;
	const float elevationCoordinate = 1.0 - sampleCoord.y;
	const float3 shSampleDir = SphericalHarmonics::GetUniformSphereSample(sampleCoord.x, 0.5 * (1.0 - elevationCoordinate * elevationCoordinate));
	const float3 rayDir = shSampleDir.xzy;
	const float3 skyColor = CloudAmbientSky(rayDir, VolumetricCloudBuffer[0].highestCloudAltitude);
	const float shFactor = 4.0 * Math::PI * elevationCoordinate / 256.0;
	const sh2 sh = SphericalHarmonics::Evaluate(shSampleDir);
	gCloudAmbientPartial[0][lane] = SphericalHarmonics::Scale(sh, skyColor.r * shFactor);
	gCloudAmbientPartial[1][lane] = SphericalHarmonics::Scale(sh, skyColor.g * shFactor);
	gCloudAmbientPartial[2][lane] = SphericalHarmonics::Scale(sh, skyColor.b * shFactor);
	const float3 groundDirection = rayDir * float3(1.0, 1.0, -1.0);
	const SharedData::PhysSkyData sky = SharedData::physSkyData;
	const float radius = sky.rPlanet + max(VolumetricCloudBuffer[0].lowestCloudAltitude, GAME_UNITS_PER_METER);
	const float probeRadius = clamp(sky.zCameraPlanet, sky.rPlanet, radius);
	float3 transmission;
	float boundaryDistance;
	const float3 ground = CloudAmbientAtmosphere(radius, probeRadius, groundDirection, 16u, transmission, boundaryDistance);
	transmission *= boundaryDistance >= 0.0 ? 1.0 : 0.0;
	const float weight = 2.0 * elevationCoordinate / 256.0;
	const float cosine = -2.0 * groundDirection.z;
	gCloudAmbientPartial[3][lane] = float4(ground * weight, 0.0);
	gCloudAmbientPartial[4][lane] = float4(ground * weight * cosine, 0.0);
	gCloudAmbientPartial[5][lane] = float4(transmission * weight, 0.0);
	gCloudAmbientPartial[6][lane] = float4(transmission * weight * cosine, 0.0);
	GroupMemoryBarrierWithGroupSync();
	[unroll] for (uint stride = 16u; stride > 0u; stride >>= 1u)
	{
		if (lane < stride) {
			[unroll] for (uint coefficient = 0u; coefficient < 8u; ++coefficient)
			{
				gCloudAmbientPartial[coefficient][lane] += gCloudAmbientPartial[coefficient][lane + stride];
			}
		}
		GroupMemoryBarrierWithGroupSync();
	}
	if (lane == 0u) {
		[unroll] for (uint coefficient = 0u; coefficient < 8u; ++coefficient)
		{
			RWCloudAmbientPartial[uint2(coefficient, group.x)] = gCloudAmbientPartial[coefficient][0];
		}
	}
}

	[numthreads(8, 1, 1)] void reduceCloudAmbientSH(uint lane : SV_GroupIndex)
{
	[unroll] for (uint coefficient = 0u; coefficient < 8u; ++coefficient)
	{
		gCloudAmbientPartial[coefficient][lane] = TexCloudAmbientPartial[uint2(coefficient, lane)];
	}
	GroupMemoryBarrierWithGroupSync();
	[unroll] for (uint stride = 4u; stride > 0u; stride >>= 1u)
	{
		if (lane < stride) {
			[unroll] for (uint coefficient = 0u; coefficient < 8u; ++coefficient)
			{
				gCloudAmbientPartial[coefficient][lane] += gCloudAmbientPartial[coefficient][lane + stride];
			}
		}
		GroupMemoryBarrierWithGroupSync();
	}
	if (lane == 0u) {
		const float3 reflection = gCloudAmbientPartial[7][0].rgb / max(gCloudAmbientPartial[7][0].w, 1e-6);
		RWCloudAmbientSH[int2(0, 0)] = gCloudAmbientPartial[0][0];
		RWCloudAmbientSH[int2(1, 0)] = gCloudAmbientPartial[1][0];
		RWCloudAmbientSH[int2(2, 0)] = gCloudAmbientPartial[2][0];
		RWCloudAmbientSH[int2(3, 0)] = float4(gCloudAmbientPartial[3][0].rgb + reflection * gCloudAmbientPartial[5][0].rgb, 0.0);
		RWCloudAmbientSH[int2(4, 0)] = float4(gCloudAmbientPartial[4][0].rgb + reflection * gCloudAmbientPartial[6][0].rgb, 0.0);
		RWCloudAmbientSH[int2(5, 0)] = float4(reflection, 0.0);
	}
}

struct CloudRaySegments
{
	float2 nearSegment;
	float2 farSegment;
	float nearLength;
	float length;
};

CloudRaySegments GetCloudRaySegments(float3 origin, float3 dir, float bottomAltitude, float topAltitude,
	VolumetricCloudData info)
{
	CloudRaySegments segments = (CloudRaySegments)0;
	if (topAltitude <= bottomAltitude)
		return segments;
	const float2 outer = IntersectCloudBelow(origin, dir, topAltitude, info.cloudCurvatureRadius);
	const float entry = max(outer.x, 50.0 * GAME_UNITS_PER_METER);
	const float exit = min(outer.y, info.rayMarchRange);
	if (exit <= entry)
		return segments;

	const float2 inner = IntersectCloudBelow(origin, dir, bottomAltitude, info.cloudCurvatureRadius);
	segments.nearSegment = float2(entry, exit);
	// A grazing ray can cross the layer twice; skip the clear gap between crossings.
	if (inner.y > entry && inner.x < exit) {
		segments.nearSegment.y = clamp(inner.x, entry, exit);
		segments.farSegment = float2(clamp(inner.y, entry, exit), exit);
		if (segments.nearSegment.y <= entry) {
			segments.nearSegment = segments.farSegment;
			segments.farSegment = 0.0;
		}
	}
	segments.nearLength = segments.nearSegment.y - segments.nearSegment.x;
	segments.length = segments.nearLength + segments.farSegment.y - segments.farSegment.x;
	return segments;
}

float CloudTemporalMarchHash(float2 pixel, float frame)
{
	const float3 seed = frac(float3(pixel, frame) * 0.1031);
	const float scramble = dot(seed, seed.zyx + 31.32);
	return frac((seed.y + seed.x + scramble * 2.0) * (scramble + seed.z));
}

float CloudSpatialMarchHash(float2 position)
{
	const float2 seed = frac(position * 0.1031);
	const float scramble = dot(seed.xyx, seed.yxx + 19.19);
	return frac((seed.y + seed.x + scramble * 2.0) * (scramble + seed.x));
}

float CloudDepthWeight(float transmittance, float density, float distance)
{
#ifdef CLOUD_REFLECTION
	// Raw transmission makes moments associative across ordered ray segments.
	return transmittance * density * (1.0 - exp2(distance * GAME_UNIT_TO_M * -0.36067376));
#else
	return saturate((transmittance - 0.1) * 1.1111112) * density * (1.0 - exp2(distance * GAME_UNIT_TO_M * -0.36067376));
#endif
}

float2 LowNdfUV(float2 worldXY, VolumetricCloudData info)
{
	return worldXY * info.lowNdfFrequency + 0.5;
}

float CloudViewStep(float distance, float angularFootprint)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	const float stepScale = angularFootprint > 0.0 ? 2.0 : info.marchStepScale;
	const float viewStep = (3.0 + distance * GAME_UNIT_TO_M * (60.0 / 16384.0)) * stepScale * GAME_UNITS_PER_METER;
	if (angularFootprint <= 0.0)
		return viewStep;
	// Resolve the final reflection texel, independently of the checkerboard spacing.
	return max(viewStep, max(3.0 * GAME_UNITS_PER_METER, distance * angularFootprint * 0.25));
}

struct NDFInfo
{
	bool in_layer;
	float dimension_profile;
	float coverage;
	float height_fraction;
	float shaping_start;
	float2 altitude;
	float top_type;
	float bottom_type;
	float storm;
	float rawStorm;
	bool stormVolume;
	uint stormIndex;
	float stormBlend;
	float replacement;
#ifdef CLOUD_VOXELS
	float voxelBlend;
	float voxelDensityScale;
#endif
#ifndef NO_CLOUD_STORMS
	float plumeCoordinate;
	float plumeDensity;
	float2 plumeEnvelope;
#endif
};

void initNDFInfo(out NDFInfo ndf)
{
	ndf = (NDFInfo)0;
#ifndef NO_CLOUD_STORMS
	ndf.plumeDensity = 1.0;
#endif
}

float3 SampleCloudHeights(float2 worldXY, float2 anchoredXY)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	float3 heights = saturate(TexCloudHeight.SampleLevel(TileableSampler, LowNdfUV(worldXY, info), 0).rgb);
	heights.xy = lerp(info.lowCloudBaseAltitude, info.lowCloudTopAltitude, heights.xy);
	return LocalNdfHeight(heights, anchoredXY, info);
}

bool IsVolcanic(CloudStormData storm);

float CloudNdfProfile(inout NDFInfo ndf)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	const float top = TexCloudProfileLUT.SampleLevel(TransmittanceSampler, float2(ndf.top_type, ndf.height_fraction), 0).g;
	float expandedTop = top;
	[branch] if (info.topExpansion > 0.0)
		expandedTop = TexCloudAdjustmentLUT.SampleLevel(TransmittanceSampler, float2(ndf.top_type, ndf.height_fraction), 0).r;
	const float bottom = TexCloudProfileLUT.SampleLevel(TransmittanceSampler, float2(ndf.bottom_type, ndf.height_fraction), 0).r;
	// The eruption's authored top types must not inherit the weather's anvil
	// expansion, which otherwise turns low type values into full-height columns.
	float topProfile = ndf.stormVolume && IsVolcanic(CloudStorms[ndf.stormIndex]) ? top : lerp(top, max(top, expandedTop), info.topExpansion);
	[branch] if (ndf.storm > 0.15 && (!ndf.stormVolume || !IsVolcanic(CloudStorms[ndf.stormIndex])))
		topProfile = max(top, TexCloudProfileLUT.SampleLevel(TransmittanceSampler, float2(ndf.top_type, 1.0 - ndf.height_fraction), 0).g);
	const float verticalProfile = max(topProfile * bottom, 0.0);
	const float heightFade = saturate(ndf.height_fraction / info.coverageHeightRange);
	const float exponent = lerp(lerp(lerp(info.coverageBottomPower, 1.0, heightFade), 1.0, saturate((ndf.storm - 0.15) * 20.0)), 1.0, heightFade);
	const float coverageProfile = pow(ndf.coverage, exponent);
	return verticalProfile * coverageProfile;
}

NDFInfo sampleNDF(float2 worldXY, float2 anchoredXY, float profileAltitude)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	NDFInfo ndf;
	initNDFInfo(ndf);
	const float3 heights = SampleCloudHeights(worldXY, anchoredXY);
	const float minAltitude = heights.r;
	const float maxAltitude = heights.g;
	if (maxAltitude <= minAltitude || profileAltitude <= minAltitude || profileAltitude >= maxAltitude)
		return ndf;
	ndf.shaping_start = heights.b;
	ndf.altitude = heights.rg;
	const float4 range = info.localNdfAuxiliary;
	[branch] if (range.z != range.w && InLocalNdf(LocalNdfUV(anchoredXY, info), info))
	{
		const LocalNdfModelSample local = SampleLocalNdfModel(anchoredXY, info);
		if (HasLocalNdfInfluence(local)) {
			const float3 baseModel = saturate(TexCloudModeling.SampleLevel(TileableSampler, LowNdfUV(worldXY, info), 0));
			const float topType = ApplyLocalNdfModel(baseModel, local).g;
			ndf.shaping_start = saturate(lerp(range.z, range.w, saturate((topType - range.x) / (range.y - range.x))));
		}
	}
	ndf.height_fraction = saturate((profileAltitude - minAltitude) / (maxAltitude - minAltitude));
	const float shearBlend = smoothstep(0.0, 1.0, saturate((ndf.height_fraction - 0.1) * 1.5384616));
	const bool sampleUpstream = shearBlend > 0.0 && any(info.cloudShapeShear != 0.0);
	const float2 modelingXY = worldXY - info.cloudShapeShear * smoothstep(0.0, 1.0, ndf.height_fraction);
	float3 model = saturate(TexCloudModeling.SampleLevel(TileableSampler, LowNdfUV(modelingXY, info), 0));
	float3 upstream = model;
	[branch] if (sampleUpstream)
		upstream = saturate(TexCloudModeling.SampleLevel(TileableSampler, LowNdfUV(modelingXY - info.cloudShapeShear * 60.0, info), 0));
	const float2 anchoredModelXY = anchoredXY - info.cloudShapeShear * smoothstep(0.0, 1.0, ndf.height_fraction);
	model = ApplyLocalNdfModel(model, SampleLocalNdfModel(anchoredModelXY, info));
	[branch] if (sampleUpstream)
		upstream = ApplyLocalNdfModel(upstream, SampleLocalNdfModel(anchoredModelXY - info.cloudShapeShear * 60.0, info));
	else upstream = model;
	ndf.coverage = lerp(model.r, max(model.r, upstream.r), shearBlend);
	if (ndf.coverage < 1e-8)
		return ndf;
	ndf.in_layer = true;
	ndf.top_type = lerp(model.g, upstream.g, shearBlend) * saturate(ndf.coverage * 10.0);
	ndf.bottom_type = lerp(model.b, upstream.b, shearBlend);
	ndf.dimension_profile = CloudNdfProfile(ndf);
	return ndf;
}

struct CloudDensityContext
{
	NDFInfo ndf;
	float3 noise_coordinates;
	float eye_distance;
	float light_density;
	float weather_height_fraction;
	float3 storm_displacement;
};

float4 SampleCloudStormNoise(float3 positionMeters, float height, float mip, uint index, NDFInfo ndf);

float sampleCloudDensityFromContext(
	inout CloudDensityContext density_context, float mipBias, float minimumMip)
{
	density_context.light_density = 0.0;
	const NDFInfo ndf = density_context.ndf;
	if (!ndf.in_layer || ndf.dimension_profile <= 1e-10)
		return 0.0;

	const VolumetricCloudData info = VolumetricCloudBuffer[0];
#ifdef NO_CLOUD_STORMS
	const bool risingBillows = false;
#else
	const bool risingBillows = ndf.stormVolume && IsVolcanic(CloudStorms[ndf.stormIndex]);
#endif
	const float profileMip = ndf.dimension_profile * 3.0;
	// Density is nonlinear in noise; coarse averages must not erase the cloud shape.
	// The dense eruption needs its cellular surface at full detail. Cone/footprint
	// filtering still applies, but density alone must not blur the noise first.
#ifdef NO_CLOUD_STORMS
	const float biasedMip = floor(profileMip) + mipBias;
#else
	const float biasedMip = risingBillows ? mipBias : (ndf.stormVolume ? floor(profileMip + mipBias) : floor(profileMip) + mipBias);
#endif
	const float mip = max(biasedMip, min(minimumMip, biasedMip + 1.0));
	// Storms have their own channel-dependent deformation and keep their noise.
	const bool voxelNoise = info.useVoxelNoise != 0u && !ndf.stormVolume;
	float4 noise = 0.0;
	[branch] if (info.skipShapeNoise == 0u)
	{
#ifndef NO_CLOUD_STORMS
		[branch] if (ndf.stormVolume)
			noise = SampleCloudStormNoise(density_context.noise_coordinates, ndf.height_fraction, mip, ndf.stormIndex, ndf);
		else
#endif
		{
			[branch] if (voxelNoise)
				noise = NoiseVoxel.SampleLevel(TileableSampler, density_context.noise_coordinates, mip);
			else noise = TexCloudShapeNoise.SampleLevel(TileableSampler, density_context.noise_coordinates, mip);
		}
	}
	float profile = ndf.dimension_profile;
#ifndef NO_CLOUD_STORMS
	if (risingBillows) {
		// G is generated by the local Alligator kernel. Use its large cells to
		// displace the carrier surface in both directions, before fine erosion.
		// A clipped gain makes every value above .4 the same displacement,
		// flattening the lobe tips onto an offset copy of the carrier surface.
		// Keep the full Alligator range; sqrt also matches the rounded noise mix.
		const float lobe = info.skipShapeNoise != 0u ? 0.0 : ndf.plumeEnvelope.y * (sqrt(noise.g) - .35);
		profile *= saturate((lobe - ndf.plumeEnvelope.x) / 90.0 + .5);
		density_context.ndf.dimension_profile = profile;
	}
#endif
	float eroded = saturate(profile);
	[branch] if (info.skipShapeNoise == 0u)
	{
		[branch] if (voxelNoise)
			eroded = ReconstructNdfVoxelNoiseDensity(noise, profile, ndf.top_type, ndf.height_fraction);
		else eroded = ReconstructCloudNoiseDensity(noise, profile, ndf.top_type, ndf.height_fraction, density_context.eye_distance, ndf.storm, risingBillows);
	}
#ifdef NO_CLOUD_STORMS
	const float densityScale = info.lowDensityScale;
#else
	const float densityScale = ndf.stormVolume ? lerp(info.lowDensityScale, CloudStorms[ndf.stormIndex].driftDensity.z * ndf.plumeDensity, ndf.stormBlend) : info.lowDensityScale;
#endif
	float lightDensity;
	const float density = ShapeCloudBaseDensity(eroded, ndf.height_fraction, info.bottomDensityWidth, info.bottomDensityPower, ndf.shaping_start, ndf.rawStorm, lightDensity, risingBillows) * densityScale;
	// The supercell's enhanced shadow density belongs to its anvil model.
	// An opaque eruption shades the same eroded surface that the view ray sees.
#ifdef NO_CLOUD_STORMS
	density_context.light_density = density;
#else
	density_context.light_density = ndf.stormVolume && !risingBillows ? lightDensity * densityScale : density;
#endif
	return density;
}

float sampleWeatherCloudDensity(
	float3 pos, CloudLayer cloud, float mipBias, float viewDistance, float minimumMip,
	out CloudDensityContext density_context)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	initNDFInfo(density_context.ndf);
	density_context.noise_coordinates = 0.0;
	density_context.eye_distance = 0.0;
	density_context.light_density = 0.0;
	density_context.weather_height_fraction = 0.0;
	density_context.storm_displacement = 0.0;

	const float profileAltitude = CloudProfileAltitude(pos, FrameBuffer::CameraPosAdjust.xy, info.cloudCurvatureRadius);
	if (profileAltitude < cloud.lowestAltitude || profileAltitude > cloud.highestAltitude)
		return 0.0;

	const float2 fieldMeters = CloudFieldPosition((pos.xy - info.noiseWindOffset) * GAME_UNIT_TO_M, info.cloudEvolution.xy);
	density_context.ndf = sampleNDF(fieldMeters * GAME_UNITS_PER_METER, pos.xy, profileAltitude);
	density_context.weather_height_fraction = density_context.ndf.height_fraction;
	if (!density_context.ndf.in_layer || density_context.ndf.dimension_profile <= 0.0)
		return 0;

	const float bottomFade = saturate((density_context.ndf.height_fraction - 0.02) * 33.333332);
	const float2 xy = fieldMeters;
	float2 noiseXY = xy * 0.0043545123;
	[branch] if (info.skipShapeNoise == 0u && bottomFade < 1.0)
	{
		const float2 displacement = TexCloudAdjustmentLUT.SampleLevel(TileableSampler, xy * 0.004, 0).gb;
		noiseXY += (displacement * 2.0 - 1.0) * (0.125 - bottomFade * 0.125);
	}
	density_context.noise_coordinates = float3(noiseXY,
		(pos.z * GAME_UNIT_TO_M - 20.0 + bottomFade * 10.0) * 0.0034834063 - info.cloudEvolution.x * (1.0 / 6.283185307179586));
	density_context.eye_distance = viewDistance;
	return sampleCloudDensityFromContext(density_context, mipBias, minimumMip);
}

#ifdef CLOUD_VOXELS
#	define sampleCloudDensity sampleNdfCloudDensity
#endif
#include "PhysicalSky/CloudStorm.hlsli"
#ifdef CLOUD_VOXELS
#	undef sampleCloudDensity
#endif
#include "PhysicalSky/CloudVoxel.hlsli"

#include "PhysicalSky/MidCloud.hlsli"

float3 sampleExternalSunTransmittance(float3 pos, float3 sun_dir)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	const float3 pos_planet = pos + float3(-FrameBuffer::CameraPosAdjust.xy, info.planetRadius);

	// Finite scene-shadow coverage would project a camera-following boundary into the cloud layer.
	return SampleAtmosphereLightTr(TexTransmittance, TransmittanceSampler, pos_planet, sun_dir);
}

float3 CirrusBoundaryRadiance(float3 position, float3 direction, float3 incoming);
float3 MiddleBoundaryRadiance(float3 position, float3 direction, float2 opticalDepth);
#include "PhysicalSky/CloudAmbient.hlsli"
#include "PhysicalSky/CloudLighting.hlsli"

struct VolumetricCloudResult
{
	float2 lightning;
	float3 transmittance;
	float3 lum;
	float cloud_depth;
	float2 motionMetadata;
	float4 stormMotion;
#ifdef CLOUD_REFLECTION
	float3 transportLum;
	float2 rawTransmission;
	float4 integralWeights;
#endif
};

float CirrusDensity(float2 position, out float profile)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	const float2 weather = saturate(TexCirrusWeather.SampleLevel(TileableSampler, position * (GAME_UNIT_TO_M / 16384.0 * info.cirrusUvInvSize) + 0.5, 0));
	const float3 patterns = TexCirrusPatterns.SampleLevel(TileableSampler, (position - info.cirrusWindOffset) * (GAME_UNIT_TO_M * 0.0002331 * info.cirrusUvInvSize), 0);
	const float3 squared = patterns * patterns;
	profile = lerp(lerp(squared.b, squared.r, saturate(weather.y * 2.0)), squared.g, saturate(weather.y * 2.0 - 1.0));
	profile = pow(max(profile, 1e-10), 1.9 - weather.x * 1.8) * saturate(weather.x * weather.x * weather.x * 2.0);
	return (profile * 2.0) * info.cirrusDensityScale;
}

// Delta-scaled forward transport belongs to the incident background, whether
// that is sky, terrain, or another cloud sheet. Keep it in the transmission
// coefficient instead of substituting sky radiance into the scattering output.
float CloudBackgroundTransmittance(float direct, float transport)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	const float forwardWeight = saturate(info.ambientStrength * info.cloudLightingScale);
	return lerp(direct, max(direct, transport), forwardWeight);
}

void IntegrateCirrus(float3 position, float3 direction, float distance, float phase, CloudAmbient cloudAmbient,
	inout float3 radiance, inout float transmittance, inout float ambientTransmittance,
	inout float depthSum, inout float weightSum, inout float cirrusWeight)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	const bool ambientEnabled = info.ambientStrength > 0.0 && (info.msAlbedo > 0.0 || CloudStormCount() > 0u);
	if (max(transmittance, ambientEnabled ? ambientTransmittance : 0.0) <= 0.1)
		return;
	float profile;
	const float density = CirrusDensity(position.xy, profile);
	if (density <= 1e-10)
		return;
	const float3 shellPosition = position + float3(-FrameBuffer::CameraPosAdjust.xy, info.cloudCurvatureRadius);
	const float3 normal = normalize(shellPosition);
	const float lightCosine = dot(normal, info.dirlightDir);
	const float viewCosine = dot(normal, direction);
	const float lightStep = CloudCirrusLightDistance(lightCosine) * 0.25;
	float lightDepth = 0.0;
	[unroll] for (uint i = 0u; i < 4u; ++i)
	{
		float unusedProfile;
		const float shadowDistance = (float(i) + 0.5) * lightStep;
		lightDepth += CirrusDensity(position.xy + info.dirlightDir.xy * (shadowDistance * GAME_UNITS_PER_METER), unusedProfile);
	}
	// The center-to-boundary samples estimate half of the layer's light optical depth.
	lightDepth *= 2.0 * lightStep * 10.0 / info.cirrusLightThickness * info.sunExtinction;
	const float viewDepth = density * 10.0;
	// A capped grazing light path samples only part of the layer's vertical extent.
	const float lightSpan = clamp(lightCosine * (8.0 * lightStep / info.cirrusLightThickness), -1.0, 1.0);
	const float tangentCosine = sqrt(info.cirrusLightThickness / ((info.cloudCurvatureRadius + info.cirrusAltitude) * GAME_UNIT_TO_M));
	const float viewSpan = clamp(viewCosine / tangentCosine, -1.0, 1.0);
	const float nearFraction = 0.5 * (1.0 + viewSpan * lightSpan);
	const float transmission = CloudCirrusTransmission(viewDepth, lightDepth, nearFraction);
	const float sun = transmission * phase;
	const float3 sunlight = sampleExternalSunTransmittance(position, info.dirlightDir) * GetCloudDirectionalLightColor();
	const float ambientResponse = info.ambientStrength * (direction.z + 1.0) * pow(saturate(1.0 - profile * 0.3), 0.2);
	const float3 ambientRadiance = CloudAmbientRadiance(position + float3(-FrameBuffer::CameraPosAdjust.xy, info.planetRadius), cloudAmbient) * ambientResponse;
	const float stepTransmittance = exp(-viewDepth);
	// The collapsed forward peak transports sheet radiance as well as sky radiance.
	const float weight = CloudBackgroundTransmittance(transmittance, ambientEnabled ? ambientTransmittance : transmittance) * (1.0 - stepTransmittance);
	radiance += weight * info.cloudLightingScale * (sun * sunlight + ambientRadiance);
	const float depthWeight = CloudDepthWeight(transmittance, density, distance);
	depthSum += depthWeight * distance;
	weightSum += depthWeight;
	cirrusWeight += depthWeight;
	transmittance *= stepTransmittance;
	ambientTransmittance *= stepTransmittance;
}

#ifdef CLOUD_REFLECTION
// Resolve the sheet once with unit incident transmission, then retain the two
// transport terms separately until the ray segments have been composed.
void IntegrateReflectionCirrus(float3 position, float3 direction, float distance, float phase, CloudAmbient ambient,
	inout VolumetricCloudResult result, inout float transmission, inout float transportTransmission,
	inout float depthSum, inout float weightSum, inout float cirrusWeight)
{
	float3 source = 0.0;
	float sheetTransmission = 1.0, sheetTransport = 1.0;
	float sheetDepth = 0.0, sheetWeight = 0.0, sheetCirrusWeight = 0.0;
	IntegrateCirrus(position, direction, distance, phase, ambient, source, sheetTransmission, sheetTransport,
		sheetDepth, sheetWeight, sheetCirrusWeight);
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	const bool ambientEnabled = info.ambientStrength > 0.0 && (info.msAlbedo > 0.0 || CloudStormCount() > 0u);
	const float forwardWeight = ambientEnabled ? saturate(info.ambientStrength * info.cloudLightingScale) : 0.0;
	result.lum += (1.0 - forwardWeight) * transmission * source;
	result.transportLum += forwardWeight * transportTransmission * source;
	depthSum += transmission * sheetDepth;
	weightSum += transmission * sheetWeight;
	cirrusWeight += transmission * sheetCirrusWeight;
	transmission *= sheetTransmission;
	transportTransmission *= sheetTransport;
}
#endif

bool CloudIntervalContains(CloudRaySegments segments, float distance)
{
	return (distance >= segments.nearSegment.x && distance < segments.nearSegment.y) ||
	       (distance >= segments.farSegment.x && distance < segments.farSegment.y);
}

float2 CloudReflectionInterval(float3 eye, float3 direction, float begin, float end)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	uint2 size, tiles;
	TexCloudHeight.GetDimensions(size.x, size.y);
	TexCloudHeightBoundsTiles.GetDimensions(tiles.x, tiles.y);
	if (any(size % tiles != 0u))
		return float2(begin, end);
	const float2 position = (eye.xy + direction.xy * begin - info.noiseWindOffset) * GAME_UNIT_TO_M;
	const float2 uv = LowNdfUV(CloudFieldPosition(position, info.cloudEvolution.xy) * GAME_UNITS_PER_METER, info);
	const int2 cell = int2(floor(uv * tiles));
	const float2 margin = 0.5 / float2(size);
	const float2 lower = (float2(cell) - 1.0) / tiles + margin;
	const float2 upper = (float2(cell) + 2.0) / tiles - margin;
	// Bound the warp derivative along the ray, including changing wave phases.
	const float2 waveRate = abs(float2(dot(direction.xy, float2(0.0007, 0.0004)), dot(direction.xy, float2(-0.0003, 0.0009))));
	const float2 warpRate = abs(info.cloudEvolution.y) * float2(dot(waveRate, float2(80, 40)), dot(waveRate, float2(30, 60)));
	const float2 uvRate = (abs(direction.xy) + warpRate) * abs(info.lowNdfFrequency);
	const float2 clearance = max(min(uv - lower, upper - uv), 0.0);
	const float2 travel = clearance / max(uvRate, 1e-20);
	const float blockEnd = min(end, begin + min(travel.x, travel.y));
	if (blockEnd <= begin)
		return float2(begin, end);
	float2 heights = float2(1, 0);
	[unroll] for (int y = -1; y <= 1; ++y)
	{
		[unroll] for (int x = -1; x <= 1; ++x)
		{
			const int2 address = ((cell + int2(x, y)) % int2(tiles) + int2(tiles)) % int2(tiles);
			const float2 tile = TexCloudHeightBoundsTiles.Load(int3(address, 0)).xy;
			heights = float2(min(heights.x, tile.x), max(heights.y, tile.y));
		}
	}
	const float2 altitude = lerp(info.lowCloudBaseAltitude, info.lowCloudTopAltitude, heights);
	const float3 relativeEye = eye - float3(FrameBuffer::CameraPosAdjust.xy, 0);
	const CloudRaySegments segments = GetCloudRaySegments(relativeEye, direction,
		altitude.x - GAME_UNITS_PER_METER, altitude.y + GAME_UNITS_PER_METER, info);
	const float2 nearInterval = float2(max(begin, segments.nearSegment.x), min(blockEnd, segments.nearSegment.y));
	if (nearInterval.y > nearInterval.x)
		return nearInterval;
	const float2 farInterval = float2(max(begin, segments.farSegment.x), min(blockEnd, segments.farSegment.y));
	return farInterval.y > farInterval.x ? farInterval : float2(blockEnd, blockEnd);
}

float2 ClipCloudBoundary(float2 segment, float4 boundary)
{
	if (boundary.w <= 0.0 || segment.y <= segment.x)
		return segment;
	const bool inside = boundary.z > 0.0 && (boundary.x == 0.0 || boundary.z < boundary.x);
	if (!inside && boundary.x > 0.0)
		segment.x = max(segment.x, min(boundary.x, boundary.w));
	// An open grid does not bound the part of the ray beyond its XY footprint.
	if (boundary.y > 0.0 && segment.y <= boundary.w)
		segment.y = min(segment.y, boundary.y);
	segment.y = max(segment.x, segment.y);
	return segment;
}

VolumetricCloudResult RenderVolumetricCloudRay(float3 direction, float3 eye, float sceneDistance, float2 jitter, float apShadow, float4 boundary, float angularFootprint, float2 raySegment = float2(0.0, 1.0))
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	VolumetricCloudResult result;
	result.lightning = 0.0;
	result.transmittance = 1.0;
	result.lum = 0.0;
#ifdef CLOUD_REFLECTION
	result.transportLum = 0.0;
	result.rawTransmission = 1.0;
	result.integralWeights = 0.0;
	const float stopTransmission = 0.0001;
#else
	const float stopTransmission = 0.1;
#endif
	result.motionMetadata = 0.0;
	result.stormMotion = 0.0;
	bool sceneLimited = sceneDistance > 0.0;
	const float3 planetEye = eye + float3(-FrameBuffer::CameraPosAdjust.xy, info.planetRadius);
	const float2 ground = IntersectSpherePair(planetEye, direction, info.planetRadius);
	if (dot(planetEye, direction) < 0.0 && ground.y > 0.0) {
		sceneDistance = sceneLimited ? min(sceneDistance, max(ground.x, 0.0)) : max(ground.x, 0.0);
		sceneLimited = true;
	}
	const bool localRay = LocalNdfIntersectsRay(eye.xy, direction.xy, info);
	const float traceBottom = localRay ? info.lowCloudTraceBottomAltitude : info.lowCloudBaseAltitude;
	const float traceTop = localRay ? info.lowCloudTraceTopAltitude : info.lowCloudTopAltitude;
	const float3 cloudEye = float3(eye.xy - FrameBuffer::CameraPosAdjust.xy, eye.z);
	const CloudRaySegments bounds = GetCloudRaySegments(cloudEye, direction, traceBottom, traceTop, info);
	CloudRaySegments low = bounds;
	float2 stormInterval = CloudStormRayInterval(eye, direction);
	const float2 voxelInterval = CloudVoxelInterval(eye, direction, 0.0, false);
	const bool voxelRay = voxelInterval.y > voxelInterval.x;
	if (voxelRay)
		stormInterval = stormInterval.y > stormInterval.x ? float2(min(stormInterval.x, voxelInterval.x), max(stormInterval.y, voxelInterval.y)) : voxelInterval;
	const bool stormRay = stormInterval.y > stormInterval.x;
	if (angularFootprint > 0.0) {
		const float2 heights = TexCloudHeightBounds[uint2(0, 0)];
		float2 altitude = lerp(info.lowCloudBaseAltitude, info.lowCloudTopAltitude, heights);
		if (localRay && info.localNdfAltitude.y > 0.0) {
			altitude.x = min(altitude.x, info.localNdfAltitude.x * GAME_UNITS_PER_METER);
			altitude.y = max(altitude.y, (info.localNdfAltitude.x + info.localNdfAltitude.y) * GAME_UNITS_PER_METER);
		}
		// Enclose bilinear NDF samples.
		const float padding = GAME_UNITS_PER_METER;
		low = GetCloudRaySegments(cloudEye, direction, max(traceBottom, altitude.x - padding),
			min(traceTop, altitude.y + padding), info);
		const float end = bounds.farSegment.y > bounds.farSegment.x ? bounds.farSegment.y : bounds.nearSegment.y;
		low.nearSegment.y = max(low.nearSegment.x, min(low.nearSegment.y, end));
		low.farSegment.y = max(low.farSegment.x, min(low.farSegment.y, end));
	}
	low.nearSegment = ClipCloudBoundary(low.nearSegment, boundary);
	low.farSegment = ClipCloudBoundary(low.farSegment, boundary);
	const CloudRaySegments weatherSegments = low;
#ifdef CLOUD_MAIN_DISPATCH
	CloudRaySegments weatherCandidate = bounds;
	// Local modeling may reach this ray through the upstream/shear samples.
	// Its separate field is not included in the weather-map reduction.
	if (info.localNdfAltitude.z <= 0.0) {
		const float2 altitude = lerp(info.lowCloudBaseAltitude, info.lowCloudTopAltitude, TexCloudWeatherBounds[uint2(0, 0)]);
		weatherCandidate = GetCloudRaySegments(cloudEye, direction, max(traceBottom, altitude.x - GAME_UNITS_PER_METER),
			min(traceTop, altitude.y + GAME_UNITS_PER_METER), info);
	}
#endif
	if (stormRay) {
		// The world-anchored storm is not represented by the curved weather boundary mesh.
		float start = stormInterval.x;
		if (low.nearSegment.y > low.nearSegment.x)
			start = min(start, low.nearSegment.x);
		if (low.farSegment.y > low.farSegment.x)
			start = min(start, low.farSegment.x);
		const float end = max(stormInterval.y, max(low.nearSegment.y, low.farSegment.y));
		low.nearSegment = float2(start, end);
		low.farSegment = 0.0;
	}
	if (sceneLimited) {
		low.nearSegment.y = max(low.nearSegment.x, min(low.nearSegment.y, sceneDistance));
		low.farSegment.y = max(low.farSegment.x, min(low.farSegment.y, sceneDistance));
	}
#ifdef CLOUD_REFLECTION
	// Allocate reflection samples over occupied ray intervals. The logarithmic
	// coordinate follows the distance-based march spacing without serial steps.
	const float offset = (3.0 / (60.0 / 16384.0)) * GAME_UNITS_PER_METER;
	const float4 arc = log2(1.0 + max(float4(low.nearSegment, low.farSegment), 0.0) / offset);
	const float nearArc = max(arc.y - arc.x, 0.0);
	const float farArc = max(arc.w - arc.z, 0.0);
	const float2 coordinate = raySegment * (nearArc + farArc);
	const float2 mapped = (exp2(coordinate <= nearArc ? arc.xx + coordinate : arc.zz + coordinate - nearArc) - 1.0) * offset;
	// Include a cirrus sheet outside the finite low-cloud intervals in the first
	// or last segment, without integrating an infinite volume step.
	raySegment = float2(raySegment.x == 0.0 ? 0.0 : mapped.x, raySegment.y == 1.0 ? 1e30 : mapped.y);
	low.nearSegment = float2(max(low.nearSegment.x, raySegment.x), min(low.nearSegment.y, raySegment.y));
	low.farSegment = float2(max(low.farSegment.x, raySegment.x), min(low.farSegment.y, raySegment.y));
	low.nearSegment.y = max(low.nearSegment.y, low.nearSegment.x);
	low.farSegment.y = max(low.farSegment.y, low.farSegment.x);
#endif
	low.nearLength = low.nearSegment.y - low.nearSegment.x;
	low.length = low.nearLength + (low.farSegment.y - low.farSegment.x);
	result.cloud_depth = info.rayMarchRange;
	if (bounds.length > 0.0)
		result.cloud_depth = bounds.nearSegment.x > 50.0 * GAME_UNITS_PER_METER ? bounds.nearSegment.x : bounds.nearSegment.y;
	const float boundaryDepth = boundary.x > 0.0 && boundary.z > 0.0 ? min(boundary.x, boundary.z) : max(boundary.x, boundary.z);
	const bool hasBoundaryDepth = boundary.w > 0.0 && boundaryDepth > 0.0;
	if (hasBoundaryDepth)
		result.cloud_depth = boundaryDepth;
	if (stormRay)
		result.cloud_depth = min(result.cloud_depth, stormInterval.x);
	float cirrusDistance = sceneDistance;
	bool cirrusPending = false;

#if defined(CLOUD_MAIN_DISPATCH) && defined(NO_CLOUD_STORMS) && !defined(CLOUD_VOXELS)
	// No weather support intersects this ray. Keep the boundary/depth metadata
	// above, but avoid walking the empty shell and setting up its lighting.
	if (info.localNdfAltitude.z <= 0.0 && weatherCandidate.length <= 0.0)
		return result;
#endif
	if (low.length <= 0.0 && !cirrusPending)
		return result;

	// Split the volume at the sheet so camera altitude and NDF bounds cannot reverse compositing order.
	float4 boundaries = float4(low.nearSegment, low.farSegment);
	boundaries = float4(min(boundaries.x, boundaries.y), max(boundaries.x, boundaries.y),
		min(boundaries.z, boundaries.w), max(boundaries.z, boundaries.w));
	boundaries = float4(min(boundaries.x, boundaries.z), min(boundaries.y, boundaries.w),
		max(boundaries.x, boundaries.z), max(boundaries.y, boundaries.w));
	boundaries.yz = float2(min(boundaries.y, boundaries.z), max(boundaries.y, boundaries.z));
	const float sheetBoundary = cirrusPending ? cirrusDistance : 0.0;
	const float lastBoundary = max(boundaries.w, sheetBoundary);
	boundaries = min(boundaries, float4(sheetBoundary, max(boundaries.xyz, sheetBoundary)));
	const float lightCosine = dot(direction, info.dirlightDir);
	const float phase = CloudLowPhase(lightCosine);
	const float cirrusPhase = cirrusPending ? CloudCirrusPhase(lightCosine) : 0.0;
	const CloudAmbient ambient = MakeCloudAmbient();
	const bool ambientEnabled = info.ambientStrength > 0.0 && (info.msAlbedo > 0.0 || CloudStormCount() > 0u);
	float ambientTransmittance = 1.0;
	const CloudLayer layer = GetCloudLayer(info);
	float noiseTexelsPerMeter = 0.0;
	float maximumNoiseMip = 0.0;
	if (angularFootprint > 0.0) {
		uint width, height, depth, levels;
		TexCloudShapeNoise.GetDimensions(0, width, height, depth, levels);
		noiseTexelsPerMeter = max(max(width, height) * 0.0043545123, depth * 0.0034834063);
		maximumNoiseMip = float(levels - 1u);
	}
	float depthSum = 0.0;
	float weightSum = 0.0;
	float cirrusWeight = 0.0;
	float heightSum = 0.0;
	float transmittance = 1.0;
#if defined(CLOUD_MAIN_DISPATCH) && !defined(CLOUD_VOXELS)
	float lightDepth = 0.0;
	float lightDepthEnd = -1.0;
	uint lightMaterial = 0xffffffffu;
#endif
	[loop] for (uint interval = 0u; interval < 4u && max(transmittance, ambientEnabled ? ambientTransmittance : 0.0) > stopTransmission; ++interval)
	{
		const float begin = interval == 0u ? boundaries.x : (interval == 1u ? boundaries.y : (interval == 2u ? boundaries.z : boundaries.w));
		const float end = interval == 0u ? boundaries.y : (interval == 1u ? boundaries.z : (interval == 2u ? boundaries.w : lastBoundary));
		if (cirrusPending && cirrusDistance <= begin) {
#ifdef CLOUD_REFLECTION
			IntegrateReflectionCirrus(eye + direction * cirrusDistance, direction, cirrusDistance, cirrusPhase, ambient,
				result, transmittance, ambientTransmittance, depthSum, weightSum, cirrusWeight);
#else
			IntegrateCirrus(eye + direction * cirrusDistance, direction, cirrusDistance, cirrusPhase, ambient,
				result.lum, transmittance, ambientTransmittance, depthSum, weightSum, cirrusWeight);
#endif
			cirrusPending = false;
		}
		if (end <= begin || !CloudIntervalContains(low, 0.5 * (begin + end)))
			continue;
		float distance = begin;
		float reflectionEnd = begin;
		float2 stormPiece = 0.0;
		float2 voxelPiece = 0.0;
		[loop] while (distance < end && max(transmittance, ambientEnabled ? ambientTransmittance : 0.0) > stopTransmission)
		{
#ifdef CLOUD_REFLECTION
			// A stratum represents its complete finite interval. Use the same
			// extinction and material blend as the view, with footprint-filtered
			// noise and the coarse voxel field supplied by CLOUD_VOXEL_COARSE.
			const float step = end - distance;
			const float sampleDistance = distance + jitter.x * step;
			const float3 pos = eye + direction * sampleDistance;
			const float viewDistance = sampleDistance * GAME_UNIT_TO_M;
			const float minimumMip = min(log2(max(viewDistance * angularFootprint * noiseTexelsPerMeter, 1.0)), maximumNoiseMip);
			CloudDensityContext densityContext;
			const float extinction = sampleCloudDensity(pos, layer, 0.0, viewDistance, minimumMip, densityContext);
			const float depthDistance = sampleDistance;
			distance = end;
#else
#	ifdef CLOUD_MAIN_DISPATCH
			if (!voxelRay && !CloudIntervalContains(weatherCandidate, distance) &&
				!(stormRay && distance >= stormInterval.x && distance < stormInterval.y)) {
				float limit = end;
				if (weatherCandidate.nearSegment.y > weatherCandidate.nearSegment.x && weatherCandidate.nearSegment.x > distance)
					limit = min(limit, weatherCandidate.nearSegment.x);
				if (weatherCandidate.farSegment.y > weatherCandidate.farSegment.x && weatherCandidate.farSegment.x > distance)
					limit = min(limit, weatherCandidate.farSegment.x);
				if (stormRay && stormInterval.x > distance)
					limit = min(limit, stormInterval.x);
				// Preserve the original sample sequence. Advancing through proven
				// empty space needs no density, noise, motion or lighting evaluation.
				float emptyStep = min(CloudViewStep(distance, angularFootprint), end - distance);
				[loop] while (distance + emptyStep * 2.0 < limit)
				{
					distance += emptyStep * 2.0;
					emptyStep = min(CloudViewStep(distance, angularFootprint), end - distance);
				}
			}
#	endif
			if (voxelRay && distance >= voxelPiece.y)
				voxelPiece = CloudVoxelInterval(eye, direction, distance, true);
			if (stormRay && !CloudIntervalContains(weatherSegments, distance)) {
				if (distance >= stormPiece.y)
					stormPiece = CloudStormNextInterval(eye, direction, distance);
				if (!(distance >= stormPiece.x && distance < stormPiece.y) && !(distance >= voxelPiece.x && distance < voxelPiece.y)) {
					float next = end;
					if (weatherSegments.nearSegment.y > weatherSegments.nearSegment.x && weatherSegments.nearSegment.x > distance)
						next = min(next, weatherSegments.nearSegment.x);
					if (weatherSegments.farSegment.y > weatherSegments.farSegment.x && weatherSegments.farSegment.x > distance)
						next = min(next, weatherSegments.farSegment.x);
					if (stormPiece.x > distance)
						next = min(next, stormPiece.x);
#	ifdef CLOUD_VOXELS
					if (voxelPiece.x > distance)
						next = min(next, voxelPiece.x);
#	endif
					distance = next;
					if (distance >= end)
						break;
				}
			}
			if (angularFootprint > 0.0 && !localRay && !stormRay && distance >= reflectionEnd) {
				const float2 reflectionInterval = CloudReflectionInterval(eye, direction, distance, end);
				distance = reflectionInterval.x;
				reflectionEnd = reflectionInterval.y;
				if (reflectionEnd <= distance)
					continue;
			}
			const float marchEnd = angularFootprint > 0.0 && !localRay && !stormRay ? reflectionEnd : end;
			float step = min(CloudViewStep(distance, angularFootprint), marchEnd - distance);
#	ifdef CLOUD_VOXELS
			const bool inVoxel = distance >= voxelPiece.x && distance < voxelPiece.y;
			uint2 occupiedVoxels = 0u;
			const bool ndfOccupied = CloudIntervalContains(weatherSegments, distance) || (distance >= stormPiece.x && distance < stormPiece.y);
			if (inVoxel) {
				const float voxelStep = CloudVoxelStep(eye + direction * distance, distance, angularFootprint, occupiedVoxels);
				step = min(ndfOccupied ? min(step, voxelStep) : voxelStep, min(marchEnd, voxelPiece.y) - distance);
				// A positive voxel SDF says nothing about weather or storm density.
				// End an empty-space jump before entering either of those media.
				if (!ndfOccupied) {
					if (weatherSegments.nearSegment.y > weatherSegments.nearSegment.x && weatherSegments.nearSegment.x > distance)
						step = min(step, weatherSegments.nearSegment.x - distance);
					if (weatherSegments.farSegment.y > weatherSegments.farSegment.x && weatherSegments.farSegment.x > distance)
						step = min(step, weatherSegments.farSegment.x - distance);
					if (stormPiece.x > distance)
						step = min(step, stormPiece.x - distance);
				}
			} else if (voxelPiece.x > distance)
				step = min(step, voxelPiece.x - distance);
#	endif
			const float sampleDistance = distance + (distance * GAME_UNIT_TO_M < 250.0 ? jitter.x : jitter.y) * step;
			const float3 pos = eye + direction * sampleDistance;
			CloudDensityContext densityContext = (CloudDensityContext)0;
			const float viewDistance = distance * GAME_UNIT_TO_M;
			const float minimumMip = min(log2(max(viewDistance * angularFootprint * noiseTexelsPerMeter, 1.0)), maximumNoiseMip);
#	ifdef CLOUD_VOXELS
			// Keep NDF out of voxel-only segments; the original voxel marcher has
			// no weather-density or weather-lighting probes in its inner loop.
			float extinction = 0.0;
			[branch] if (ndfOccupied)
				extinction = sampleNdfCloudDensity(pos, layer, 0.0, viewDistance, minimumMip, densityContext);
			if (any(occupiedVoxels != 0u))
				extinction = AddVoxelDensity(pos, 0.0, viewDistance, 0.0, occupiedVoxels, extinction, densityContext);
#	else
			const float extinction = sampleCloudDensity(pos, layer, 0.0, viewDistance, minimumMip, densityContext);
#	endif
			const float depthDistance = distance;
			distance += step * (densityContext.ndf.dimension_profile > 0.05 || voxelRay ? 1.0 : 2.0);
			if (angularFootprint > 0.0 && !localRay && !stormRay)
				distance = min(distance, marchEnd);
#endif
			if (extinction <= 0.0)
				continue;
			const float viewDepth = extinction * info.viewExtinction * step * GAME_UNIT_TO_M;
			const float stepTransmittance = exp(-viewDepth);
			const float opacity = viewDepth < 0.001 ? CloudOpacity(viewDepth) : 1.0 - stepTransmittance;
			if (Lightning::HasAnySource())
				result.lightning += transmittance * opacity * stepTransmittance *
				                    Lightning::CloudMask(pos + float3(0, 0, info.bottomZ), densityContext.ndf.height_fraction, densityContext.ndf.dimension_profile);
			if (transmittance > stopTransmission) {
#if defined(CLOUD_MAIN_DISPATCH) && !defined(CLOUD_VOXELS)
				// The light cone spans 120--240 m. Share its integral over nearby
				// view samples; density, erosion and scattering remain per-sample.
				const uint material = densityContext.ndf.stormVolume ? densityContext.ndf.stormIndex + 1u : 0u;
				if (viewDistance >= lightDepthEnd || material != lightMaterial) {
					lightDepth = CloudLightOpticalDepth(pos, direction, densityContext.ndf, extinction, viewDistance, minimumMip);
					lightDepthEnd = viewDistance + min(4.0 * step * GAME_UNIT_TO_M, 64.0);
					lightMaterial = material;
				}
				const float3 source = ShadeCloudLighting(pos, densityContext.ndf, extinction, viewDepth, opacity, phase, lightDepth);
#else
				const float3 source = CloudLighting(pos, direction, densityContext.ndf, extinction, viewDepth, opacity,
					viewDistance, minimumMip, phase);
#endif
				result.lum += transmittance * source;
				if (densityContext.ndf.stormVolume) {
					const float height = StormHeight(pos.z * GAME_UNIT_TO_M, CloudStorms[densityContext.ndf.stormIndex]);
					const float glow = exp(-height / CloudStorms[densityContext.ndf.stormIndex].emission.w) * saturate(densityContext.ndf.coverage);
					result.lum += transmittance * opacity * CloudStorms[densityContext.ndf.stormIndex].emission.rgb * glow * densityContext.ndf.stormBlend;
				}
			}
			[branch] if (ambientEnabled)
			{
				const float2 ambientPhase = CloudMaterialAmbient(densityContext.ndf);
				float transport = ambientPhase.x;
#ifdef CLOUD_VOXELS
				// NVDF opacity is the native density integral, not the NDF forward
				// transport integral. Otherwise details become translucent and the
				// marcher continues through already opaque voxel interiors.
				transport = lerp(transport, 1.0, densityContext.ndf.voxelBlend);
#endif
				const float reducedDepth = transport * viewDepth;
				const float reducedOpacity = CloudOpacity(reducedDepth);
#ifdef CLOUD_REFLECTION
				result.transportLum += ambientTransmittance * reducedOpacity * ambientPhase.y * info.ambientStrength * info.cloudLightingScale *
#else
				result.lum += ambientTransmittance * reducedOpacity * ambientPhase.y * info.ambientStrength * info.cloudLightingScale *
#endif
				                       CloudAmbientTransport::ViewSource(pos);
				ambientTransmittance *= exp(-reducedDepth);
			}

			const float depthWeight = CloudDepthWeight(transmittance, extinction, depthDistance);
			depthSum += depthWeight * depthDistance;
			weightSum += depthWeight;
			if (densityContext.ndf.stormVolume) {
				result.stormMotion.xyz += depthWeight * densityContext.storm_displacement;
				result.stormMotion.w += depthWeight * densityContext.ndf.stormBlend;
			}
#ifdef CLOUD_VOXELS
			// The authored field is stationary. Only its detail noise follows wind;
			// do not apply the weather's height shear to this local volume.
			result.stormMotion.xy += depthWeight * densityContext.ndf.voxelBlend * info.cloudWindDelta;
			result.stormMotion.w += depthWeight * densityContext.ndf.voxelBlend;
#endif
#if defined(NO_CLOUD_STORMS) && !defined(CLOUD_VOXELS)
			heightSum += depthWeight * densityContext.ndf.height_fraction;
#else
			float weatherWeight = 1.0 - densityContext.ndf.stormBlend;
#	ifdef CLOUD_VOXELS
			weatherWeight -= densityContext.ndf.voxelBlend;
#	endif
			heightSum += depthWeight * weatherWeight * densityContext.weather_height_fraction;
#endif
			transmittance *= stepTransmittance;
		}
	}
	if (cirrusPending) {
#ifdef CLOUD_REFLECTION
		IntegrateReflectionCirrus(eye + direction * cirrusDistance, direction, cirrusDistance, cirrusPhase, ambient,
			result, transmittance, ambientTransmittance, depthSum, weightSum, cirrusWeight);
#else
		IntegrateCirrus(eye + direction * cirrusDistance, direction, cirrusDistance, cirrusPhase, ambient,
			result.lum, transmittance, ambientTransmittance, depthSum, weightSum, cirrusWeight);
#endif
	}
#ifdef CLOUD_REFLECTION
	result.rawTransmission = float2(transmittance, ambientEnabled ? ambientTransmittance : transmittance);
	result.integralWeights = float4(depthSum, weightSum, cirrusWeight, heightSum);
#else
	transmittance = CloudBackgroundTransmittance(transmittance, ambientEnabled ? ambientTransmittance : transmittance);
	result.transmittance = saturate((transmittance - 0.1) / 0.9);
	// Keep radiance premultiplied by the remapped opacity.
	result.lum /= 1.0 - min(transmittance, 0.1);
	result.lightning /= 1.0 - min(transmittance, 0.1);
	if (weightSum > 0.0) {
		result.cloud_depth = depthSum / weightSum;
		result.stormMotion *= (1.0 - result.transmittance.x) / weightSum;
		result.motionMetadata = float2(saturate(cirrusWeight / weightSum), saturate(heightSum / weightSum));
#	ifdef CLOUD_AP_SHADOW
		if (!ViewAtmosphere::Enabled()) {
			// Foreground air must not inherit visibility averaged through the scene behind the cloud.
			if (SharedData::physSkyData.apLumMix > 0.0)
				apShadow = AtmosphereShadow::TraceShadow(direction, result.cloud_depth, jitter.x);
#	endif
			const float4 ap = SampleCloudAerialPerspective(direction, result.cloud_depth, apShadow);
			result.lum = result.lum * ap.a + ap.rgb * (1.0 - result.transmittance);
			result.lightning *= ap.a;
#	ifdef CLOUD_AP_SHADOW
		}
#	endif
	}
#endif
	return result;
}

// clang-format off
#include "PhysicalSky/UpperCloud.hlsli"
#include "PhysicalSky/CloudBlur.hlsli"
#include "PhysicalSky/CloudBounds.hlsli"
#include "PhysicalSky/CloudTemporal.hlsli"
#include "PhysicalSky/CloudVoxelView.hlsli"
#include "PhysicalSky/LightningView.hlsli"
// clang-format on

// Match the largest shadow-volume dimension (kShadowVolW/H in PhysicalSky.h).
#define NTHREADS 256
groupshared float g_density[2 * NTHREADS];
groupshared uint g_density_segment[NTHREADS];

// Accumulate the low-cloud extinction column along the light direction into the
// camera-centred shadow volume. Each thread group walks one light ray through the
// volume with a parallel prefix sum.
[numthreads(NTHREADS, 1, 1)] void renderShadowVolume(const uint gtid : SV_GroupThreadID, const uint2 gid : SV_GroupID) {
	const VolumetricCloudData info = VolumetricCloudBuffer[0];

	uint3 dims;
	RWShadowVolume.GetDimensions(dims.x, dims.y, dims.z);
	const float3 rcp_dims = 1.0 / float3(dims);
	const float shadow_thickness = max(info.shadowVolumeTop - info.shadowVolumeBottom, 1.0);
	const float3 scale = float3(info.shadowVolumeRange.xx, shadow_thickness);
	const float3 rcp_scale = 1.0 / scale;

	const float3 ray_dir = -info.dirlightDir;  // from sun

	float3 ray_px_increment = ray_dir * rcp_scale * dims;
	const float dir_max_component = max(max(abs(ray_px_increment.x), abs(ray_px_increment.y)), abs(ray_px_increment.z));

	uint3 start_px;
	bool3 component_mask = false;
	uint ray_step = gtid;
	uint ray_steps = NTHREADS;
	uint column = 0;
	if (abs(ray_px_increment.x) == dir_max_component) {
		start_px = uint3(ray_px_increment.x > 0 ? 0 : dims.x - 1, gid);
		component_mask.x = true;
	} else if (abs(ray_px_increment.y) == dir_max_component) {
		start_px = uint3(gid.x, ray_px_increment.y > 0 ? 0 : dims.y - 1, gid.y);
		component_mask.y = true;
	} else {
		// Pack four 64-voxel columns into the 256-lane group instead of
		// launching 192 inactive lanes for every vertical column.
		ray_steps = dims.z;
		column = gtid / ray_steps;
		ray_step = gtid % ray_steps;
		start_px = uint3(gid.x * (NTHREADS / ray_steps) + column, gid.y, ray_px_increment.z > 0 ? 0 : dims.z - 1);
		component_mask.z = true;
	}
	ray_px_increment /= dir_max_component;
	const float3 ray_uv_increment = ray_px_increment * rcp_dims;
	const float3 start_uv = (start_px + 0.5) * rcp_dims;
	const float3 raw_thread_uv = start_uv + ray_step * ray_uv_increment;

	const bool is_valid_x = component_mask.x && raw_thread_uv.x > 0 && raw_thread_uv.x < 1;
	const bool is_valid_y = component_mask.y && raw_thread_uv.y > 0 && raw_thread_uv.y < 1;
	const bool is_valid_z = component_mask.z && raw_thread_uv.z > 0 && raw_thread_uv.z < 1;
	const bool is_valid = is_valid_x || is_valid_y || is_valid_z;

	const float3 thread_uv = raw_thread_uv - floor(raw_thread_uv);  // wraparound
	const uint3 thread_px_coord = thread_uv * dims;

	float density = 0.0;
	float localExtinction = 0.0;
	float accumulated_density = 0.0;
	if (is_valid) {
		const float2 center = CloudShadowVolume::GridCenter(FrameBuffer::CameraPosAdjust.xy, info.shadowVolumeRange, dims.xy);
		const float3 pos = float3(center + (thread_uv.xy - 0.5) * info.shadowVolumeRange, info.shadowVolumeBottom + shadow_thickness * thread_uv.z);

		// Fetch only density represented inside this finite shadow volume. Prefixes
		// start with zero extinction at the boundary instead of assuming a uniform
		// cloud shell outside the represented domain.
		CloudDensityContext densityContext;
		localExtinction = sampleCloudDensity(pos, GetCloudLayer(info), 2.0, 2000.0, 0.0, densityContext);
		float4 middleModel;
		float2 middleDepth;
		localExtinction += MiddleCloudDensity(pos, middleModel, middleDepth, info.shadowVolumeRange / min(dims.x, dims.y));
		density = localExtinction * length(ray_uv_increment * scale);

		accumulated_density = density;
	}
	// At 256 x 256 x 64 the ray wraps at most four times per axis.
	// Pack the exact segment identity into one shared word instead of int4.
	const uint3 wrap = asuint(int3(floor(raw_thread_uv))) & 255u;
	const uint segment = wrap.x | (wrap.y << 8u) | (wrap.z << 16u) | (column << 24u);
	g_density_segment[gtid] = segment;

	// Ping-pong scan storage lets each step publish its input with one barrier.
	// The next step writes the other bank, so it cannot overwrite pending reads.
	uint read_base = 0;
	[unroll] for (uint offset = 1; offset < NTHREADS; offset <<= 1)
	{
		// Uniform across the group; vertical columns need only six scan steps.
		if (offset >= ray_steps)
			continue;
		g_density[read_base + gtid] = accumulated_density;
		GroupMemoryBarrierWithGroupSync();
		if (is_valid && gtid >= offset) {
			if (g_density_segment[gtid - offset] == segment)  // no wraparound happened
			{
				accumulated_density += g_density[read_base + gtid - offset];
			}
		}
		read_base = NTHREADS - read_base;
	}

	// save
	if (is_valid) {
		// Every voxel is rebuilt deterministically. The camera-centred grid moves,
		// so blending the same index from the previous frame would trail shadows.
		RWShadowVolume[thread_px_coord] = exp(-max(accumulated_density - 0.5 * density, 0.0) * info.sunExtinction * GAME_UNIT_TO_M);
	}
}

	// Each lane integrates one light ray; neighbouring lanes sample neighbouring
	// columns. Keep the slanted sampling grid, wrap resets and half-voxel integral,
	// without a group-wide prefix scan or shared-memory barriers at every voxel.
	[numthreads(8, 8, 1)] void renderShadowColumns(uint2 ray : SV_DispatchThreadID)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	uint3 dims;
	RWShadowVolume.GetDimensions(dims.x, dims.y, dims.z);
	const float3 rcpDims = 1.0 / float3(dims);
	const float thickness = max(info.shadowVolumeTop - info.shadowVolumeBottom, 1.0);
	const float3 scale = float3(info.shadowVolumeRange.xx, thickness);
	float3 increment = -info.dirlightDir * (1.0 / scale) * dims;
	const float maximum = max(max(abs(increment.x), abs(increment.y)), abs(increment.z));
	const uint axis = abs(increment.x) == maximum ? 0u : (abs(increment.y) == maximum ? 1u : 2u);
	const uint2 columns = axis == 0u ? dims.yz : (axis == 1u ? dims.xz : dims.xy);
	if (any(ray >= columns))
		return;
	uint3 start;
	if (axis == 0u)
		start = uint3(increment.x > 0.0 ? 0u : dims.x - 1u, ray);
	else if (axis == 1u)
		start = uint3(ray.x, increment.y > 0.0 ? 0u : dims.y - 1u, ray.y);
	else
		start = uint3(ray, increment.z > 0.0 ? 0u : dims.z - 1u);
	increment /= maximum;
	const float3 uvIncrement = increment * rcpDims;
	const float3 startUv = (start + 0.5) * rcpDims;
	const float stepLength = length(uvIncrement * scale);
	const float2 center = CloudShadowVolume::GridCenter(FrameBuffer::CameraPosAdjust.xy, info.shadowVolumeRange, dims.xy);
	const CloudLayer layer = GetCloudLayer(info);
	float accumulated = 0.0;
	uint previousSegment = 0u;
	[loop] for (uint step = 0u; step < dims[axis]; ++step)
	{
		const float3 rawUv = startUv + step * uvIncrement;
		const float3 uv = rawUv - floor(rawUv);
		const uint3 pixel = uv * dims;
		const uint3 wrap = asuint(int3(floor(rawUv))) & 255u;
		const uint segment = wrap.x | (wrap.y << 8u) | (wrap.z << 16u);
		// Crossing a side of the finite volume starts a new light column.
		if (step == 0u || segment != previousSegment)
			accumulated = 0.0;
		previousSegment = segment;
		const float3 position = float3(center + (uv.xy - 0.5) * info.shadowVolumeRange, info.shadowVolumeBottom + thickness * uv.z);
		CloudDensityContext densityContext;
		float extinction = sampleCloudDensity(position, layer, 2.0, 2000.0, 0.0, densityContext);
		float4 middleModel;
		float2 middleDepth;
		extinction += MiddleCloudDensity(position, middleModel, middleDepth, info.shadowVolumeRange / min(dims.x, dims.y));
		const float density = extinction * stepLength;
		accumulated += density;
		RWShadowVolume[pixel] = exp(-max(accumulated - 0.5 * density, 0.0) * info.sunExtinction * GAME_UNIT_TO_M);
	}
}

[numthreads(8, 8, 4)] void resampleShadowVolume(uint3 pixel : SV_DispatchThreadID) {
	uint3 dims;
	RWShadowVolume.GetDimensions(dims.x, dims.y, dims.z);
	if (any(pixel >= dims))
		return;
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	const float3 scale = float3(info.shadowVolumeRange.xx, max(info.shadowVolumeTop - info.shadowVolumeBottom, 1.0));
	float3 increment = -info.dirlightDir * rcp(scale) * float3(dims);
	const float maximum = max(max(abs(increment.x), abs(increment.y)), abs(increment.z));
	const uint axis = abs(increment.x) == maximum ? 0u : (abs(increment.y) == maximum ? 1u : 2u);
	increment /= maximum;
	const float rayStep = increment[axis] > 0.0 ? float(pixel[axis]) : float(dims[axis] - 1u - pixel[axis]);
	float3 offset = frac(0.5 + rayStep * increment) - 0.5;
	offset = float3(axis == 0u ? 0.0 : offset.x, axis == 1u ? 0.0 : offset.y, axis == 2u ? 0.0 : offset.z);
	RWShadowVolume[pixel] = TexShadowVolume.SampleLevel(TransmittanceSampler, (float3(pixel) + 0.5 - offset) / float3(dims), 0);
}

RWTexture2D<float4> RWCloudShadowMean : register(u0);
groupshared float ShadowMeanSamples[64];

// One 8x8 spatial quadrature per height. Reuse complete local prefixes; where
// the finite box truncates a ray, integrate the actual curved cloud layer.
// Averaging exp(-tau), rather than tau, preserves clear gaps between clouds.
[numthreads(64, 1, 1)] void buildCloudShadowMean(uint lane : SV_GroupIndex, uint3 group : SV_GroupID) {
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	uint width, height;
	RWCloudShadowMean.GetDimensions(width, height);
	uint3 dims;
	TexShadowVolume.GetDimensions(dims.x, dims.y, dims.z);
	const float2 center = CloudShadowVolume::GridCenter(FrameBuffer::CameraPosAdjust.xy, info.shadowVolumeRange, dims.xy);
	const float2 uv = (float2(lane % 8u, lane / 8u) + 0.5) / 8.0;
	const float altitude = lerp(info.shadowVolumeBottom, info.shadowVolumeTop, float(group.y) / (height - 2u));
	float3 pos = float3(center + (uv - 0.5) * info.shadowVolumeRange, altitude);
	pos.z -= CloudCurvatureOffset(pos.xy - FrameBuffer::CameraPosAdjust.xy, info.cloudCurvatureRadius);
	const float3 origin = float3(pos.xy - FrameBuffer::CameraPosAdjust.xy, pos.z);
	const float3 direction = info.dirlightDir;
	const float2 outer = IntersectCloudBelow(origin, direction, info.shadowVolumeTop, info.cloudCurvatureRadius);
	const float2 inner = IntersectCloudBelow(origin, direction, info.shadowVolumeBottom, info.cloudCurvatureRadius);
	const float start = max(outer.x, 0.0);
	const float nearEnd = min(outer.y, max(inner.x, 0.0));
	const float farStart = max(start, inner.y);
	const float nearLength = max(nearEnd - start, 0.0);
	const float farLength = max(outer.y - farStart, 0.0);
	const float pathLength = nearLength + farLength;
	float transmittance = 1.0;
	if (pathLength > 0.0) {
		const float3 exitPosition = pos + direction * (farLength > 0.0 ? outer.y : nearEnd);
		const float2 exitUv = (exitPosition.xy - center) / info.shadowVolumeRange + 0.5;
		const float z = (pos.z - info.shadowVolumeBottom) / max(info.shadowVolumeTop - info.shadowVolumeBottom, 1.0);
		// Keep the linear-filter footprint off incomplete side prefixes too.
		const float2 margin = 2.0 / float2(dims.xy);
		if (z >= 0.0 && z <= 1.0 && exitPosition.z >= info.shadowVolumeBottom && exitPosition.z <= info.shadowVolumeTop + 1.0 && all(exitUv >= margin) && all(exitUv <= 1.0 - margin)) {
			transmittance = TexShadowVolume.SampleLevel(TransmittanceSampler, float3(uv, z), 0);
		} else {
			const float stepLength = pathLength / 32.0;
			float opticalDepth = 0.0;
			// FXC's loop analysis is expensive for storm/voxel density bodies.
			// fastopt preserves their DXBC while avoiding speculative loop work.
#ifdef NO_CLOUD_STORMS
			[loop]
#else
			[fastopt]
#endif
				for (uint step = 0u; step < 32u; ++step)
			{
				const float distanceInCloud = (step + 0.5) * stepLength;
				const float distanceAlongRay = distanceInCloud < nearLength ? start + distanceInCloud : farStart + distanceInCloud - nearLength;
				CloudDensityContext densityContext;
				float density = sampleCloudDensity(pos + direction * distanceAlongRay, GetCloudLayer(info), 2.0, 2000.0, 0.0, densityContext);
				float4 middleModel;
				float2 middleDepth;
				density += MiddleCloudDensity(pos + direction * distanceAlongRay, middleModel, middleDepth,
					max(stepLength * length(direction.xy), info.shadowVolumeRange / min(dims.x, dims.y)));
				opticalDepth += density * stepLength * info.sunExtinction * GAME_UNIT_TO_M;
			}
			transmittance = exp(-max(opticalDepth, 0.0));
		}
	}
	ShadowMeanSamples[lane] = transmittance;
	GroupMemoryBarrierWithGroupSync();
	[unroll] for (uint stride = 32u; stride > 0u; stride >>= 1u)
	{
		if (lane < stride)
			ShadowMeanSamples[lane] += ShadowMeanSamples[lane + stride];
		GroupMemoryBarrierWithGroupSync();
	}
	if (lane == 0u) {
		RWCloudShadowMean[uint2(0, group.y + 1u)] = float4(saturate(ShadowMeanSamples[0] / 64.0), 0, 0, 0);
		if (group.y == 0u)
			RWCloudShadowMean[uint2(0, 0)] = float4(info.shadowVolumeBottom, info.shadowVolumeTop, info.cloudCurvatureRadius, 1);
	}
}
