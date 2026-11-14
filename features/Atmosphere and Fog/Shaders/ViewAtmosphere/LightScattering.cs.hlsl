#define COMPUTESHADER
#define PS_LINEAR_SHADOW_SAMPLER
#include "Common/Random.hlsli"
#include "Common/ViewAtmosphere.hlsli"
#include "PhysicalSky/Common.hlsli"

SamplerState LinearSampler : register(s0);
SamplerComparisonState ShadowSampler : register(s1);
Texture3D<float4> MediumHistory : register(t0);
Texture2DArray<float4> DirectionalShadowMap : register(t1);
Texture2D<float4> MediumMsLut : register(t2);
RWTexture3D<float4> MediumSource : register(u0);
cbuffer MediumDispatch : register(b0)
{
	uint MediumLevel;
	uint3 MediumPadding;
};
struct DirectionalShadowLightData
{
	column_major float4x4 ShadowProj[2];
	column_major float4x4 InvShadowProj[2];
	float2 EndSplitDistances;
	float2 StartSplitDistances;
};
StructuredBuffer<DirectionalShadowLightData> DirectionalShadowLights : register(t98);

#if defined(TERRAIN_SHADOWS)
#	include "TerrainShadows/TerrainShadows.hlsli"
#endif

float MediumVisibility(float3 position, out float atmosphereVisibility, out float3 diffuseVisibility)
{
	float visibility = 1.0;
	if (ViewAtmosphere::Data[0].flags.z & 1u) {
		const DirectionalShadowLightData light = DirectionalShadowLights[0];
		const float depth = SharedData::GetScreenDepth(FrameBuffer::GetShadowDepth(position));
		if (light.EndSplitDistances.y > 0.0 && depth < light.EndSplitDistances.y) {
			const uint cascade = depth >= light.EndSplitDistances.x ? 1u : 0u;
			const float3 posLS = mul(light.ShadowProj[cascade], float4(position + FrameBuffer::CameraPosAdjust.xyz, 1)).xyz;
			if (all(posLS.xy >= 0.0) && all(posLS.xy <= 1.0)) {
				const float bias = SharedData::viewAtmosphereSettings.shadowBias;
				visibility *= dot(float4(DirectionalShadowMap.GatherRed(LinearSampler, float3(posLS.xy, cascade)) > posLS.z - bias), 0.25);
			}
		}
	}
#ifdef TERRAIN_SHADOWS
	if (ViewAtmosphere::Data[0].flags.z & 256u)
		visibility *= TerrainShadows::GetTerrainShadow(position + FrameBuffer::CameraPosAdjust.xyz, LinearSampler);
#endif
	atmosphereVisibility = visibility;
	float layerShadow = 1.0;
#ifdef CLOUD_SHADOWS
	if ((ViewAtmosphere::Data[0].flags.z & 512u) && CloudShadows::IntersectCloudDist(position, SharedData::DirLightDirection.xyz) >= 0.0)
		layerShadow = CloudShadows::GetCloudShadowMult(position, LinearSampler);
#endif
	const float volumeShadow = PhysSky::GetDirlightTransmittance(position + FrameBuffer::CameraPosAdjust.xyz, LinearSampler, true).x;
	const float2 remap = SharedData::physSkyData.cloudShadowRemapRange;
	// MS LUTs assume clear-sky illumination. Keep cloud transport on that source
	// beyond the direct-shadow box, including light scattered by the clouds.
	diffuseVisibility = atmosphereVisibility * PhysSky::Remap(layerShadow, remap.x, remap.y, 0, 1) * PhysSky::GetCloudDiffuseTransmittance(position + FrameBuffer::CameraPosAdjust.xyz);
	atmosphereVisibility *= PhysSky::Remap(layerShadow, remap.x, remap.y, 0, 1) * PhysSky::Remap(volumeShadow, remap.x, remap.y, 0, 1);
	visibility *= layerShadow * volumeShadow;
	return saturate(visibility);
}

#ifdef EXP_HEIGHT_FOG
#	define VIEW_ATMOSPHERE_INJECTION
#	include "ExponentialHeightFog/VolumetricFogCommon.hlsli"
#	include "IBL/IBL.hlsli"
TextureCube<float3> MediumEnvironment : register(t31);
#	define SKYLIGHTING_PROBE_REGISTER t50
#	include "Skylighting/Skylighting.hlsli"
#	ifdef LIGHT_LIMIT_FIX
#		include "LightLimitFix/LightLimitFix.hlsli"
Texture2DArray<float> EngineLocalShadowMaps : register(t5);
cbuffer EngineLocalShadowCB : register(b1)
{
	LightLimitFix::LocalShadowData EngineLocalShadows[4];
};
#	endif
#	define VolumetricFogGridSize ViewAtmosphere::Data[0].grids[MediumLevel].size.xyz
#	define VolumetricFogInvGridSize (1.0 / VolumetricFogGridSize)
#	define VolumetricFogHasDirectionalShadowMap ((ViewAtmosphere::Data[0].flags.z & 1u) != 0u)
#	define VolumetricFogHasIBL ((ViewAtmosphere::Data[0].flags.z & 4u) != 0u)
#	define VolumetricFogHasSkylighting ((ViewAtmosphere::Data[0].flags.z & 8u) != 0u)
#	define VolumetricFogHasLocalLights ((ViewAtmosphere::Data[0].flags.z & 32u) != 0u)
#	define VolumetricFogHasLocalShadowCache ((ViewAtmosphere::Data[0].flags.z & 64u) != 0u)
#	define VolumetricFogHasEngineLocalShadows ((ViewAtmosphere::Data[0].flags.z & 128u) != 0u)
#	define VolumetricFogStateFrameIndexMod8 (SharedData::FrameCount & 7u)
uint4 Rand4DPCG32(int4 p)
{
	uint4 v = uint4(p) * 1664525u + 1013904223u;
	v.x += v.y * v.w;
	v.y += v.z * v.x;
	v.z += v.x * v.y;
	v.w += v.y * v.z;
	v ^= v >> 16u;
	v.x += v.y * v.w;
	v.y += v.z * v.x;
	v.z += v.x * v.y;
	v.w += v.y * v.z;
	return v;
}
namespace ExponentialHeightFog
{
	float3 ComputeCellWorldPosition(uint3 coord, float3 offset, out float viewDepth)
	{
		const float3 position = ViewAtmosphere::Position(ViewAtmosphere::Data[0].grids[MediumLevel], coord, offset);
		viewDepth = abs(mul(FrameBuffer::CameraView, float4(position, 1)).z);
		return position;
	}
}
#	include "ExponentialHeightFog/VolumetricFogLighting.hlsli"

float4 AnalyticalFogSource(float3 position, float3 materialPosition, float visibility)
{
	const SharedData::ExponentialHeightFogSettings fog = SharedData::exponentialHeightFogSettings;
	if (length(materialPosition) < fog.startDistance)
		return 0.0;
	const float height = materialPosition.z + FrameBuffer::CameraPosAdjust.z;
	const float density = 0.001 * (fog.fogDensity * exp2(-fog.fogHeightFalloff * 0.001 * max(height - fog.fogHeight, 0.0)) +
									  fog.fogDensity2 * exp2(-fog.fogHeightFalloff2 * 0.001 * max(height - fog.fogHeight2, 0.0)));
	const float extinction = density * 0.48045301392;
	if (extinction <= 0.0)
		return 0.0;
	float3 color = fog.fogInscatteringColor.rgb * fog.fogInscatteringColor.a;
	const float3 direction = normalize(position);
	const float3 cubeDirection = normalize(lerp(position, float3(0, 0, 1), saturate((fog.cubemapMipLevel + 1.0) / 9.0)));
	float3 environment = 0.0;
	if (!SharedData::InInterior && fog.useSkyIBL && VolumetricFogHasIBL)
		environment = ImageBasedLighting::GetSkyIBLColor(cubeDirection);
	if (SharedData::InInterior && fog.useDynamicCubemaps && (ViewAtmosphere::Data[0].flags.z & 1024u))
		environment = MediumEnvironment.SampleLevel(LinearSampler, cubeDirection, fog.cubemapMipLevel).rgb;
	color += environment * fog.inscatteringTint.rgb * fog.inscatteringTint.a;
	color += ComputeDirectionalLightColor(position) * ExponentialHeightFog::HenyeyGreenstein(dot(GetFogLightDirection(), direction), fog.directionalInscatteringAnisotropy) * fog.directionalInscatteringMultiplier * visibility;
	return float4(max(ColorManagement::SceneToLinear(color), 0.0) * extinction, extinction);
}
#endif

float3 AtmosphereLight(float3 position, float3 direction, float3 lightDir, float3 color, float3 rayleigh, float3 mie, float directVisibility, float3 diffuseVisibility)
{
	if (!any(color > 0.0) || dot(lightDir, lightDir) < 0.5 || !any(rayleigh + mie > 0.0))
		return 0.0;
	const float3 planet = PhysSky::GetMediumPlanetPosition(position);
	const float radius = length(planet);
	const float cosine = dot(direction, lightDir);
	const float3 tr = PhysSky::SampleAtmosphereLightTr(PhysSky::TexTrLut, LinearSampler, planet, lightDir);
	const float3 ms = MediumMsLut.SampleLevel(LinearSampler, PhysSky::MsLutUv(radius, dot(planet / radius, lightDir)), 0).rgb;
	float fogTransmittance = 1.0;
#ifdef EXP_HEIGHT_FOG
	if (ViewAtmosphere::Data[0].flags.y != 0u)
		fogTransmittance = ExponentialHeightFog::EvaluateHeightFogLightTransmittance(position, FrameBuffer::CameraPosAdjust.xyz, lightDir);
#endif
	return color * (directVisibility * fogTransmittance * tr * (rayleigh * PhysSky::Phase::Rayleigh(cosine) + mie * PhysSky::Phase::CornetteShanks(cosine, SharedData::physSkyData.aerosolPhaseG)) + diffuseVisibility * (rayleigh + mie) * ms);
}

[numthreads(8, 8, 4)] void main(uint3 coord : SV_DispatchThreadID) {
	const ViewAtmosphere::Parameters info = ViewAtmosphere::Data[0];
	const ViewAtmosphere::Grid grid = info.grids[MediumLevel];
	if (any(coord >= grid.size.xyz))
		return;
	const uint2 seed = Random::pcg2d(coord.xy + uint2(coord.z * 313u, MediumLevel * 997u));
	const float jitter = lerp(0.5, Random::R2Modified(SharedData::FrameCountAlwaysActive, seed / 4294967295.0).x, info.history.y);
	const float2 xyJitter = Random::R2Modified(SharedData::FrameCountAlwaysActive, seed.yx / 4294967295.0) - 0.5;
	const float3 offset = float3(0.5 + xyJitter * info.history.z, jitter);
	const float3 position = ViewAtmosphere::Position(grid, coord, offset);
	const float3 center = ViewAtmosphere::Position(grid, coord, 0.5);
	const float3 direction = normalize(position);
	float atmosphereVisibility;
	float3 diffuseVisibility;
	const float visibility = MediumVisibility(position, atmosphereVisibility, diffuseVisibility);
	const SharedData::PhysSkyData sky = SharedData::physSkyData;
	float3 rayleigh, mie, extinction;
	PhysSky::GetMediumCoefficients(position, rayleigh, mie, extinction);
	float3 light = AtmosphereLight(position, direction, sky.sunDir, sky.sunlightColor, rayleigh, mie, atmosphereVisibility, diffuseVisibility);
	light += AtmosphereLight(position, direction, sky.masserDir, sky.masserColor, rayleigh, mie, atmosphereVisibility, diffuseVisibility);
	light += AtmosphereLight(position, direction, sky.secundaDir, sky.secundaColor, rayleigh, mie, atmosphereVisibility, diffuseVisibility);
	light *= sky.apLumMix;
	float fogExtinction = 0.0;
#ifdef EXP_HEIGHT_FOG
	if (info.flags.y != 0u) {
		float4 fog;
		if (SharedData::exponentialHeightFogSettings.volumetricFogEnabled) {
			fog = ComputeLightScattering(coord, offset, visibility, center);
			const float fade = saturate((length(center) - SharedData::exponentialHeightFogSettings.volumetricFogStartDistance) /
										max(SharedData::exponentialHeightFogSettings.volumetricFogNearFadeInDistance, 1.0));
			fog *= fade;
		} else {
			fog = AnalyticalFogSource(position, center, visibility);
		}
		fog *= 1000.0 / GAME_UNIT_TO_M;
		light += fog.rgb;
		fogExtinction = fog.a;
	}
#endif
	float4 source = float4(max(light, 0.0), max(fogExtinction, 0.0));
	const float3 previous = center + FrameBuffer::CameraPosAdjust.xyz - FrameBuffer::CameraPreviousPosAdjust.xyz;
	const float4 clip = mul(FrameBuffer::CameraPreviousViewProjUnjittered, float4(previous, 1));
	const float2 uv = clip.xy / max(clip.w, 1e-6) * float2(0.5, -0.5) + 0.5;
	const float previousDistance = length(previous);
	if (info.history.x > 0.0 && SharedData::ResetHistory == 0u && clip.w > 0.0 && all(uv > 0.0) && all(uv < 1.0) && previousDistance >= grid.depth.x && previousDistance < grid.depth.y) {
		const float slice = ViewAtmosphere::SourceSlice(grid, previousDistance);
		const float3 historyUv = ViewAtmosphere::AtlasUv(grid, uv, clamp(slice, 0.0, float(grid.size.z - 1u)));
		const float4 old = MediumHistory.SampleLevel(LinearSampler, historyUv, 0);
		const float extinctionChange = abs(old.a - source.a) / max(max(old.a, source.a), 1e-6);
		const float historyWeight = info.history.x * saturate(1.0 - extinctionChange * 4.0);
		if (all(isfinite(old)))
			source = lerp(source, old, historyWeight);
	}
	MediumSource[coord + grid.offset.xyz] = min(source, 65504.0);
}
