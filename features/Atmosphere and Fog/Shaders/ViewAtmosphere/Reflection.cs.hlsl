#define COMPUTESHADER
#define PS_LINEAR_SHADOW_SAMPLER
#include "IBL/IBL.hlsli"
#include "PhysicalSky/Common.hlsli"
#define SKYLIGHTING_PROBE_REGISTER t50
#include "Skylighting/Skylighting.hlsli"
#ifdef EXP_HEIGHT_FOG
#	include "ExponentialHeightFog/VolumetricFogCommon.hlsli"
#endif
#ifdef TERRAIN_SHADOWS
#	include "TerrainShadows/TerrainShadows.hlsli"
#endif

SamplerState LinearSampler : register(s0);
Texture2D<float4> ReflectionMsLut : register(t2);

#ifdef REFLECTION_SKY_SH
RWTexture2D<sh2> ReflectionSkySH : register(u0);
groupshared float4 SkyR[256], SkyG[256], SkyB[256];
groupshared float3 ClearSky[256];

// Build the illumination input before local fog is composed into the reflection.
// No final reflections texture or previous sky SH is read here.
[numthreads(16, 16, 1)] void main(uint2 pixel : SV_DispatchThreadID, uint lane : SV_GroupIndex) {
	const float3 direction = SphericalHarmonics::GetUniformSphereSample((pixel.x + 0.5) / 16.0, (pixel.y + 0.5) / 16.0);
	float3 radiance = PhysSky::SampleSky(direction, 0.0, LinearSampler);
	ClearSky[lane] = radiance;
	if (SharedData::physSkyData.enableVolumetricClouds) {
		const float cloudT = PhysSky::TexVolCubeTr.SampleLevel(LinearSampler, direction, 0);
		const float depth = PhysSky::TexVolCubeDepth.SampleLevel(LinearSampler, direction, 0).x * 1000.0 / GAME_UNIT_TO_M;
		const float4 air = PhysSky::SampleAp(direction, depth, 0.0, LinearSampler);
		// The incident field must not contain the clear-sky foreground AP source:
		// that source is integrated with visibility by ReflectionMedium itself.
		// In particular, a black opaque cloud must not expose unshadowed air.rgb.
		radiance = radiance * cloudT + PhysSky::TexVolCubeLum.SampleLevel(LinearSampler, direction, 0).rgb * air.a;
	}
	// Match DiffuseIBLCS's reversed cubemap projection convention.
	const sh2 basis = SphericalHarmonics::Evaluate(-direction) * (4.0 * Math::PI / 256.0);
	SkyR[lane] = radiance.r * basis;
	SkyG[lane] = radiance.g * basis;
	SkyB[lane] = radiance.b * basis;
	GroupMemoryBarrierWithGroupSync();
	[unroll] for (uint stride = 128u; stride > 0u; stride >>= 1u)
	{
		if (lane < stride) {
			SkyR[lane] += SkyR[lane + stride];
			SkyG[lane] += SkyG[lane + stride];
			SkyB[lane] += SkyB[lane + stride];
			ClearSky[lane] += ClearSky[lane + stride];
		}
		GroupMemoryBarrierWithGroupSync();
	}
	if (lane == 0u) {
		ReflectionSkySH[uint2(0, 0)] = SkyR[0];
		ReflectionSkySH[uint2(1, 0)] = SkyG[0];
		ReflectionSkySH[uint2(2, 0)] = SkyB[0];
		// Compare the DC incident radiance to the clear-sky reference in the same
		// units. This is a low-frequency cloud transport estimate, not sun visibility.
		const float3 cloudy = max(float3(SkyR[0].x, SkyG[0].x, SkyB[0].x) * 0.28209479177387814, 0.0);
		const float3 clear = ClearSky[0] / 256.0;
		const float3 transport = float3(
			clear.x > 1e-20 ? saturate(cloudy.x / clear.x) : 1.0,
			clear.y > 1e-20 ? saturate(cloudy.y / clear.y) : 1.0,
			clear.z > 1e-20 ? saturate(cloudy.z / clear.z) : 1.0);
		ReflectionSkySH[uint2(0, 1)] = float4(transport, 1.0);
		ReflectionSkySH[uint2(1, 1)] = 0.0;
		ReflectionSkySH[uint2(2, 1)] = 0.0;
	}
}
#else
RWTexture3D<float4> ReflectionLuminance : register(u0);
RWTexture3D<float4> ReflectionTransmittance : register(u1);
groupshared float3 SegmentL[128], SegmentT[128];
groupshared float3 AngularRayleigh[2], AngularMie[2];
groupshared float AngularFog[2];

void BuildAngularPhase(uint2 pixel, uint ray)
{
	const SharedData::PhysSkyData sky = SharedData::physSkyData;
	float3 rayleigh = 0.0, mie = 0.0;
	float fogPhase = 0.0;
	const float top = PhysSky::InvSkyViewLutUv(float2(0.0, float(pixel.y) / 32.0), 32u).z;
	const float bottom = PhysSky::InvSkyViewLutUv(float2(0.0, float(pixel.y + 1u) / 32.0), 32u).z;
	// Average phase over the texel's solid angle: a coarse ray must not miss or
	// over-amplify the forward peak. This is shared by all 31 distance intervals.
	[unroll] for (uint y = 0u; y < 4u; ++y)
	{
		const float z0 = PhysSky::InvSkyViewLutUv(float2(0.0, (pixel.y + y * 0.25) / 32.0), 32u).z;
		const float z1 = PhysSky::InvSkyViewLutUv(float2(0.0, (pixel.y + (y + 1u) * 0.25) / 32.0), 32u).z;
		const float z = 0.5 * (z0 + z1);
		const float radius = sqrt(max(0.0, 1.0 - z * z));
		const float weight = 0.25 * (z0 - z1) / max(top - bottom, 1e-8);
		[unroll] for (uint x = 0u; x < 4u; ++x)
		{
			const float azimuth = (pixel.x + (x + 0.5) * 0.25) * (2.0 * Math::PI / 32.0);
			const float3 direction = float3(radius * cos(azimuth), radius * sin(azimuth), z);
			const float3 cosine = float3(dot(direction, sky.sunDir), dot(direction, sky.masserDir), dot(direction, sky.secundaDir));
			rayleigh += weight * float3(PhysSky::Phase::Rayleigh(cosine.x), PhysSky::Phase::Rayleigh(cosine.y), PhysSky::Phase::Rayleigh(cosine.z));
			mie += weight * float3(PhysSky::Phase::CornetteShanks(cosine.x, sky.aerosolPhaseG), PhysSky::Phase::CornetteShanks(cosine.y, sky.aerosolPhaseG), PhysSky::Phase::CornetteShanks(cosine.z, sky.aerosolPhaseG));
#	ifdef EXP_HEIGHT_FOG
			const float g = SharedData::exponentialHeightFogSettings.volumetricFogEnabled ? SharedData::exponentialHeightFogSettings.volumetricFogScatteringDistribution : SharedData::exponentialHeightFogSettings.directionalInscatteringAnisotropy;
			fogPhase += weight * ExponentialHeightFog::HenyeyGreenstein(dot(direction, PhysSky::GetCloudShadowLightDirection()), g);
#	endif
		}
	}
	AngularRayleigh[ray] = rayleigh;
	AngularMie[ray] = mie;
	AngularFog[ray] = fogPhase;
}

float DirectVisibility(float3 position)
{
	float visibility = dot(PhysSky::GetDirlightTransmittance(position + FrameBuffer::CameraPosAdjust.xyz, LinearSampler, true), 1.0 / 3.0);
#	ifdef TERRAIN_SHADOWS
	if (ViewAtmosphere::Data[0].flags.z & 256u)
		visibility *= TerrainShadows::GetTerrainShadow(position + FrameBuffer::CameraPosAdjust.xyz, LinearSampler);
#	endif
#	ifdef CLOUD_SHADOWS
	if (ViewAtmosphere::Data[0].flags.z & 512u)
		visibility *= CloudShadows::GetCloudShadowMult(position, LinearSampler);
#	endif
	return saturate(visibility);
}

float SkyVisibility(float3 position, float3 direction, float g)
{
	if (!(ViewAtmosphere::Data[0].flags.z & 8u))
		return 1.0;
	const sh2 phase = SphericalHarmonics::EvaluatePhaseHG(direction, g);
	const float visibility = saturate(dot(Skylighting::SampleNoBias(position), phase));
	return Skylighting::MixDiffuse(lerp(1.0, visibility, Skylighting::GetFadeOutFactor(position)));
}

float3 AtmosphereSource(float3 position, float3 lightDirection, float3 color,
	float3 rayleigh, float3 mie, float rayleighPhase, float miePhase, float directVisibility, float3 diffuseVisibility)
{
	if (!any(color > 0.0))
		return 0.0;
	const float3 planet = PhysSky::GetMediumPlanetPosition(position);
	const float radius = length(planet);
	const float3 tr = PhysSky::SampleAtmosphereLightTr(PhysSky::TexTrLut, LinearSampler, planet, lightDirection);
	const float3 ms = ReflectionMsLut.SampleLevel(LinearSampler, PhysSky::MsLutUv(radius, dot(planet / radius, lightDirection)), 0).rgb;
	float fogTr = 1.0;
#	ifdef EXP_HEIGHT_FOG
	if (ViewAtmosphere::Data[0].flags.y != 0u)
		fogTr = ExponentialHeightFog::EvaluateHeightFogLightTransmittance(position, FrameBuffer::CameraPosAdjust.xyz, lightDirection);
#	endif
	return color * (directVisibility * fogTr * tr *
						   (rayleigh * rayleighPhase + mie * miePhase) +
					   (rayleigh + mie) * ms * diffuseVisibility);
}

#	ifdef EXP_HEIGHT_FOG
float FogOpticalDepth(float3 direction, float nearDistance, float farDistance)
{
	const SharedData::ExponentialHeightFogSettings fog = SharedData::exponentialHeightFogSettings;
	const float start = fog.volumetricFogEnabled ? fog.volumetricFogStartDistance : fog.startDistance;
	nearDistance = max(nearDistance, start);
	if (farDistance <= nearDistance)
		return 0.0;
	const float height0 = FrameBuffer::CameraPosAdjust.z + direction.z * nearDistance;
	const float height1 = FrameBuffer::CameraPosAdjust.z + direction.z * farDistance;
	float integral = fog.fogDensity * 0.001 * ExponentialHeightFog::HeightDensityIntegral(height0 - fog.fogHeight, height1 - fog.fogHeight, fog.fogHeightFalloff * 0.001);
	integral += fog.fogDensity2 * 0.001 * ExponentialHeightFog::HeightDensityIntegral(height0 - fog.fogHeight2, height1 - fog.fogHeight2, fog.fogHeightFalloff2 * 0.001);
	float scale = 0.69314718056;
	if (fog.volumetricFogEnabled) {
		scale = fog.volumetricFogExtinctionScale * (0.5 / 0.69314718056);
		const float midpoint = 0.5 * (nearDistance + farDistance);
		scale *= ExponentialHeightFog::EvaluateHeightFogNoiseModulation(direction * midpoint, FrameBuffer::CameraPosAdjust.xyz);
		scale *= saturate((midpoint - start) / max(fog.volumetricFogNearFadeInDistance, 1.0));
	}
	return max(0.0, integral * (farDistance - nearDistance) * scale);
}

float3 FogSource(float3 position, float3 direction, float visibility, float phase)
{
	const SharedData::ExponentialHeightFogSettings fog = SharedData::exponentialHeightFogSettings;
	const bool volumetric = fog.volumetricFogEnabled != 0u;
	const float g = volumetric ? fog.volumetricFogScatteringDistribution : fog.directionalInscatteringAnisotropy;
	const float skyVisibility = SkyVisibility(position, direction, g);
	float3 ambient = fog.fogInscatteringColor.rgb * fog.fogInscatteringColor.a * skyVisibility;
	if (ViewAtmosphere::Data[0].flags.z & 4u) {
		if (volumetric)
			ambient = ImageBasedLighting::GetPhaseIBLColorOccluded(direction, g, skyVisibility, position);
		else if (fog.useSkyIBL) {
			const sh2 phase = SphericalHarmonics::EvaluatePhaseHG(-direction, g);
			float3 skyColor = max(0.0, float3(dot(ImageBasedLighting::SkyIBLTexture.Load(int3(0, 0, 0)), phase),
										   dot(ImageBasedLighting::SkyIBLTexture.Load(int3(1, 0, 0)), phase), dot(ImageBasedLighting::SkyIBLTexture.Load(int3(2, 0, 0)), phase)));
			skyColor = Color::Saturation(skyColor, SharedData::iblSettings.SkyIBLSaturation) * SharedData::iblSettings.SkyIBLScale;
			ambient += skyColor * skyVisibility * fog.inscatteringTint.rgb * fog.inscatteringTint.a;
		}
	}
	const float3 lightDirection = PhysSky::GetCloudShadowLightDirection();
	const float3 lightColor = SharedData::physSkyData.volCloudUseSun != 0u ? SharedData::physSkyData.sunlightColor : SharedData::DirLightColor.xyz;
	const float3 tr = PhysSky::SampleAtmosphereLightTr(PhysSky::TexTrLut, LinearSampler, PhysSky::GetMediumPlanetPosition(position), lightDirection);
	const float fogTr = ExponentialHeightFog::EvaluateHeightFogLightTransmittance(position, FrameBuffer::CameraPosAdjust.xyz, lightDirection);
	const float3 direct = Color::DirectionalLight(lightColor) * tr * fogTr * visibility * phase;
	if (!volumetric)
		return ColorManagement::SceneToLinear(ambient + direct * fog.directionalInscatteringMultiplier);
	const float3 albedo = saturate(fog.volumetricFogAlbedo.rgb) * fog.volumetricFogAlbedo.a;
	return albedo * (ambient * fog.volumetricSkyLightingIntensity + direct * fog.volumetricDirectionalScatteringIntensity) +
	       fog.volumetricFogEmissive.rgb * fog.volumetricFogEmissive.a;
}
#	endif

// Two angular rays per group; evaluate 31 intervals in parallel, then prefix-compose.
[numthreads(32, 2, 1)] void main(uint3 thread : SV_GroupThreadID, uint3 group : SV_GroupID, uint lane : SV_GroupIndex) {
	const uint2 pixel = uint2(group.x * 2u + thread.y, group.y);
	if (thread.x == 0u)
		BuildAngularPhase(pixel, thread.y);
	GroupMemoryBarrierWithGroupSync();
	const float3 direction = PhysSky::InvSkyViewLutUv((pixel + 0.5) / 32.0, 32u);
	float nearDistance = ViewAtmosphere::ReflectionDistance(thread.x);
	float farDistance = ViewAtmosphere::ReflectionDistance(min(thread.x + 1u, 31u));
	// Clip atmospheric integration at the planet, rather than extending constant fog underground.
	const float radius = max(SharedData::physSkyData.zCameraPlanet, SharedData::physSkyData.rPlanet);
	const float b = radius * direction.z;
	const float c = (radius - SharedData::physSkyData.rPlanet) * (radius + SharedData::physSkyData.rPlanet);
	const float discriminant = b * b - c;
	if (b < 0.0 && discriminant >= 0.0) {
		const float ground = c / max(-b + sqrt(discriminant), 1e-6);
		nearDistance = min(nearDistance, ground);
		farDistance = min(farDistance, ground);
	}
	const float3 position = direction * (0.5 * (nearDistance + farDistance));
	const float stepKm = max(farDistance - nearDistance, 0.0) * GAME_UNIT_TO_M / 1000.0;
	float3 rayleigh, mie, extinction;
	PhysSky::GetMediumCoefficients(position, rayleigh, mie, extinction);
	const float directVisibility = DirectVisibility(position);
	const float3 diffuseVisibility = SkyVisibility(position, direction, 0.0) * PhysSky::GetCloudDiffuseTransmittance(position + FrameBuffer::CameraPosAdjust.xyz);
	const SharedData::PhysSkyData sky = SharedData::physSkyData;
	// Existing world shadow data follows the active directional caster only.
	const float3 caster = PhysSky::GetCloudShadowLightDirection();
	float3 source = AtmosphereSource(position, sky.sunDir, sky.sunlightColor, rayleigh, mie, AngularRayleigh[thread.y].x, AngularMie[thread.y].x, dot(caster, sky.sunDir) > 0.999 ? directVisibility : 1.0, diffuseVisibility);
	source += AtmosphereSource(position, sky.masserDir, sky.masserColor, rayleigh, mie, AngularRayleigh[thread.y].y, AngularMie[thread.y].y, dot(caster, sky.masserDir) > 0.999 ? directVisibility : 1.0, diffuseVisibility);
	source += AtmosphereSource(position, sky.secundaDir, sky.secundaColor, rayleigh, mie, AngularRayleigh[thread.y].z, AngularMie[thread.y].z, dot(caster, sky.secundaDir) > 0.999 ? directVisibility : 1.0, diffuseVisibility);
	source *= sky.apLumMix;
	extinction *= sky.apTrMix;
#	ifdef EXP_HEIGHT_FOG
	if (ViewAtmosphere::Data[0].flags.y != 0u && stepKm > 0.0) {
		const float fogExtinction = FogOpticalDepth(direction, nearDistance, farDistance) / stepKm;
		extinction += fogExtinction;
		source += fogExtinction * FogSource(position, direction, directVisibility, AngularFog[thread.y]);
	}
#	endif
	const float3 tau = extinction * stepKm;
	float3 tr = exp(-tau);
	const float3 factor = float3(tau.x < 1e-3 ? stepKm * (1.0 - 0.5 * tau.x) : (1.0 - tr.x) / extinction.x,
		tau.y < 1e-3 ? stepKm * (1.0 - 0.5 * tau.y) : (1.0 - tr.y) / extinction.y,
		tau.z < 1e-3 ? stepKm * (1.0 - 0.5 * tau.z) : (1.0 - tr.z) / extinction.z);
	float3 lum = max(source, 0.0) * factor;
	uint bank = 0u;
	[unroll] for (uint stride = 1u; stride < 32u; stride <<= 1u)
	{
		GroupMemoryBarrierWithGroupSync();
		SegmentL[bank + lane] = lum;
		SegmentT[bank + lane] = tr;
		GroupMemoryBarrierWithGroupSync();
		if (thread.x >= stride) {
			lum = SegmentL[bank + lane - stride] + SegmentT[bank + lane - stride] * lum;
			tr = SegmentT[bank + lane - stride] * tr;
		}
		bank = 64u - bank;
	}
	if (thread.x == 0u) {
		ReflectionLuminance[uint3(pixel, 0)] = 0.0;
		ReflectionTransmittance[uint3(pixel, 0)] = 1.0;
	}
	if (thread.x < 31u) {
		ReflectionLuminance[uint3(pixel, thread.x + 1u)] = float4(min(lum, 65504.0), 0.0);
		ReflectionTransmittance[uint3(pixel, thread.x + 1u)] = float4(tr, 1.0);
	}
}
#endif
