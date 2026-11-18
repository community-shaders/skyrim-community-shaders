#ifndef PHYSICAL_SKY_CLOUD_STORM_HLSLI
#define PHYSICAL_SKY_CLOUD_STORM_HLSLI

float2 StormRotate(float2 value, float angle)
{
	float sine, cosine;
	sincos(angle, sine, cosine);
	return float2(cosine * value.x - sine * value.y, sine * value.x + cosine * value.y);
}

bool IsVolcanic(CloudStormData storm)
{
#ifdef VOLCANIC_STORMS_ONLY
	return true;
#else
	return ((uint)storm.generation.w & 4u) != 0u;
#endif
}

float StormHeight(float z, CloudStormData storm)
{
	return saturate((z - storm.altitudeMotion.x) / max(storm.altitudeMotion.y, 1.0));
}

float3 PlumeRotate(float3 p, float4 q)
{
	return p + 2.0 * cross(q.xyz, cross(q.xyz, p) + q.w * p);
}

// A continuously swept radius, rather than isolated spherical lobes. Only the
// envelope follows this path; the noise fields below remain rigid and isotropic.
float VolcanoCoreProfile(float3 position, uint index, out float coordinate, out float2 envelope)
{
	envelope = float2(1e30, 0.0);
	float weightSum = 0.0;
	coordinate = 0.0;
	// A weak crosswind folds the outflow back over the rising column. Nearest
	// segment ownership would jump across their bisector, cutting density and
	// motion abruptly. Blend path attributes over a fixed physical width instead.
	const float width = max(CloudStorms[index].centerRadiusStrength.z * .11, 1.0);
	const float widthSquared = width * width;
	[unroll] for (uint i = 0u; i < 7u; ++i)
	{
		const float4 a = CloudStorms[index].parcels[i].sphere;
		const float4 b = CloudStorms[index].parcels[i + 1u].sphere;
		const float3 axis = b.xyz - a.xyz;
		const float along = saturate(dot(position - a.xyz, axis) / max(dot(axis, axis), 1e-6));
		float3 delta = position - lerp(a.xyz, b.xyz, along);
		const float pathDistanceSquared = dot(delta, delta);
		// The outflow spreads horizontally while its vertical envelope thins.
		// This does not scale the noise coordinates or their local motion.
		delta.z /= lerp(1.0, .45, saturate((i + along - 4.0) / 3.0));
		const float distanceSquared = dot(delta, delta);
		const float radius = lerp(a.w, b.w, along);
		// Signed squared-distance approximation, in metres near the surface.
		// Keep a conservative halo: Alligator may add lobes outside this carrier.
		const float distance = (distanceSquared - radius * radius) / (2.0 * max(radius, 1.0));
		envelope.x = min(envelope.x, distance);
		float weight = widthSquared / (widthSquared + pathDistanceSquared);
		weight *= weight;
		weight *= weight;
		weightSum += weight;
		coordinate += weight * (i + along);
		envelope.y += weight * min(radius * .55, 220.0);
	}
	const float normalization = rcp(max(weightSum, 1e-20));
	coordinate = clamp(coordinate * normalization, 0.0, 7.0);
	// The minimum signed distance is continuous; its winning segment's radius
	// is not. Blend amplitude too, so it cannot reintroduce a displaced seam.
	envelope.y *= normalization;
	return envelope.x < 200.0 ? .95 : 0.0;
}

float3 PlumeNoisePosition(float3 position, uint index, uint parcelIndex)
{
	const CloudStormParcel parcel = CloudStorms[index].parcels[parcelIndex];
	return PlumeRotate(position - parcel.sphere.xyz, parcel.rotation) - parcel.noiseOffset.xyz;
}

float3 PlumeDisplacement(float3 position, uint index, uint parcelIndex)
{
	const CloudStormParcel parcel = CloudStorms[index].parcels[parcelIndex];
	const float3 field = PlumeRotate(position - parcel.sphere.xyz, parcel.rotation) - parcel.noiseOffset.xyz;
	const float4 inversePrevious = float4(-parcel.previousRotation.xyz, parcel.previousRotation.w);
	return position - (PlumeRotate(field + parcel.previousNoiseOffset.xyz, inversePrevious) + parcel.previousSphere.xyz);
}

// Mesocyclone bands: faster, tighter noise toward the vortex centre.
void StormRingParameters(float radius, uint band, out float bend, out float speed, out float2 frequency)
{
	if (band <= 1u) {
		bend = (1.0 - saturate(radius * 8.205128)) * (band == 0u ? 180.0 : 80.0);
		speed = band == 0u ? 7.875 : 2.8;
		frequency = band == 0u ? float2(0.00544314, 0.0038101978) : float2(0.00453595, 0.0031751648);
	} else {
		const float ramp = band == 2u ? 1.0 - saturate((radius - 0.121875) * 5.8608065) :
		                                1.0 - saturate((radius - 0.2925) * 3.3333335);
		bend = ramp * ramp * (band == 2u ? 120.0 : 20.0);
		speed = band == 2u ? 0.8 : (band == 3u ? 0.2 : 0.1);
		frequency = float2(0.0036287599, 0.0025401318);
	}
}

void StormRings(float radius, float bandRadius, out uint first, out uint second, out float blend)
{
	first = 0u;
	second = 0u;
	blend = 0.0;
	if (bandRadius < 0.08125)
		return;
	if (bandRadius < 0.121875) {
		second = 1u;
		blend = pow(saturate((radius - 0.08125) * 24.615381), 0.3);
	} else if (bandRadius < 0.203125)
		first = second = 1u;
	else if (bandRadius < 0.324375) {
		first = 1u;
		second = 2u;
		blend = pow(saturate((radius - 0.203125) * 8.247422), 0.3);
	} else if (bandRadius < 0.4)
		first = second = 2u;
	else if (bandRadius < 0.55) {
		first = 2u;
		second = 3u;
		blend = pow(saturate((radius - 0.4) * 6.6666665), 0.3);
	} else if (bandRadius < 0.63)
		first = second = 3u;
	else if (bandRadius < 0.8) {
		first = 3u;
		second = 4u;
		blend = pow(saturate((radius - 0.63) * 5.8823524), 0.3);
	} else
		first = second = 4u;
}

float4 StormWarpedNoise(float2 xy, float z, float2 frequency, float height, float mip)
{
	const float fade = saturate((height - 0.02) * 33.333332);
	const float2 warp = TexCloudAdjustmentLUT.SampleLevel(TileableSampler, xy * 0.004, 0).gb * 2.0 - 1.0;
	return TexCloudShapeNoise.SampleLevel(TileableSampler,
		float3(xy * frequency.x + warp * (0.125 * (1.0 - fade)), (z - 10.0 + fade * 10.0) * frequency.y), mip);
}

float4 StormRingNoise(float3 position, float radius, uint band, float height, float mip, CloudStormData storm)
{
	float bend, speed;
	float2 frequency;
	StormRingParameters(radius, band, bend, speed, frequency);
	const float angle = -(bend + storm.animation.x * speed * storm.altitudeMotion.z) * 0.01745329252;
	const float2 xy = storm.centerRadiusStrength.xy + StormRotate(position.xy - storm.centerRadiusStrength.xy, angle);
	const float4 noise = StormWarpedNoise(xy, position.z, frequency, height, mip);
	return band == 0u ? pow(noise, 0.8) : noise;
}

float4 SampleCloudStormNoise(float3 positionMeters, float height, float mip, uint index, NDFInfo ndf)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	const CloudStormData storm = CloudStorms[index];
#ifndef NO_CLOUD_STORMS
	[branch] if (IsVolcanic(storm))
	{
		const uint firstIndex = min((uint)ndf.plumeCoordinate, 6u);
		const float blend = smoothstep(0.0, 1.0, saturate(ndf.plumeCoordinate - firstIndex));
		// Evaluate each rigid field immediately, keeping temporary coordinates dead.
		const float4 a = TexCloudShapeNoise.SampleLevel(TileableSampler, PlumeNoisePosition(positionMeters, index, firstIndex) * .0010, mip);
		const float4 b = TexCloudShapeNoise.SampleLevel(TileableSampler, PlumeNoisePosition(positionMeters, index, firstIndex + 1u) * .0010, mip);
		float4 noise = lerp(a, b, blend);
		// G sculpts the large carrier lobes; B supplies independent, smaller
		// billows, and A retains the finest erosion without another texture fetch.
		noise.r = lerp(noise.r, sqrt(noise.b), 0.65);
		return noise;
	}
#endif
	// Rotate noise in world XY. The instance's height-dependent NDF displacement must
	// not bend the noise field and stretch its billows along the plume.
	const float2 relative = positionMeters.xy - storm.centerRadiusStrength.xy;
	const float radius = length(relative) / (storm.centerRadiusStrength.z * storm.centerRadiusStrength.w);
	const float2 seed = frac(positionMeters.xy * 0.1031);
	const float scramble = dot(seed.xyx, seed.yxx + 19.19);
	const float jitter = frac((seed.y + seed.x + scramble * 2.0) * (scramble + seed.x));
	const float bandRadius = radius + jitter * 0.01;
	const float2 advectedXY = positionMeters.xy - info.noiseWindOffset * GAME_UNIT_TO_M;
	float4 result = 0.0;
	[branch] if (bandRadius < 0.9)
	{
		// The anvil uses rising coarse billows rather than the lower vortex's fine bands.
		[branch] if (height > 0.6 + jitter * 0.02)
		{
			const float z = positionMeters.z - storm.animation.x * storm.altitudeMotion.w;
			if (radius < 0.4)
				result = StormWarpedNoise(advectedXY, z - 10.0, float2(0.0029030081, 0.0023222708), height, mip);
			else {
				const float exponent = 2.0 - saturate((height - 0.7) * 3.3333333) * 1.5;
				const float remaining = 1.0 - saturate(radius);
				const float angle = -(remaining * remaining * 100.0 + storm.animation.x * 0.2 * storm.altitudeMotion.z) * 0.01745329252;
				const float2 xy = storm.centerRadiusStrength.xy + StormRotate(relative, angle);
				const float4 rotated = pow(StormWarpedNoise(xy, z, float2(0.001, 0.00070000003), height, mip), exponent);
				result = rotated;
				if (radius < 0.55) {
					const float4 centre = StormWarpedNoise(advectedXY, z - 10.0, float2(0.0029030081, 0.0023222708), height, mip);
					result = lerp(centre, rotated, saturate((radius - 0.4) * 6.6666665));
				}
			}
		}
		else
		{
			uint first, second;
			float blend;
			StormRings(radius, bandRadius, first, second, blend);
			result = StormRingNoise(positionMeters, radius, first, height, mip, storm);
			[branch] if (first != second)
				result = lerp(result, StormRingNoise(positionMeters, radius, second, height, mip, storm), blend);
			if (bandRadius >= 0.85) {
				const float4 ordinary = StormWarpedNoise(advectedXY, positionMeters.z - 10.0, float2(0.0043545123, 0.0034834063), height, mip);
				result = lerp(result, ordinary, pow(saturate((radius - 0.85) * 20.00002), 0.3));
			}
		}
	}
	else result = StormWarpedNoise(advectedXY, positionMeters.z - 10.0, float2(0.0043545123, 0.0034834063), height, mip);
	return result;
}

void FinalizeStormNdf(inout NDFInfo ndf, CloudStormData storm)
{
	if (!IsVolcanic(storm) && ndf.storm > 0.15) {
		const float crownCoverage = pow(saturate((ndf.storm - 0.1) * 1.1111112), 1.3);
		const float topBlend = saturate((ndf.height_fraction - 0.5) * 3.3333333);
		ndf.coverage = lerp(ndf.coverage, crownCoverage * storm.centerRadiusStrength.w, topBlend);
		ndf.top_type = lerp(ndf.top_type, crownCoverage, topBlend);
	}
	ndf.top_type *= saturate(ndf.coverage * 10.0);
	ndf.in_layer = ndf.coverage > 1e-8;
	ndf.stormVolume = true;
	ndf.dimension_profile = CloudNdfProfile(ndf);
}

NDFInfo SampleVolcanicNdf(float3 positionMeters, uint index)
{
	const CloudStormData storm = CloudStorms[index];
	NDFInfo ndf;
	initNDFInfo(ndf);
#ifndef NO_CLOUD_STORMS
	[branch] if (IsVolcanic(storm))
	{
		if (any(positionMeters < storm.plumeLower.xyz) || any(positionMeters > storm.plumeUpper.xyz))
			return ndf;
		const float profile = VolcanoCoreProfile(positionMeters, index, ndf.plumeCoordinate, ndf.plumeEnvelope);
		if (profile <= 0.0)
			return ndf;
		const uint first = min((uint)ndf.plumeCoordinate, 6u);
		const float blend = smoothstep(0.0, 1.0, saturate(ndf.plumeCoordinate - first));
		const float3 centre = lerp(CloudStorms[index].parcels[first].sphere.xyz, CloudStorms[index].parcels[first + 1u].sphere.xyz, blend);
		// Transport the authored modeling with the local section. The old fixed
		// Height footprint must not slice off a wind-displaced or spreading parcel.
		const float3 uv = float3((positionMeters.xy - centre.xy) / (2.0 * storm.animation.z) + .5, index);
		const float4 model = saturate(TexStormModeling.SampleLevel(TransmittanceSampler, uv, 0));
		ndf.altitude = float2(storm.altitudeMotion.x, storm.altitudeMotion.x + storm.altitudeMotion.y) * GAME_UNITS_PER_METER;
		ndf.height_fraction = StormHeight(positionMeters.z, storm);
		ndf.coverage = model.r;
		ndf.top_type = model.g * saturate(model.r * 10.0);
		ndf.bottom_type = model.b;
		ndf.rawStorm = model.a;
		ndf.storm = model.a;
		ndf.stormVolume = true;
		ndf.stormIndex = index;
		ndf.stormBlend = 1.0;
		ndf.plumeDensity = lerp(CloudStorms[index].parcels[first].flow.w, CloudStorms[index].parcels[first + 1u].flow.w, blend);
		const float age = saturate((ndf.plumeCoordinate - 4.0) / 3.0);
		ndf.dimension_profile = profile * lerp(1.0, .55, age);
		ndf.dimension_profile *= smoothstep(0.0, .025, ndf.height_fraction);
		ndf.in_layer = ndf.dimension_profile > 0.0;
		// Ownership follows the un-eroded three-dimensional carrier, not sampled
		// density. Blend across its surface without clearing the whole XY column.
		ndf.replacement = storm.centerRadiusStrength.w * (1.0 - smoothstep(-90.0, 90.0, ndf.plumeEnvelope.x)) * smoothstep(0.0, .025, ndf.height_fraction);
		return ndf;
	}
#endif
	return ndf;
}

// Supercell replaces the weather NDF before height clipping, profile evaluation
// and erosion. A clear part of the resulting profile must stay clear.
float sampleSupercellWeatherDensity(float3 pos, CloudLayer cloud, float mipBias, float viewDistance, float minimumMip,
	out CloudDensityContext context)
{
#if defined(NO_CLOUD_STORMS) || defined(VOLCANIC_STORMS_ONLY)
	return sampleWeatherCloudDensity(pos, cloud, mipBias, viewDistance, minimumMip, context);
#else
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	NDFInfo ndf;
	initNDFInfo(ndf);
	bool applied = false;
	float influence = 0.0;
	// Match instance priority: earlier names overlay later names.
	[loop] for (uint remaining = CloudStormCount(); remaining > 0u; --remaining)
	{
		const uint index = remaining - 1u;
		const CloudStormData storm = CloudStorms[index];
		if (IsVolcanic(storm))
			continue;
		const float2 relative = pos.xy * GAME_UNIT_TO_M - storm.centerRadiusStrength.xy;
		if (any(abs(relative) >= storm.animation.z))
			continue;
		const float3 uv = float3(relative / (2.0 * storm.animation.z) + .5, index);
		const float4 bounds = TexStormHeight.SampleLevel(TransmittanceSampler, uv, 0);
		const float4 model = TexStormModeling.SampleLevel(TransmittanceSampler, uv, 0);
		const bool authored = ((uint)storm.generation.w & 2u) != 0u;
		const float strength = storm.centerRadiusStrength.w;
		const float backResidual = 1.0 - strength * bounds.z;
		// Height, top and bottom retain distinct 7/3/10 influence ramps.
		const float3 coreWeights = strength * saturate(model.a * float3(7.0, 3.0, 10.0));
		const float3 residual = authored ? 1.0 - bounds.z : backResidual * (1.0 - coreWeights);
		const float coverageResidual = authored ? residual.x : backResidual * lerp(1.0 - coreWeights.z, 1.0, bounds.z);
		const float weight = 1.0 - min(coverageResidual, min(residual.x, min(residual.y, residual.z)));
		if (weight <= 0.0)
			continue;
		if (!applied) {
			const float2 field = CloudFieldPosition((pos.xy - info.noiseWindOffset) * GAME_UNIT_TO_M, info.cloudEvolution.xy) * GAME_UNITS_PER_METER;
			const float3 heights = SampleCloudHeights(field, pos.xy);
			// Weather heights use the curved layer; instance heights are local Z.
			ndf.altitude = heights.xy - CloudCurvatureOffset(pos.xy - FrameBuffer::CameraPosAdjust.xy, info.cloudCurvatureRadius);
			ndf.shaping_start = heights.z;
			// Height auxiliary is authored before supercell composition and survives
			// full replacement. The weather Modeling fetch can still be skipped.
			if (coverageResidual > 0.0 || residual.y > 0.0 || residual.z > 0.0) {
				const float3 weather = ApplyLocalNdfModel(saturate(TexCloudModeling.SampleLevel(TileableSampler, LowNdfUV(field, info), 0)), SampleLocalNdfModel(pos.xy, info));
				ndf.coverage = weather.r;
				ndf.top_type = weather.g;
				ndf.bottom_type = weather.b;
			}
		}
		const float2 weatherHeight = (ndf.altitude * GAME_UNIT_TO_M - storm.altitudeMotion.x) / storm.altitudeMotion.y;
		// Clamp the final normalized height map. With independent instance
		// slabs, retain the old layer's range only while its height has weight.
		const float lower = residual.x > 0.0 ? min(weatherHeight.x, 0.0) : 0.0;
		const float upper = residual.x > 0.0 ? max(weatherHeight.y, 1.0) : 1.0;
		const float2 height = clamp(bounds.xy + weatherHeight * residual.x, lower, upper);
		ndf.altitude = (storm.altitudeMotion.x + height * storm.altitudeMotion.y) * GAME_UNITS_PER_METER;
		ndf.top_type = model.g + ndf.top_type * residual.y;
		ndf.bottom_type = model.b + ndf.bottom_type * residual.z;
		if (authored)
			ndf.coverage = model.r + ndf.coverage * coverageResidual;
		else {
			const float backCoverage = ndf.coverage * backResidual + bounds.w;
			ndf.coverage = lerp(backCoverage * (1.0 - coreWeights.z) + model.r, max(backCoverage, model.r), bounds.z);
		}
		ndf.rawStorm = max(ndf.rawStorm, model.a);
		ndf.storm = ndf.rawStorm;
		ndf.stormIndex = index;
		influence = lerp(influence, 1.0, weight);
		applied = true;
	}
	if (!applied)
		return sampleWeatherCloudDensity(pos, cloud, mipBias, viewDistance, minimumMip, context);
	ndf.stormVolume = true;
	ndf.stormBlend = influence;
	context = (CloudDensityContext)0;
	context.ndf = ndf;
	// Clip only AFTER replacement: never fall back to weather outside these heights.
	if (ndf.altitude.y <= ndf.altitude.x || pos.z <= ndf.altitude.x || pos.z >= ndf.altitude.y)
		return 0.0;
	ndf.height_fraction = saturate((pos.z - ndf.altitude.x) / (ndf.altitude.y - ndf.altitude.x));
	ndf.coverage = saturate(ndf.coverage);
	ndf.top_type = saturate(ndf.top_type);
	ndf.bottom_type = saturate(ndf.bottom_type);
	FinalizeStormNdf(ndf, CloudStorms[ndf.stormIndex]);
	if (((uint)CloudStorms[ndf.stormIndex].generation.w & 2u) != 0u)
		ndf.dimension_profile *= smoothstep(0.0, 0.3, min(ndf.height_fraction, 1.0 - ndf.height_fraction));
	context.ndf = ndf;
	context.noise_coordinates = pos * GAME_UNIT_TO_M;
	context.eye_distance = viewDistance;
	context.weather_height_fraction = ndf.height_fraction;
	return sampleCloudDensityFromContext(context, mipBias, minimumMip);
#endif
}

float3 CloudStormDisplacement(float3 worldPositionMeters, NDFInfo ndf);

float sampleCloudDensity(float3 pos, CloudLayer cloud, float mipBias, float viewDistance, float minimumMip,
	out CloudDensityContext context)
{
#ifdef NO_CLOUD_STORMS
	const float density = sampleWeatherCloudDensity(pos, cloud, mipBias, viewDistance, minimumMip, context);
	context.ndf.stormVolume = false;
	context.ndf.stormBlend = 0.0;
	return density;
#else
	context = (CloudDensityContext)0;
	float remaining = 1.0, result = 0.0, lightDensity = 0.0, dominantDensity = -1.0;
	float3 stormMotion = 0.0;
	// Instances are ordered overlays. Full replacement avoids evaluating the
	// background at all; partial replacement blends extinction, not its maximum.
	[loop] for (uint index = 0u; index < CloudStormCount(); ++index)
	{
		if (!IsVolcanic(CloudStorms[index]))
			continue;
		CloudDensityContext stormContext = (CloudDensityContext)0;
		stormContext.noise_coordinates = pos * GAME_UNIT_TO_M;
		stormContext.ndf = SampleVolcanicNdf(stormContext.noise_coordinates, index);
		if (!stormContext.ndf.in_layer)
			continue;
		stormContext.eye_distance = viewDistance;
		const float sampledDensity = sampleCloudDensityFromContext(stormContext, mipBias, minimumMip);
		const float strength = CloudStorms[index].centerRadiusStrength.w;
		// Rounded lobes can extend beyond the carrier. They also own weather,
		// but erosion cannot take ownership away from the carrier interior.
		stormContext.ndf.replacement = max(stormContext.ndf.replacement,
			strength * smoothstep(0.0, .1, stormContext.ndf.dimension_profile));
		const float weight = remaining * strength;
		const float contribution = weight * sampledDensity;
		result += contribution;
		if (contribution > 0.0)
			stormMotion += contribution * CloudStormDisplacement(pos * GAME_UNIT_TO_M, stormContext.ndf);
		lightDensity += weight * stormContext.light_density;
		// Keep one dominant storm for the per-instance scattering calibration.
		// The density blend itself does not switch when the densities cross.
		if (contribution > dominantDensity) {
			context = stormContext;
			dominantDensity = contribution;
		}
		remaining *= 1.0 - stormContext.ndf.replacement;
		if (remaining == 0.0)
			break;
	}
	float stormDensity = result;
	[branch] if (remaining > 0.0)
	{
		CloudDensityContext weatherContext;
		const float weatherDensity = remaining * sampleSupercellWeatherDensity(pos, cloud, mipBias, viewDistance, minimumMip, weatherContext);
		result += weatherDensity;
		lightDensity += remaining * weatherContext.light_density;
		const float supercellDensity = weatherDensity * weatherContext.ndf.stormBlend;
		if (supercellDensity > 0.0)
			stormMotion += supercellDensity * CloudStormDisplacement(pos * GAME_UNIT_TO_M, weatherContext.ndf);
		if (stormDensity <= 0.0 || supercellDensity > dominantDensity)
			context = weatherContext;
		stormDensity += supercellDensity;
		context.weather_height_fraction = weatherContext.weather_height_fraction;
	}
	context.ndf.stormBlend = result > 0.0 ? stormDensity / result : 0.0;
	context.storm_displacement = result > 0.0 ? stormMotion / result : 0.0;
	context.light_density = lightDensity;
	return result;
#endif
}

float2 PlumeRayInterval(float3 eye, float3 direction, uint index, float start, bool nearestOnly)
{
	const float3 origin = eye * GAME_UNIT_TO_M;
	// Parallel rays use a finite reciprocal; reject slabs they cannot enter.
	const bool3 parallel = abs(direction) < 1e-8;
	const float3 inverseDirection = 1.0 / (parallel ? 1e-8 : direction);
	float2 result = 0.0;
	[unroll] for (uint i = 0u; i < 7u; ++i)
	{
		const float4 a = CloudStorms[index].parcels[i].sphere;
		const float4 b = CloudStorms[index].parcels[i + 1u].sphere;
		// Linear centre/radius interpolation stays within these endpoint bounds.
		const float3 lower = min(a.xyz - a.w, b.xyz - b.w) - 200.0;
		const float3 upper = max(a.xyz + a.w, b.xyz + b.w) + 200.0;
		const float3 first = (lower - origin) * inverseDirection;
		const float3 second = (upper - origin) * inverseDirection;
		const float3 nearPlane = parallel ? -1e30 : min(first, second);
		const float3 farPlane = parallel ? 1e30 : max(first, second);
		const float2 segment = float2(max(start * GAME_UNIT_TO_M, max(nearPlane.x, max(nearPlane.y, nearPlane.z))), min(farPlane.x, min(farPlane.y, farPlane.z)));
		if (segment.y > segment.x && !any(parallel && (origin < lower || origin > upper))) {
			if (result.y <= result.x || (nearestOnly && segment.x < result.x))
				result = segment;
			else if (!nearestOnly || segment.x == result.x)
				result = float2(min(result.x, segment.x), max(result.y, segment.y));
		}
	}
	return result * GAME_UNITS_PER_METER;
}

// Replacement acts on the weather column even outside the storm's own slab.
// Include curved weather bounds for dispatch classification and ray intervals.
void SupercellInfluenceBounds(CloudStormData storm, out float3 lower, out float3 upper)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	const float extent = storm.animation.z;
	const float2 farXY = abs(storm.centerRadiusStrength.xy - FrameBuffer::CameraPosAdjust.xy * GAME_UNIT_TO_M) + extent;
	const float curvature = dot(farXY, farXY) / (info.cloudCurvatureRadius * GAME_UNIT_TO_M);
	lower = float3(storm.centerRadiusStrength.xy - extent, min(storm.altitudeMotion.x, info.lowCloudTraceBottomAltitude * GAME_UNIT_TO_M - curvature));
	upper = float3(storm.centerRadiusStrength.xy + extent, max(storm.altitudeMotion.x + storm.altitudeMotion.y, info.lowCloudTraceTopAltitude * GAME_UNIT_TO_M));
}

float2 CloudStormInstanceInterval(float3 eye, float3 direction, uint index)
{
	const CloudStormData storm = CloudStorms[index];
	if (IsVolcanic(storm))
		return PlumeRayInterval(eye, direction, index, 0.0, false);
	float3 lower, upper;
	SupercellInfluenceBounds(storm, lower, upper);
	float2 interval = float2(0.0, VolumetricCloudBuffer[0].rayMarchRange * GAME_UNIT_TO_M);
	const float3 origin = eye * GAME_UNIT_TO_M;
	[unroll] for (uint axis = 0u; axis < 3u; ++axis)
	{
		if (abs(direction[axis]) < 1e-8) {
			if (origin[axis] < lower[axis] || origin[axis] > upper[axis])
				return 0.0;
		} else {
			const float2 roots = (float2(lower[axis], upper[axis]) - origin[axis]) / direction[axis];
			interval.x = max(interval.x, min(roots.x, roots.y));
			interval.y = min(interval.y, max(roots.x, roots.y));
		}
	}
	return interval.y > interval.x ? interval * GAME_UNITS_PER_METER : 0.0;
}

float2 CloudStormRayInterval(float3 eye, float3 direction)
{
	float2 result = 0.0;
#ifndef NO_CLOUD_STORMS
	[loop] for (uint index = 0u; index < CloudStormCount(); ++index)
	{
		const float2 interval = CloudStormInstanceInterval(eye, direction, index);
		if (interval.y > interval.x) {
			result.x = result.y > result.x ? min(result.x, interval.x) : interval.x;
			result.y = max(result.y, interval.y);
		}
	}
#endif
	return result;
}

// Return the next occupied piece, rather than marching the empty space inside
// one box enclosing both the vent and the long downwind plume.
float2 CloudStormNextInterval(float3 eye, float3 direction, float start)
{
	float2 result = 0.0;
#ifndef NO_CLOUD_STORMS
	[loop] for (uint index = 0u; index < CloudStormCount(); ++index)
	{
		float2 segment = IsVolcanic(CloudStorms[index]) ? PlumeRayInterval(eye, direction, index, start, true) : CloudStormInstanceInterval(eye, direction, index);
		segment.x = max(segment.x, start);
		if (segment.y > segment.x) {
			if (result.y <= result.x || segment.x < result.x)
				result = segment;
			else if (segment.x == result.x)
				result.y = max(result.y, segment.y);
		}
	}
#endif
	return result;
}

float3 CloudStormDisplacement(float3 worldPositionMeters, NDFInfo ndf)
{
	float3 displacement = 0.0;
	const CloudStormData storm = CloudStorms[ndf.stormIndex];
	const float delta = storm.animation.x - storm.animation.y;
	if (delta == 0.0)
		return displacement;
#ifndef NO_CLOUD_STORMS
	if (IsVolcanic(storm)) {
		const uint first = min((uint)ndf.plumeCoordinate, 6u);
		const float blend = smoothstep(0.0, 1.0, saturate(ndf.plumeCoordinate - first));
		return lerp(PlumeDisplacement(worldPositionMeters, ndf.stormIndex, first),
				   PlumeDisplacement(worldPositionMeters, ndf.stormIndex, first + 1u), blend) *
		       GAME_UNITS_PER_METER;
	}
#endif

	const float2 relative = worldPositionMeters.xy - storm.centerRadiusStrength.xy;
	const float radius = length(relative) / (storm.centerRadiusStrength.z * storm.centerRadiusStrength.w);
	uint first, second;
	float blend, bend, firstSpeed, secondSpeed;
	float2 frequency;
	StormRings(radius, radius, first, second, blend);
	StormRingParameters(radius, first, bend, firstSpeed, frequency);
	StormRingParameters(radius, second, bend, secondSpeed, frequency);
	float spin = radius < 0.9 ? lerp(firstSpeed, secondSpeed, blend) : 0.0;
	const float crown = step(0.61, ndf.height_fraction) * (radius < 0.9 ? 1.0 : 0.0);
	const float outerCrown = saturate((radius - 0.4) * 6.6666665);
	spin = lerp(spin, 0.2 * outerCrown, crown);
	const float angle = -delta * spin * storm.altitudeMotion.z * 0.01745329252;
	displacement.xy = relative - StormRotate(relative, angle);
	displacement.z = crown * delta * storm.altitudeMotion.w;
	displacement *= GAME_UNITS_PER_METER;
	// The anvil centre and ordinary clouds follow the shared horizontal wind.
	const float windWeight = radius >= 0.9 ? 1.0 : crown * (1.0 - outerCrown);
	displacement.xy = lerp(displacement.xy, VolumetricCloudBuffer[0].cloudWindDelta, windWeight);
	return displacement;
}

#endif
