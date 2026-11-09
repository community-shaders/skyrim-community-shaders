#ifndef PHYSICAL_SKY_CLOUD_LIGHTING_HLSLI
#define PHYSICAL_SKY_CLOUD_LIGHTING_HLSLI

float CloudLightProbeJitter(float2 positionMeters)
{
	const float2 seed = frac(positionMeters * 0.1031);
	const float scramble = dot(seed.xyx, seed.yxx + 19.19);
	return frac((seed.y + seed.x + scramble * 2.0) * (scramble + seed.x));
}

// Jendersie and d'Eon, An Approximate Mie Scattering Function (2023).
float CloudLowPhase(float cosine)
{
	const float4 fit = VolumetricCloudBuffer[0].lowCloudPhase;
	const float mu = clamp(cosine, -1.0, 1.0);
	const float2 g = fit.xy;
	const float2 oneMinusG = 1.0 - g;
	const float2 denominator = oneMinusG * oneMinusG + 2.0 * g * (1.0 - mu);
	const float2 hg = oneMinusG * (1.0 + g) / (denominator * sqrt(denominator));
	const float draine = hg.y * (1.0 + fit.z * mu * mu) / (1.0 + fit.z * (1.0 + 2.0 * g.y * g.y) / 3.0);
	return 0.25 * RCP_PI * lerp(hg.x, draine, fit.w);
}

float CloudCirrusPhase(float cosine)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	return Phase::HGDualLobe(cosine, info.phaseForwardG, info.phaseBackwardG, 0.5);
}

struct CloudAmbient
{
	sh2 skyR;
	sh2 skyG;
	sh2 skyB;
	float3 groundRadiance;
	float3 skyRadiance;
	float3 lowerBoundaryTransmittance;
};

CloudAmbient MakeCloudAmbient()
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	const float3 lowerBoundary = float3(0.0, 0.0, info.planetRadius + max(info.lowestCloudAltitude, GAME_UNITS_PER_METER));
	CloudAmbient ambient;
	ambient.skyR = TexCloudAmbientSH[int2(0, 0)];
	ambient.skyG = TexCloudAmbientSH[int2(1, 0)];
	ambient.skyB = TexCloudAmbientSH[int2(2, 0)];
	ambient.groundRadiance = TexCloudAmbientSH[int2(3, 0)].rgb;
	ambient.skyRadiance = SampleCloudAmbientRadiance();
	ambient.lowerBoundaryTransmittance = SampleAtmosphereLightTr(TexTransmittance, TransmittanceSampler, lowerBoundary, float3(0.0, 0.0, 1.0));
	return ambient;
}

float3 CloudAmbientRadiance(float3 planetPos, CloudAmbient ambient)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	const float radius = max(length(planetPos), info.planetRadius);
	const float ratio = info.planetRadius / radius;
	const float cosDisc = sqrt(saturate(1.0 - ratio * ratio));
	const float3 sampleVerticalTransmittance = SampleAtmosphereLightTr(TexTransmittance, TransmittanceSampler, planetPos, float3(0.0, 0.0, 1.0));
	const float3 columnTransmittance = min(ambient.lowerBoundaryTransmittance / max(sampleVerticalTransmittance, 1e-4), 1.0);
	const float3 groundRadiance = TexCloudAmbientSH[int2(4, 0)].rgb * columnTransmittance;
	return 0.5 * (1.0 + cosDisc) * ambient.skyRadiance + 0.5 * (1.0 - cosDisc) * groundRadiance;
}

float CloudCirrusLightDistance(float lightCosine)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	const float radius = (info.cloudCurvatureRadius + info.cirrusAltitude) * GAME_UNIT_TO_M;
	const float halfThickness = 0.5 * info.cirrusLightThickness;
	const float b = radius * lightCosine;
	const float outerC = halfThickness * (2.0 * radius + halfThickness);
	const float outerRoot = sqrt(b * b + outerC);
	float distance = b >= 0.0 ? outerC / (outerRoot + b) : outerRoot - b;
	const float innerC = halfThickness * (2.0 * radius - halfThickness);
	const float innerDiscriminant = b * b - innerC;
	if (b < 0.0 && innerDiscriminant >= 0.0)
		distance = min(distance, innerC / (-b + sqrt(innerDiscriminant)));
	return min(distance, 3000.0);
}

float CloudMeanTransmission(float depth)
{
	return depth < 0.05 ? 1.0 - depth * (0.5 - depth * (1.0 / 6.0 - depth / 24.0)) :
	                      (1.0 - exp(-depth)) / depth;
}

// Integrate the light source against view extinction across the unresolved layer.
float CloudCirrusTransmission(float viewDepth, float lightDepth, float nearFraction)
{
	const float nearDepth = lightDepth * nearFraction;
	const float farDepth = viewDepth + lightDepth * (1.0 - nearFraction);
	const float integral = exp(-min(nearDepth, farDepth)) * CloudMeanTransmission(abs(farDepth - nearDepth));
	return integral / CloudMeanTransmission(viewDepth);
}

float CloudOpacity(float depth)
{
	return depth < 0.001 ? depth * (1.0 - 0.5 * depth) : 1.0 - exp(-depth);
}

float TraceNdfLighting(float3 pos, float3 viewDir, NDFInfo ndf, float extinction, float viewDistance, float minimumMip, float transportExtinction)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	float localOpticalDepth = 0.0;
	float previousDistance = 0.0;
	float previousExtinction = extinction;
	const uint count = uint(10.0 - saturate((viewDistance - 512.0) * 0.00040192925) * 6.0);
	const float extentMeters = 240.0 - saturate(ndf.height_fraction * 3.3333333) * 120.0;
	const float spacingMeters = extentMeters / count;
	const float cosine = dot(viewDir, info.dirlightDir);
	const float angularOffset = -0.25 * cosine;
	const float jitter = CloudLightProbeJitter(pos.xy * GAME_UNIT_TO_M);
	const bool eruption = ndf.stormVolume && IsVolcanic(CloudStorms[ndf.stormIndex]);
	[loop] for (uint i = 0u; i < count; ++i)
	{
		const float index = i + 0.5;
		const float fraction = index / count;
		const float angularScale = angularOffset + 0.5 + fraction * fraction * fraction * (0.5 - angularOffset);
		// Resolve the ash billows near the surface before widening the probe cone.
		// Uniform 30 m spacing plus a full-cell jitter skipped these lobes entirely.
		const float nearFraction = (index + jitter * 0.5) / count;
		const float distanceMeters = eruption ? nearFraction * nearFraction * nearFraction * extentMeters :
		                                        (fraction * extentMeters + jitter * spacingMeters) * angularScale;
		const float3 samplePos = pos + info.dirlightDir * (distanceMeters * GAME_UNITS_PER_METER);
		CloudDensityContext context;
		// Filter the light probes separately from the detailed view density.
		const float mipBias = 2.0;
#ifdef CLOUD_VOXELS
		sampleNdfCloudDensity(samplePos, GetCloudLayer(info), mipBias, viewDistance, minimumMip, context);
#else
		sampleCloudDensity(samplePos, GetCloudLayer(info), mipBias, viewDistance, minimumMip, context);
#endif
		const float sigma = context.light_density;
		if (ndf.stormVolume && !eruption) {
			const float forward = saturate(cosine);
			const float weight = (forward + 1.0) * (spacingMeters * 0.45) * (index * 0.4 * forward + 1.0);
			localOpticalDepth += sigma * weight;
		} else
			localOpticalDepth += 0.5 * (previousExtinction + sigma) * (distanceMeters - previousDistance);
		// Both direct and transport terms are negligible past this depth.
		// Avoid the remaining expensive density probes in opaque storm interiors.
		if (ndf.stormVolume && localOpticalDepth * info.sunExtinction * min(transportExtinction, 1.0) > 12.0)
			break;
		previousExtinction = sigma;
		previousDistance = distanceMeters;
	}
	return localOpticalDepth;
}

float TraceCloudLighting(float3 pos, float3 viewDir, NDFInfo ndf, float extinction, float viewDistance, float minimumMip, float transportExtinction)
{
#ifdef CLOUD_VOXELS
	float voxelDepth = 0.0;
	[branch] if (ndf.voxelBlend > 0.0)
		voxelDepth = TraceVoxelLighting(pos, viewDistance);
	if (ndf.voxelBlend >= .9999)
		return voxelDepth;
	const float ndfDepth = TraceNdfLighting(pos, viewDir, ndf, extinction * (1.0 - ndf.voxelBlend), viewDistance, minimumMip, transportExtinction);
	return voxelDepth + ndfDepth;
#else
	return TraceNdfLighting(pos, viewDir, ndf, extinction, viewDistance, minimumMip, transportExtinction);
#endif
}

// Local isotropic closure: https://zhuanlan.zhihu.com/p/457997155
float CloudScatteringSeries(float opticalDepth, float albedo)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	const float transmission = exp(-opticalDepth);
	const float opacity = opticalDepth < 0.001 ? opticalDepth * (1.0 - 0.5 * opticalDepth) : 1.0 - transmission;
	const float recirculation = albedo * opacity;
	const float escapeAndAbsorption = (1.0 - albedo) + albedo * transmission;
	return (0.25 * RCP_PI) * recirculation / escapeAndAbsorption;
}

// Material transitions follow the extinction contribution, rather than the
// density winner, so partial storm replacement does not pop between two albedos.
float CloudMaterialAlbedo(NDFInfo ndf)
{
	const float weather = VolumetricCloudBuffer[0].msAlbedo;
	return ndf.stormVolume ? lerp(weather, CloudStorms[ndf.stormIndex].driftDensity.w, ndf.stormBlend) : weather;
}

float2 CloudMaterialAmbient(NDFInfo ndf)
{
	float2 result = CloudAmbientPhase.xy;
	if (ndf.stormVolume) {
		const float2 storm = CloudStorms[ndf.stormIndex].ambientPhase.xy;
		const float scattering = lerp(result.x * result.y, storm.x * storm.y, ndf.stormBlend);
		result.x = lerp(result.x, storm.x, ndf.stormBlend);
		result.y = scattering / max(result.x, 1e-6);
	}
	return result;
}

struct CloudMsProfile
{
	float densityScale;
	float referenceDepth;
	float4 coefficients;
};

CloudMsProfile PrepareCloudMsProfile(NDFInfo ndf)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	const float width = min(info.msBoundaryWidth, max(ndf.altitude.y - ndf.altitude.x, 0.0) * (0.25 * GAME_UNIT_TO_M));
	float densityScale = ndf.stormVolume ? lerp(info.lowDensityScale, CloudStorms[ndf.stormIndex].driftDensity.z, ndf.stormBlend) : info.lowDensityScale;
#ifdef CLOUD_VOXELS
	densityScale += ndf.voxelBlend * (ndf.voxelDensityScale - info.lowDensityScale);
#endif
	const float referenceDepth = densityScale * width;
	CloudMsProfile result = (CloudMsProfile)0;
	result.densityScale = densityScale;
	result.referenceDepth = referenceDepth;
	if (referenceDepth <= 0.0 || densityScale <= 0.0)
		return result;
	const float coordinate = clamp((log2(referenceDepth) + 12.0) * 2.0, 0.0, 48.0);
	const uint index = min((uint)coordinate, 47u);
	float4 low = ndf.stormVolume ? CloudStorms[ndf.stormIndex].msCalibration[index] : CloudMsCalibration[index];
	float4 high = ndf.stormVolume ? CloudStorms[ndf.stormIndex].msCalibration[index + 1u] : CloudMsCalibration[index + 1u];
	[branch] if (ndf.stormVolume && ndf.stormBlend < 1.0)
	{
		low = lerp(CloudMsCalibration[index], low, ndf.stormBlend);
		high = lerp(CloudMsCalibration[index + 1u], high, ndf.stormBlend);
	}
	result.coefficients = lerp(low, high, coordinate - float(index));
	return result;
}

float CloudMultipleScattering(float extinction, NDFInfo ndf, CloudMsProfile calibration)
{
	const float referenceDepth = calibration.referenceDepth;
	const float densityScale = calibration.densityScale;
	if (referenceDepth <= 0.0 || extinction <= 0.0 || densityScale <= 0.0)
		return 0.0;
	const float exponent = CloudDensityExponent(ndf.height_fraction);
	const float4 coefficients = calibration.coefficients;
	const float profile = clamp((exponent - 0.35) * 10.0, 0.0, 3.0);
	const uint profileIndex = min((uint)profile, 2u);
	float coefficient = lerp(coefficients[profileIndex], coefficients[profileIndex + 1u], profile - float(profileIndex));
	if (referenceDepth > 4096.0)
		coefficient *= pow(referenceDepth / 4096.0, exponent / (exponent + 1.0));
	else if (referenceDepth < 0.000244140625)
		coefficient *= referenceDepth * 4096.0;
	return CloudScatteringSeries(coefficient * extinction / densityScale, CloudMaterialAlbedo(ndf));
}

float CloudMultipleScattering(float extinction, NDFInfo ndf)
{
	return CloudMultipleScattering(extinction, ndf, PrepareCloudMsProfile(ndf));
}

float CloudTransportExtinction(NDFInfo ndf)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	const float4 fit = info.lowCloudPhase;
	const float meanDraine = fit.y * (1.0 + fit.z * (3.0 + 2.0 * fit.y * fit.y) / 5.0) / (1.0 + fit.z * (1.0 + 2.0 * fit.y * fit.y) / 3.0);
	const float meanCosine = lerp(fit.x, meanDraine, fit.w);
	return ndf.stormVolume ? 1.0 - CloudMaterialAlbedo(ndf) * meanCosine : info.msTransportExtinction;
}

float CloudLightOpticalDepth(float3 pos, float3 viewDir, NDFInfo ndf, float extinction, float viewDistance, float minimumMip)
{
	return TraceCloudLighting(pos, viewDir, ndf, extinction, viewDistance, minimumMip, CloudTransportExtinction(ndf)) * VolumetricCloudBuffer[0].sunExtinction;
}

float3 ShadeCloudLighting(float3 pos, NDFInfo ndf, float extinction, float viewDepth, float viewOpacity, float phase, float opticalDepth, float multipleScattering, float externalSunTransmission)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	const float albedo = CloudMaterialAlbedo(ndf);
	const float transportExtinction = CloudTransportExtinction(ndf);
	const float directional = albedo * phase * exp(-opticalDepth);
	const float3 planetPosition = pos + float3(-FrameBuffer::CameraPosAdjust.xy, info.planetRadius);
	const float3 sunlight = SampleAtmosphereLightTr(TexTransmittance, TransmittanceSampler, planetPosition, info.dirlightDir) * GetCloudDirectionalLightColor();
	float densityGate = saturate(extinction / info.scatterDensityThreshold);
	[branch] if (info.scatterDensityPower != 1.0)
		densityGate = pow(densityGate, info.scatterDensityPower);
	const float scatterAmount = CloudOpacity(viewDepth * densityGate);
	float multiple = 0.0;
	[branch] if (info.msStrength > 0.0 && albedo > 0.0)
		multiple = info.msStrength * albedo * multipleScattering * exp(-opticalDepth * transportExtinction);
	return info.cloudLightingScale * sunlight * externalSunTransmission * (directional * scatterAmount + multiple * viewOpacity);
}

float3 ShadeCloudLighting(float3 pos, NDFInfo ndf, float extinction, float viewDepth, float viewOpacity, float phase, float opticalDepth, float multipleScattering)
{
	return ShadeCloudLighting(pos, ndf, extinction, viewDepth, viewOpacity, phase, opticalDepth, multipleScattering, UpperSunTransmission(pos));
}

float3 ShadeCloudLighting(float3 pos, NDFInfo ndf, float extinction, float viewDepth, float viewOpacity, float phase, float opticalDepth)
{
	float multipleScattering = 0.0;
	[branch] if (VolumetricCloudBuffer[0].msStrength > 0.0 && CloudMaterialAlbedo(ndf) > 0.0)
		multipleScattering = CloudMultipleScattering(extinction, ndf);
	return ShadeCloudLighting(pos, ndf, extinction, viewDepth, viewOpacity, phase, opticalDepth, multipleScattering);
}

float3 CloudLighting(float3 pos, float3 viewDir, NDFInfo ndf, float extinction, float viewDepth, float viewOpacity,
	float viewDistance, float minimumMip, float phase)
{
	const float opticalDepth = CloudLightOpticalDepth(pos, viewDir, ndf, extinction, viewDistance, minimumMip);
	return ShadeCloudLighting(pos, ndf, extinction, viewDepth, viewOpacity, phase, opticalDepth);
}

#endif
