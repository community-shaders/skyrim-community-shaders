#ifndef VOLUMETRIC_FOG_LIGHTING_HLSLI
#define VOLUMETRIC_FOG_LIGHTING_HLSLI

float SampleDirectionalShadowPCF(float3 positionLS, uint cascadeIndex)
{
	uint shadowWidth;
	uint shadowHeight;
	uint shadowSlices;
	DirectionalShadowMap.GetDimensions(shadowWidth, shadowHeight, shadowSlices);
	if (cascadeIndex >= shadowSlices)
		return 1.0f;

	float2 texelSize = rcp(float2(max(shadowWidth, 1), max(shadowHeight, 1)));
	float compareDepth = positionLS.z - SharedData::viewAtmosphereSettings.shadowBias;

	float2 uvMin = texelSize * 1.5f;
	float2 uvMax = 1.0f.xx - uvMin;
	if (any(positionLS.xy < uvMin) || any(positionLS.xy > uvMax))
		return DirectionalShadowMap.SampleCmpLevelZero(ShadowSampler, float3(saturate(positionLS.xy), cascadeIndex), compareDepth).x;

	float center = DirectionalShadowMap.SampleCmpLevelZero(ShadowSampler, float3(positionLS.xy, cascadeIndex), compareDepth).x;
	float cross =
		DirectionalShadowMap.SampleCmpLevelZero(ShadowSampler, float3(positionLS.xy + float2(texelSize.x, 0.0f), cascadeIndex), compareDepth).x +
		DirectionalShadowMap.SampleCmpLevelZero(ShadowSampler, float3(positionLS.xy - float2(texelSize.x, 0.0f), cascadeIndex), compareDepth).x +
		DirectionalShadowMap.SampleCmpLevelZero(ShadowSampler, float3(positionLS.xy + float2(0.0f, texelSize.y), cascadeIndex), compareDepth).x +
		DirectionalShadowMap.SampleCmpLevelZero(ShadowSampler, float3(positionLS.xy - float2(0.0f, texelSize.y), cascadeIndex), compareDepth).x;

	return (center * 4.0f + cross) * rcp(8.0f);
}

float SampleDirectionalShadow(float3 positionWS)
{
	if (SharedData::InInterior || SharedData::HideSky || SharedData::InMapMenu)
		return 1.0f;
	if (!VolumetricFogHasDirectionalShadowMap)
		return 1.0f;

	DirectionalShadowLightData directionalShadowLightData = DirectionalShadowLights[0];
	float shadowMapDepth = SharedData::GetScreenDepth(FrameBuffer::GetShadowDepth(positionWS));
	if (shadowMapDepth >= directionalShadowLightData.EndSplitDistances.y)
		return 1.0f;

	float splitDenom = max(directionalShadowLightData.EndSplitDistances.x - directionalShadowLightData.StartSplitDistances.y, 1e-4f);
	float cascadeSelect = smoothstep(0.0f, 1.0f, saturate((shadowMapDepth - directionalShadowLightData.StartSplitDistances.y) / splitDenom));
	uint primaryCascade = (uint)cascadeSelect;

	float3 absolutePositionWS = positionWS + FrameBuffer::CameraPosAdjust.xyz;
	float3 positionLS = mul(directionalShadowLightData.ShadowProj[primaryCascade], float4(absolutePositionWS, 1.0f)).xyz;
	if (any(positionLS.xy < 0.0f) || any(positionLS.xy > 1.0f))
		return 1.0f;

	float shadow = SampleDirectionalShadowPCF(positionLS, primaryCascade);

	[branch] if (cascadeSelect > 0.0f && cascadeSelect < 1.0f)
	{
		uint secondaryCascade = 1u - primaryCascade;
		float3 secondaryLS = mul(directionalShadowLightData.ShadowProj[secondaryCascade], float4(absolutePositionWS, 1.0f)).xyz;
		if (!any(secondaryLS.xy < 0.0f) && !any(secondaryLS.xy > 1.0f)) {
			float secondaryShadow = SampleDirectionalShadowPCF(secondaryLS, secondaryCascade);
			shadow = lerp(shadow, secondaryShadow, cascadeSelect);
		}
	}

	float fade = saturate(shadowMapDepth / max(directionalShadowLightData.EndSplitDistances.y, 1.0f));
	float fadeFactor = 1.0f - pow(fade * fade, 8.0f);
	return lerp(1.0f, shadow, fadeFactor);
}

float SampleDirectionalWorldShadow(float3 positionWS)
{
	if (SharedData::InInterior || SharedData::HideSky || SharedData::InMapMenu)
		return 1.0f;

	float worldShadow = 1.0f;
#if defined(TERRAIN_SHADOWS)
	worldShadow *= TerrainShadows::GetTerrainShadow(positionWS + FrameBuffer::CameraPosAdjust.xyz, LinearSampler);
#endif
#if defined(CLOUD_SHADOWS)
	worldShadow *= CloudShadows::GetCloudShadowMult(positionWS, LinearSampler);
#endif
#if defined(PHYSICAL_SKY)
	if (SharedData::physSkyData.enabled)
		worldShadow *= dot(PhysSky::GetDirlightTransmittance(positionWS + FrameBuffer::CameraPosAdjust.xyz, LinearSampler, true), 1.0f.xxx / 3.0f);
#endif
	return worldShadow;
}

float3 ComputeSkyLightScattering(float3 positionWS, float3 viewDirection)
{
	float phaseG = SharedData::exponentialHeightFogSettings.volumetricFogScatteringDistribution;
	float skyVisibility = 1.0f;
	if (VolumetricFogHasSkylighting && !SharedData::InInterior) {
		// Visibility SH uses directions toward the source, unlike the IBL cubemap projection.
		const sh2 phase = SphericalHarmonics::EvaluatePhaseHG(viewDirection, phaseG);
		const sh2 skylightingSH = Skylighting::SampleNoBias(positionWS);
		const float visibility = SphericalHarmonics::FuncProductIntegral(skylightingSH, phase);
		skyVisibility = Skylighting::MixDiffuse(lerp(1.0f, saturate(visibility), Skylighting::GetFadeOutFactor(positionWS)));
	}

	float3 skyLighting =
		SharedData::exponentialHeightFogSettings.fogInscatteringColor.rgb *
		SharedData::exponentialHeightFogSettings.fogInscatteringColor.a *
		skyVisibility;
	[branch] if (VolumetricFogHasIBL)
		skyLighting = ImageBasedLighting::GetPhaseIBLColorOccluded(viewDirection, phaseG, skyVisibility, positionWS);

	return skyLighting *
	       SharedData::exponentialHeightFogSettings.volumetricSkyLightingIntensity;
}

float3 GetFogLightDirection()
{
#if defined(PHYSICAL_SKY)
	if (SharedData::physSkyData.enabled && !SharedData::InInterior && !SharedData::HideSky)
		return PhysSky::GetCloudShadowLightDirection();
#endif
	return normalize(SharedData::DirLightDirection.xyz);
}

float3 ComputeDirectionalLightColor(float3 positionWS)
{
	const float3 lightDirection = GetFogLightDirection();
	float3 dirLightColor = Color::DirectionalLight(SharedData::DirLightColor.xyz);
#if defined(PHYSICAL_SKY)
	if (SharedData::physSkyData.enabled) {
		if (SharedData::physSkyData.volCloudUseSun != 0u && !SharedData::InInterior && !SharedData::HideSky)
			dirLightColor = Color::DirectionalLight(SharedData::physSkyData.sunlightColor);
		const float3 planetPosition = PhysSky::GetMediumPlanetPosition(positionWS);
		float3 physSkyTransmittance = lerp(1.0f, PhysSky::SampleAtmosphereLightTr(PhysSky::TexTrLut, LinearSampler, planetPosition, lightDirection), SharedData::physSkyData.trMix);
		dirLightColor *= saturate(physSkyTransmittance);
	}
#endif
	return dirLightColor * ExponentialHeightFog::EvaluateHeightFogLightTransmittance(positionWS, FrameBuffer::CameraPosAdjust.xyz, lightDirection);
}

#if defined(LIGHT_LIMIT_FIX)
float SampleLocalLightShadow(LightLimitFix::Light light, float3 positionWS, float3 towardLight, float noise)
{
	if (light.lightFlags & LightLimitFix::LightFlags::LocalShadow) {
		if (!VolumetricFogHasLocalShadowCache)
			return 0.0f;
		return LightLimitFix::GetLocalShadow(LinearSampler, light.localShadowIndex, positionWS, FrameBuffer::CameraPosAdjust.xyz, towardLight, false, noise);
	}
	if (light.lightFlags & LightLimitFix::LightFlags::Shadow) {
		if (!VolumetricFogHasEngineLocalShadows || light.shadowLightIndex >= 4u)
			return 0.0f;
		LightLimitFix::LocalShadowData data = EngineLocalShadows[light.shadowLightIndex];
		if (data.Params.y <= 0.0f)
			return 0.0f;
		return LightLimitFix::GetLocalShadow(EngineLocalShadowMaps, LinearSampler, data, (uint)data.Params2.z, positionWS, FrameBuffer::CameraPosAdjust.xyz, towardLight, false, noise, data.Params2.w, 1u, 0.0f);
	}
	return (light.lightFlags & LightLimitFix::LightFlags::ShadowCaster) ? 0.0f : 1.0f;
}

float ComputeLocalLightAttenuation(float distanceSqr, float cellRadius, LightLimitFix::Light light)
{
	float distance = sqrt(max(distanceSqr, 1e-6f));

	// UE biases local light integration by froxel size to avoid singular bright voxels close to the light.
	if (light.lightFlags & LightLimitFix::LightFlags::InverseSquare) {
		distance = sqrt(max(distanceSqr, cellRadius * cellRadius));
	}

	return LightLimitFix::GetAttenuation(distance, light);
}

float3 AccumulateLocalLightScattering(
	uint3 coord,
	float3 cellOffset,
	float3 positionWS,
	float viewDepth,
	float3 viewDirection,
	float3 materialScattering)
{
	if (!VolumetricFogHasLocalLights)
		return 0.0f.xxx;

	float2 volumeUV = (float2(coord.xy) + cellOffset.xy) * VolumetricFogInvGridSize.xy;
	float2 screenUV = volumeUV;

	uint clusterIndex = 0;
	if (!LightLimitFix::GetClusterIndex(screenUV, viewDepth, clusterIndex))
		return 0.0f.xxx;

	LightLimitFix::LightGrid grid = LightLimitFix::lightGrid[clusterIndex];
	uint lightCount = min(grid.lightCount, (uint)MAX_CLUSTER_LIGHTS);

	float cornerViewDepth;
	float3 cellCornerWS = ExponentialHeightFog::ComputeCellWorldPosition(coord + uint3(1, 1, 1), cellOffset, cornerViewDepth);
	float cellRadius = max(length(cellCornerWS - positionWS), 1.0f);

	float phaseG = SharedData::exponentialHeightFogSettings.volumetricFogScatteringDistribution;
	float3 localScattering = 0.0f.xxx;
	float shadowNoise = float(Rand4DPCG32(int4(coord, VolumetricFogStateFrameIndexMod8)).w) / float(uint(0xffffffff));
	[loop] for (uint lightIndex = 0; lightIndex < lightCount; lightIndex++)
	{
		uint clusteredLightIndex = LightLimitFix::lightList[grid.offset + lightIndex];
		LightLimitFix::Light light = LightLimitFix::lights[clusteredLightIndex];

		// Lightning is integrated after the shared medium's history filter.
		if (light.lightFlags & (LightLimitFix::LightFlags::Disabled | LightLimitFix::LightFlags::Lightning))
			continue;

		float3 toLight = light.positionWS.xyz - positionWS;
		float distanceSqr = dot(toLight, toLight);
		if (distanceSqr < 1e-6f)
			continue;

		float attenuation = ComputeLocalLightAttenuation(distanceSqr, cellRadius, light);
		if (attenuation < 1e-5f)
			continue;

		float3 L = toLight * rsqrt(distanceSqr);
		attenuation *= SampleLocalLightShadow(light, positionWS, L, shadowNoise);
		if (attenuation < 1e-5f)
			continue;
		// Both L and viewDirection point toward a light ahead of the camera for forward scattering.
		float phase = ExponentialHeightFog::HenyeyGreenstein(dot(L, viewDirection), phaseG);

		float3 lightColor = Color::PointLight(light.color.xyz) * attenuation * light.fade;
		localScattering += lightColor * phase;
	}

	return localScattering *
	       SharedData::exponentialHeightFogSettings.volumetricLocalLightScatteringIntensity *
	       materialScattering;
}
#else
float3 AccumulateLocalLightScattering(
	uint3 coord,
	float3 cellOffset,
	float3 positionWS,
	float viewDepth,
	float3 viewDirection,
	float3 materialScattering)
{
	return 0.0f.xxx;
}
#endif

float4 ComputeLightScattering(uint3 coord, float3 cellOffset
#ifdef VIEW_ATMOSPHERE_INJECTION
	,
	float sharedShadow, float3 materialPosition
#endif
)
{
	float viewDepth;
	float3 positionWS = ExponentialHeightFog::ComputeCellWorldPosition(coord, cellOffset, viewDepth);

#ifdef VIEW_ATMOSPHERE_INJECTION
	const float fogExtinction = ExponentialHeightFog::EvaluateHeightFogExtinction(materialPosition, FrameBuffer::CameraPosAdjust.xyz);
	const float3 fogAlbedo = saturate(SharedData::exponentialHeightFogSettings.volumetricFogAlbedo.rgb) * SharedData::exponentialHeightFogSettings.volumetricFogAlbedo.a;
	float4 materialScatteringAndExtinction = float4(fogExtinction * fogAlbedo, fogExtinction);
#else
	float4 materialScatteringAndExtinction = VBufferA[coord];
#endif
	float extinction = materialScatteringAndExtinction.w;
#ifdef VIEW_ATMOSPHERE_INJECTION
	if (extinction <= 0.0)
		return 0.0;
#endif

	float3 viewDirection = normalize(positionWS);
	float phase = ExponentialHeightFog::HenyeyGreenstein(
		dot(GetFogLightDirection(), viewDirection),
		SharedData::exponentialHeightFogSettings.volumetricFogScatteringDistribution);

#ifdef VIEW_ATMOSPHERE_INJECTION
	float directionalShadow = sharedShadow;
#else
	float directionalShadow = SampleDirectionalShadow(positionWS) *
	                          SampleDirectionalWorldShadow(positionWS);
#endif
	float3 directionalScattering =
		ComputeDirectionalLightColor(positionWS) *
		SharedData::exponentialHeightFogSettings.volumetricDirectionalScatteringIntensity *
		directionalShadow *
		phase *
		materialScatteringAndExtinction.rgb;

	float3 skyScattering = ComputeSkyLightScattering(positionWS, viewDirection) *
	                       materialScatteringAndExtinction.rgb;

#if !defined(VOLUMETRIC_FOG_FAR_GRID)
	float3 localScattering = 0.0;
#	ifdef VIEW_ATMOSPHERE_INJECTION
	if (MediumLevel == 0u)
#	endif
		localScattering = AccumulateLocalLightScattering(
			coord,
			cellOffset,
			positionWS,
			viewDepth,
			viewDirection,
			materialScatteringAndExtinction.rgb);
#else
	float3 localScattering = 0.0f.xxx;
#endif

	float3 emissive = SharedData::exponentialHeightFogSettings.volumetricFogEmissive.rgb *
	                  SharedData::exponentialHeightFogSettings.volumetricFogEmissive.a *
	                  extinction;

	return float4(max(directionalScattering + skyScattering + localScattering + emissive, 0.0f.xxx), extinction);
}

#endif
