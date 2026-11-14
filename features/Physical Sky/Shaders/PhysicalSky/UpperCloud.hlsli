#ifndef PHYSICAL_SKY_UPPER_CLOUD_HLSLI
#define PHYSICAL_SKY_UPPER_CLOUD_HLSLI

#include "PhysicalSky/MidCloudBounds.hlsli"

float CirrusSunDepth(float3 position)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	if (info.cirrusEnabled == 0u)
		return 0.0;
	const float3 origin = position + float3(-FrameBuffer::CameraPosAdjust.xy, info.cloudCurvatureRadius);
	const float2 roots = IntersectSpherePair(origin, info.dirlightDir, info.cloudCurvatureRadius + info.cirrusAltitude);
	const float distance = roots.x >= 0.0 ? roots.x : roots.y;
	if (distance <= 0.0)
		return 0.0;
	const float3 hit = position + distance * info.dirlightDir;
	float profile;
	const float depth = CirrusDensity(hit.xy, profile) * 10.0;
	const float cosine = abs(dot(normalize(origin + distance * info.dirlightDir), info.dirlightDir));
	return depth / max(cosine, 0.1);
}

RWTexture2D<float2> RWUpperSunDepth : register(u0);
[numthreads(8, 8, 1)] void buildUpperSunDepth(uint2 pixel : SV_DispatchThreadID) {
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	const float2 xy = UpperSunGridCenter() + ((float2(pixel) + 0.5) / 256.0 - 0.5) * (2.0 * info.rayMarchRange);
	const float curve = CloudCurvatureOffset(xy - FrameBuffer::CameraPosAdjust.xy, info.cloudCurvatureRadius);
	const float3 bottom = float3(xy, info.lowCloudTraceBottomAltitude - curve);
	const float3 top = float3(xy, info.lowCloudTraceTopAltitude - curve);
	RWUpperSunDepth[pixel] = min(float2(MiddleRayDepth(bottom, info.dirlightDir).x + CirrusSunDepth(bottom),
									 MiddleRayDepth(top, info.dirlightDir).x + CirrusSunDepth(top)),
		80.0);
}

// Incident light and the view use the same cirrus source and wind anchoring.
float3 CirrusBoundaryRadiance(float3 position, float3 direction, float3 incoming)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	if (info.cirrusEnabled == 0u)
		return incoming;
	const float3 origin = position + float3(-FrameBuffer::CameraPosAdjust.xy, info.cloudCurvatureRadius);
	const float2 roots = IntersectSpherePair(origin, direction, info.cloudCurvatureRadius + info.cirrusAltitude);
	const float distance = roots.x >= 0.0 ? roots.x : roots.y;
	if (distance <= 0.0)
		return incoming;
	const float2 ground = IntersectSpherePair(origin, direction, info.cloudCurvatureRadius);
	if (ground.x >= 0.0 && ground.x < distance)
		return incoming;
	float3 radiance = 0.0;
	float transmission = 1.0, transport = 1.0, depth = 0.0, weight = 0.0, cirrus = 0.0;
	IntegrateCirrus(position + direction * distance, direction, distance, CloudCirrusPhase(dot(direction, info.dirlightDir)), MakeCloudAmbient(),
		radiance, transmission, transport, depth, weight, cirrus);
	return radiance + transmission * incoming;
}

// Lighting consumes optical properties, not weather textures or shape noises.
// Keep the phase, solar spectrum and scattering calibration shared with low cloud.
float3 MiddleDirectSource(float3 position, float3 direction, float extinction, float height, float lightDepth, float viewDepth, float opacity)
{
	NDFInfo material;
	initNDFInfo(material);
	material.height_fraction = height;
	material.altitude = VolumetricCloudBuffer[0].middleLayer.xy;
	return ShadeCloudLighting(position, material, extinction, viewDepth, opacity,
		CloudLowPhase(dot(direction, VolumetricCloudBuffer[0].dirlightDir)), lightDepth, CloudMultipleScattering(extinction, material),
		exp(-CirrusSunDepth(position) * VolumetricCloudBuffer[0].sunExtinction));
}

float3 MiddleDirectSource(float3 position, float3 direction, float4 materialSamples, float lightDepth, float viewDepth, float opacity)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	NDFInfo material;
	initNDFInfo(material);
	material.altitude = info.middleLayer.xy;
	const CloudMsProfile calibration = PrepareCloudMsProfile(material);
	const float phase = CloudLowPhase(dot(direction, info.dirlightDir));
	const float cirrus = exp(-CirrusSunDepth(position) * info.sunExtinction);
	float3 source = 0.0;
	[unroll] for (uint i = 0u; i < 2u; ++i)
	{
		material.height_fraction = materialSamples[i + 2u];
		const float extinction = materialSamples[i];
		source += ShadeCloudLighting(position, material, extinction, viewDepth, opacity,
			phase, lightDepth, CloudMultipleScattering(extinction, material, calibration), cirrus);
	}
	return 0.5 * source;
}

float3 MiddleBoundaryRadiance(float3 position, float3 direction, float2 opticalDepth)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	const float lightDepth = MiddleSelfShadowDepth(position, info.dirlightDir) * info.sunExtinction;
	const float height = direction.z > 0.0 ? 0.0 : 1.0;
	const float3 solar = MiddleDirectSource(position, direction, info.middleLayer.z, height, lightDepth, 32.0, 1.0);
	return solar + info.cloudLightingScale * info.ambientStrength * CloudAmbientPhase.y * CloudAmbientTransport::MiddleSource(position, opticalDepth);
}

float MiddleViewStepSize(float3 direction, float3 origin, float2 interval)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	const float slope = max(abs(direction.z + 2.0 * dot(origin.xy + direction.xy * interval.x, direction.xy) / info.cloudCurvatureRadius), 0.1);
	const float verticalStep = (info.middleLayer.y - info.middleLayer.x) / (16.0 * slope);
	const float horizontalStep = 2.0 / max(512.0 * info.middleLayer.w * length(direction.xy), 1e-8);
	return max(min(verticalStep, horizontalStep), max((interval.y - interval.x) / 64.0, GAME_UNITS_PER_METER));
}

// Earliest possible material on the same shell/scene intervals as the march.
// Weather bounds only reject proven empty space; noise can never enlarge it.
float UpperCloudFirstDistance(float3 eye, float3 direction, float sceneDistance)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	const float3 origin = float3(eye.xy - FrameBuffer::CameraPosAdjust.xy, eye.z);
	CloudRaySegments segments = (CloudRaySegments)0;
	bool middle = false;
	float first = 1e30;
	if (info.middleLayer.z > 0.0) {
		VolumetricCloudData middleInfo = info;
		middleInfo.rayMarchRange = sceneDistance > 0.0 ? sceneDistance : 1e30;
		const float2 ground = IntersectSpherePair(origin + float3(0, 0, info.planetRadius), direction, info.planetRadius);
		if (ground.x >= 0.0 && ground.y > 0.0)
			middleInfo.rayMarchRange = min(middleInfo.rayMarchRange, ground.x);
		segments = GetCloudRaySegments(origin, direction, info.middleLayer.x, info.middleLayer.y, middleInfo);
		if (segments.nearSegment.y > segments.nearSegment.x && !MiddleIntervalEmpty(eye + direction * segments.nearSegment.x, eye + direction * segments.nearSegment.y)) {
			middle = true;
			first = min(first, segments.nearSegment.x);
		}
		if (segments.farSegment.y > segments.farSegment.x && !MiddleIntervalEmpty(eye + direction * segments.farSegment.x, eye + direction * segments.farSegment.y)) {
			middle = true;
			first = min(first, segments.farSegment.x);
		}
	}
	bool cirrus = false;
	if (info.cirrusEnabled != 0u) {
		const float2 roots = IntersectSpherePair(origin + float3(0, 0, info.cloudCurvatureRadius), direction, info.cloudCurvatureRadius + info.cirrusAltitude);
		const float sheet = roots.x >= 0.0 ? roots.x : roots.y;
		cirrus = sheet >= 0.0 && (sceneDistance <= 0.0 || sheet < sceneDistance);
		if (cirrus)
			first = min(first, sheet);
	}
	if (!middle && !cirrus)
		return 1e30;
	return first;
}

bool UpperCloudOccluded(float firstDistance, float foregroundDepth)
{
	const float encoded = EncodeCloudDepth(firstDistance);
	return foregroundDepth < encoded - max(encoded * 0.002, EncodeCloudDepth(GAME_UNITS_PER_METER));
}

VolumetricCloudResult RenderUpperCloudRay(float3 direction, float3 eye, float sceneDistance, float jitter, float angularFootprint = 0.0, float2 raySegment = float2(0.0, 1.0))
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	VolumetricCloudResult result = (VolumetricCloudResult)0;
	result.transmittance = 1.0;
	result.cloud_depth = info.rayMarchRange;
#ifdef CLOUD_REFLECTION
	result.rawTransmission = 1.0;
#endif
	const float3 origin = float3(eye.xy - FrameBuffer::CameraPosAdjust.xy, eye.z);
	CloudRaySegments segments = (CloudRaySegments)0;
	if (info.middleLayer.z > 0.0) {
		// Middle clouds can first enter the ray beyond the entire low-cloud
		// range. Trace their own shell, stopping at scene/planet occlusion.
		VolumetricCloudData middleInfo = info;
		middleInfo.rayMarchRange = sceneDistance > 0.0 ? sceneDistance : 1e30;
		const float2 ground = IntersectSpherePair(origin + float3(0, 0, info.planetRadius), direction, info.planetRadius);
		if (ground.x >= 0.0 && ground.y > 0.0)
			middleInfo.rayMarchRange = min(middleInfo.rayMarchRange, ground.x);
		segments = GetCloudRaySegments(origin, direction, info.middleLayer.x, info.middleLayer.y, middleInfo);
	}
	float sheetDistance = -1.0;
	if (info.cirrusEnabled != 0u) {
		const float2 roots = IntersectSpherePair(origin + float3(0, 0, info.cloudCurvatureRadius), direction, info.cloudCurvatureRadius + info.cirrusAltitude);
		sheetDistance = roots.x >= 0.0 ? roots.x : roots.y;
		if (sceneDistance > 0.0 && sheetDistance >= sceneDistance)
			sheetDistance = -1.0;
	}
#ifdef CLOUD_REFLECTION
	// Distribute the existing macro steps, rather than giving every lane a new
	// full march. Keep the near and far shell pieces in front-to-back order.
	const float2 steps = float2(MiddleViewStepSize(direction, origin, segments.nearSegment), MiddleViewStepSize(direction, origin, segments.farSegment));
	const float2 counts = ceil(max(float2(segments.nearSegment.y - segments.nearSegment.x, segments.farSegment.y - segments.farSegment.x), 0.0) / steps);
	const float2 indices = floor(raySegment * (counts.x + counts.y));
	const float2 mapped = indices <= counts.x ? min(segments.nearSegment.xx + indices * steps.x, segments.nearSegment.yy) :
	                                            min(segments.farSegment.xx + (indices - counts.x) * steps.y, segments.farSegment.yy);
	raySegment = float2(raySegment.x == 0.0 ? 0.0 : mapped.x, raySegment.y == 1.0 ? 1e30 : mapped.y);
	if (sheetDistance < raySegment.x || sheetDistance >= raySegment.y)
		sheetDistance = -1.0;
	segments.nearSegment = float2(max(segments.nearSegment.x, raySegment.x), min(segments.nearSegment.y, raySegment.y));
	segments.farSegment = float2(max(segments.farSegment.x, raySegment.x), min(segments.farSegment.y, raySegment.y));
#endif
	const CloudAmbient ambient = MakeCloudAmbient();
	const float cirrusPhase = CloudCirrusPhase(dot(direction, info.dirlightDir));
	float transmission = 1.0, transport = 1.0;
	float depthSum = 0.0, weightSum = 0.0, cirrusWeight = 0.0;
	[loop] for (uint piece = 0u; piece < 2u; ++piece)
	{
		const float2 interval = piece == 0u ? segments.nearSegment : segments.farSegment;
		float distance = interval.x;
		const float end = interval.y;
		// Keep lighting on macro steps. Two noise taps reconstruct erosion in
		// the local profile without doubling the self-shadow queries.
#ifdef CLOUD_REFLECTION
		const float stepSize = steps[piece];
		const float firstIndex = piece == 0u ? 0.0 : counts.x;
		const float2 ownedSteps = clamp(indices - firstIndex, 0.0, counts[piece]);
		uint remainingSteps = uint(ownedSteps.y - ownedSteps.x);
#else
		const float stepSize = MiddleViewStepSize(direction, origin, interval);
#endif
		float4 startColumn = 0.0;
		if (distance < end)
			startColumn = MiddleCloudColumn((eye + direction * distance).xy, distance * angularFootprint, 0.5 * stepSize * length(direction.xy));
		[loop] while (distance < end && max(transmission, info.ambientStrength > 0.0 ? transport : transmission) > 0.1)
		{
			if (sheetDistance >= 0.0 && sheetDistance <= distance) {
#ifdef CLOUD_REFLECTION
				IntegrateReflectionCirrus(eye + direction * sheetDistance, direction, sheetDistance, cirrusPhase, ambient,
					result, transmission, transport, depthSum, weightSum, cirrusWeight);
#else
				IntegrateCirrus(eye + direction * sheetDistance, direction, sheetDistance, cirrusPhase, ambient,
					result.lum, transmission, transport, depthSum, weightSum, cirrusWeight);
#endif
				sheetDistance = -1.0;
			}
#ifdef CLOUD_REFLECTION
			// End the last assigned step exactly at its shared boundary. An ULP
			// tail would add another density-weighted depth sample despite having
			// negligible optical thickness, shifting the layer-composition depth.
			float stepSizeHere = remainingSteps == 1u ? end - distance : min(stepSize, end - distance);
			const bool splitAtSheet = sheetDistance > distance && sheetDistance < distance + stepSizeHere;
#else
			float stepSizeHere = min(stepSize, end - distance);
#endif
			if (sheetDistance > distance)
				stepSizeHere = min(stepSizeHere, sheetDistance - distance);
			float3 position = eye + direction * (distance + jitter * stepSizeHere);
			float4 materialSamples;
			float2 depth;
			const float density = MiddleCloudSegment(position, eye + direction * distance, eye + direction * (distance + stepSizeHere), startColumn, materialSamples, depth,
				(distance + 0.5 * stepSizeHere) * angularFootprint);
			if (density > 0.0) {
				const float sampleDistance = dot(position - eye, direction);
				const float viewDepth = density * stepSizeHere * GAME_UNIT_TO_M * info.viewExtinction;
				const float opacity = CloudOpacity(viewDepth);
				const float lightDepth = MiddleSelfShadowDepth(position, info.dirlightDir) * info.sunExtinction;
				result.lum += transmission * MiddleDirectSource(position, direction, materialSamples, lightDepth, viewDepth, opacity);
				const float reducedDepth = viewDepth * CloudAmbientPhase.x;
				if (info.ambientStrength > 0.0)
#ifdef CLOUD_REFLECTION
					result.transportLum += transport * CloudOpacity(reducedDepth) * CloudAmbientPhase.y * info.ambientStrength * info.cloudLightingScale * CloudAmbientTransport::MiddleSource(position, depth);
#else
					result.lum += transport * CloudOpacity(reducedDepth) * CloudAmbientPhase.y * info.ambientStrength * info.cloudLightingScale * CloudAmbientTransport::MiddleSource(position, depth);
#endif
				const float weight = CloudDepthWeight(transmission, density, sampleDistance);
				depthSum += weight * sampleDistance;
				weightSum += weight;
				result.stormMotion += weight * float4(MiddleCloudDisplacement(position), 1.0);
				transmission *= exp(-viewDepth);
				transport *= exp(-reducedDepth);
			}
#ifdef CLOUD_REFLECTION
			distance = splitAtSheet ? sheetDistance : (remainingSteps == 1u ? end : distance + stepSizeHere);
			remainingSteps -= splitAtSheet ? 0u : 1u;
#else
			distance += stepSizeHere;
#endif
		}
	}
	if (sheetDistance >= 0.0)
#ifdef CLOUD_REFLECTION
		IntegrateReflectionCirrus(eye + direction * sheetDistance, direction, sheetDistance, cirrusPhase, ambient,
			result, transmission, transport, depthSum, weightSum, cirrusWeight);
	result.rawTransmission = float2(transmission, info.ambientStrength > 0.0 ? transport : transmission);
	result.integralWeights = float4(depthSum, weightSum, cirrusWeight, 0.0);
#else
		IntegrateCirrus(eye + direction * sheetDistance, direction, sheetDistance, cirrusPhase, ambient,
			result.lum, transmission, transport, depthSum, weightSum, cirrusWeight);
	const float raw = CloudBackgroundTransmittance(transmission, info.ambientStrength > 0.0 ? transport : transmission);
	result.transmittance = saturate((raw - 0.1) / 0.9);
	result.lum /= 1.0 - min(raw, 0.1);
	if (weightSum > 0.0) {
		result.cloud_depth = depthSum / weightSum;
		result.stormMotion *= (1.0 - result.transmittance.x) / weightSum;
		result.motionMetadata.x = saturate(cirrusWeight / weightSum);
		if (!ViewAtmosphere::Enabled()) {
			const float4 air = SampleCloudAerialPerspective(direction, result.cloud_depth, 0.0);
			result.lum = result.lum * air.a + air.rgb * (1.0 - result.transmittance);
		}
	}
#endif
	return result;
}

// Shared layer composition for primary view and reflections. Integral centroids
// follow the existing voxel composition convention at intersecting layer edges.
void CompositeUpperCloud(VolumetricCloudResult upper, inout float transmission, inout float4 color, inout float4 metadata, inout float4 motion, inout float2 lightning)
{
	const float upperDepth = EncodeCloudDepth(upper.cloud_depth);
	const float opacity = 1.0 - upper.transmittance.x;
	float front = 1.0;
	if (opacity > 0.0 && transmission < 1.0) {
		const float scale = max(max(metadata.x, upperDepth) * 0.002, EncodeCloudDepth(GAME_UNITS_PER_METER));
		front = smoothstep(-scale, scale, metadata.x - upperDepth);
	}
	const float upperVisibility = lerp(transmission, 1.0, front);
	const float lowerVisibility = lerp(1.0, upper.transmittance.x, front);
	const float upperWeight = opacity * upperVisibility;
	const float lowerWeight = (1.0 - transmission) * lowerVisibility;
	const float weight = upperWeight + lowerWeight;
	if (weight > 0.0)
		metadata.x = (upperWeight * upperDepth + lowerWeight * metadata.x) / weight;
	metadata.zw = metadata.zw * lowerVisibility + upper.motionMetadata * upperWeight;
	motion = motion * lowerVisibility + upper.stormMotion * upperVisibility;
	lightning *= lowerVisibility;
	transmission *= upper.transmittance.x;
	color = float4(min(color.rgb * lowerVisibility + upper.lum * upperVisibility, 65504.0), 1.0 - transmission);
}

#endif
