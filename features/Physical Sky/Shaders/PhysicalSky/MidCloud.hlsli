#ifndef PHYSICAL_SKY_MID_CLOUD_HLSLI
#define PHYSICAL_SKY_MID_CLOUD_HLSLI

#include "PhysicalSky/MidCloudBands.hlsli"

// Independent As/Ac weather. RGBA stores density and density-weighted
// bottom, top and type. Detail is evaluated from the shared mipmapped noises.
Texture2D<float4> TexMiddleWeather : register(t99);
Texture2D<float2> TexUpperSunDepth : register(t100);
Texture2D<float> TexMiddleWarp : register(t101);
Texture2D<float> TexMiddleCell : register(t103);

float2 UpperSunGridCenter()
{
	const float spacing = VolumetricCloudBuffer[0].rayMarchRange / 128.0;
	return floor(FrameBuffer::CameraPosAdjust.xy / spacing) * spacing;
}

float UpperSunTransmission(float3 position)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	[branch] if (info.middleLayer.z <= 0.0 && info.cirrusEnabled == 0u) return 1.0;
	const float2 uv = (position.xy - UpperSunGridCenter()) / (2.0 * info.rayMarchRange) + 0.5;
	const float2 depth = TexUpperSunDepth.SampleLevel(TransmittanceSampler, uv, 0);
	const float altitude = CloudProfileAltitude(position, FrameBuffer::CameraPosAdjust.xy, info.cloudCurvatureRadius);
	const float height = saturate((altitude - info.lowCloudTraceBottomAltitude) / max(info.lowCloudTraceTopAltitude - info.lowCloudTraceBottomAltitude, 1.0));
	return exp(-lerp(depth.x, depth.y, height) * info.sunExtinction);
}

// Integral of the same non-overlapping smoothstep edges used by density.
float MiddleProfileIntegral(float height, float softness)
{
	const float h = saturate(height);
	const float edge = saturate(min(h, 1.0 - h) / softness);
	const float integral = softness * edge * edge * edge * (1.0 - 0.5 * edge);
	return h < softness ? integral : (h > 1.0 - softness ? 1.0 - softness - integral : h - 0.5 * softness);
}

float MiddleProfile(float height, float softness)
{
	return smoothstep(0.0, softness, height) * (1.0 - smoothstep(1.0 - softness, 1.0, height));
}

float MiddleDetailNoise(Texture2D<float> noise, float2 uv, float footprint)
{
	uint width, height;
	noise.GetDimensions(width, height);
	const float mip = log2(max(footprint * max(width, height), 1.0));
	return saturate(noise.SampleLevel(TileableSampler, uv, mip));
}

float MiddleBandLattice(int2 cell)
{
	uint seed = uint(cell.x) * 1597334677u ^ uint(cell.y) * 3812015801u;
	return float(Random::pcg(seed) >> 8u) / 16777216.0;
}

// Smooth world-space wavefront displacement using the shared PCG hash.
// Fixed lattice values avoid texture repetition and extra light-ray fetches.
// Quintic reconstruction avoids the corners of coarse bilinear mips.
// Four/two wavelength cells bound the cross-band slope below one even at the
// maximum bend, while neighbouring bands can have different local curvature.
float3 MiddleBandBend(float2 coordinate)
{
	const float2 p = coordinate / float2(4.0, 2.0);
	const float2 cell = floor(p), f = frac(p);
	const float2 weight = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
	const float2 derivative = 30.0 * f * f * (f - 1.0) * (f - 1.0) / float2(4.0, 2.0);
	const float a = MiddleBandLattice(int2(cell));
	const float b = MiddleBandLattice(int2(cell) + int2(1, 0));
	const float c = MiddleBandLattice(int2(cell) + int2(0, 1));
	const float d = MiddleBandLattice(int2(cell) + int2(1, 1));
	return float3(2.0 * lerp(lerp(a, b, weight.x), lerp(c, d, weight.x), weight.y) - 1.0,
		2.0 * lerp(b - a, d - c, weight.y) * derivative.x,
		2.0 * lerp(c - a, d - b, weight.x) * derivative.y);
}

// Local column: bottom/top in game units, peak extinction per metre, As/Ac type.
// The footprint is in game units, projected onto the weather plane. View and
// lighting query the same shape at the scale their sampling can resolve.
float4 MiddleCloudColumn(float2 position, float footprint = 0.0, float bandFootprint = 0.0, bool testAltitude = false, float altitude = 0.0)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	const float2 uv = (position - info.cirrusWindOffset) * info.middleLayer.w + 0.5;
	const float4 field = TexMiddleWeather.SampleLevel(TileableSampler, uv, 0);
	const float3 geometry = field.yzw / max(field.x, 1e-6);
	float2 bounds = lerp(info.middleLayer.x, info.middleLayer.y, geometry.xy);
	float density = field.x;
	// Cells and bands can only shrink a non-inverted column with normalized contrast.
	// Extrapolated geometry remains supported; decline this bound when it can expand.
	if (testAltitude && bounds.y >= bounds.x && (MiddleCloudBandShape.w <= 0.0 || (MiddleCloudBandShape.x >= 0.0 && MiddleCloudBandShape.x <= 1.0))) {
		const float displacement = MiddleCloudBandShape.w > 0.0 ? abs(MiddleCloudBandShape.y) : 0.0;
		if (altitude < bounds.x - displacement || altitude > bounds.y + displacement)
			return 0.0;
	}
	[branch] if (density > 0.0)
	{
		const float modulation = lerp(MiddleCloudCells.z, MiddleCloudCells.y, geometry.z);
		const float uvFootprint = footprint * info.middleLayer.w;
		float cell = 1.0;
		[branch] if (modulation > 0.0)
		{
			const float2 cellUV = uv * MiddleCloudCells.x;
			float2 warp = 0.0;
			[branch] if (MiddleCloudCells.w > 0.0)
			{
				// Move whole cells with a broad, filtered field. A UV-scale warp
				// containing fine octaves folds the carrier into long thin ribbons.
				const float warpFootprint = max(uvFootprint * MiddleCloudCells.x * 0.25, 0.125);
				warp = (float2(MiddleDetailNoise(TexMiddleWarp, cellUV * 0.25 + 0.17, warpFootprint),
							MiddleDetailNoise(TexMiddleWarp, cellUV * 0.25 + 0.63, warpFootprint)) *
							   2.0 -
						   1.0) *
				       (MiddleCloudCells.w * 0.125);
			}
			cell = MiddleDetailNoise(TexMiddleCell, cellUV + warp, uvFootprint * MiddleCloudCells.x);
			// The shared cell source already has its own contrast/response.
			// Do not square it again before the final volumetric shaping.
		}
		bounds.y = lerp(bounds.x, bounds.y, lerp(1.0, cell, modulation * 0.5));
		density *= lerp(1.0, cell, modulation);
	}
	[branch] if (MiddleCloudBandShape.w > 0.0 && density > 0.0)
	{
		const float2 advected = position - info.cirrusWindOffset;
		float phase = dot(advected, MiddleCloudBands.xy);
		float2 phaseGradient = float2(1.0, 0.0);
		[branch] if (MiddleCloudBands.z > 0.0)
		{
			const float along = dot(advected, float2(-MiddleCloudBands.y, MiddleCloudBands.x));
			const float3 bend = MiddleBandBend(float2(phase, along));
			phase += bend.x * MiddleCloudBands.z;
			phaseGradient += bend.yz * MiddleCloudBands.z;
		}
		const float width = max(footprint, bandFootprint) * length(phaseGradient) / MiddleCloudBandShape.z;
		const float2 band = MiddleBandDistribution(saturate(density), MiddleCloudBands.w, phase, width);
		// Local weather and cells set the condensation margin before the wave.
		// This varies band width and permits finite ends instead of merely
		// dimming an otherwise unbroken, fixed-width stripe.
		const float occupiedDepth = saturate(band.x / max(density, 1e-6));
		bounds.y = bounds.x + (bounds.y - bounds.x) * occupiedDepth *
		                          lerp(1.0, 0.5 + 0.5 * band.y, MiddleCloudBandShape.x);
		bounds += band.y * MiddleCloudBandShape.y;
		density = band.x;
	}
	return float4(bounds, min(density, 0.7) * info.middleLayer.z, geometry.z);
}

// Keep the packed noise family and its channel reconstruction shared with low
// cloud. Texture size is a tile period, not a cloud-element or octave size.
float4 MiddleShapeNoise(float3 position, float footprint, float dimensionalProfile)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	const float frequency = info.middleShape.z;
	const float altitude = info.middleShape.w > 0.5 ?
	                           CloudProfileAltitude(position, FrameBuffer::CameraPosAdjust.xy, info.cloudCurvatureRadius) :
	                           0.5 * (info.middleLayer.x + info.middleLayer.y);
	const float3 uv = float3(position.xy - info.cirrusWindOffset, altitude) * frequency;
	// Low NDF clouds suppress fine interior noise with profile * 3. Interpolate
	// the mip continuously here: discrete profile bands would mark thin decks.
	const float profileMip = saturate(dimensionalProfile) * 3.0;
	uint width, height, depth;
	[branch] if (info.useVoxelNoise != 0u)
	{
		NoiseVoxel.GetDimensions(width, height, depth);
		const float mip = max(profileMip, log2(max(footprint * frequency * max(max(width, height), depth), 1.0)));
		return NoiseVoxel.SampleLevel(TileableSampler, uv, mip);
	}
	TexCloudShapeNoise.GetDimensions(width, height, depth);
	const float mip = max(profileMip, log2(max(footprint * frequency * max(max(width, height), depth), 1.0)));
	return TexCloudShapeNoise.SampleLevel(TileableSampler, uv, mip);
}

// As/Ac is an authored fibrous/rounded morphology blend, not an ice fraction.
// Both noise families reconstruct from the LOCAL dimensional profile. The
// low-cloud type/height blend and base-density boost describe a different
// carrier and are deliberately not applied to the middle-layer weather.
float MiddleShapedProfile(float4 noise, float profile, float height, float type, float distanceMeters)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	if (info.skipShapeNoise != 0u)
		return profile;
	float2 components;
	[branch] if (info.useVoxelNoise != 0u)
		components = ReconstructNdfVoxelNoiseComponents(noise, profile);
	else components = float2(ReconstructCloudBillow(noise, profile, 1.0, height),
		ReconstructCloudWisp(noise, profile, height, distanceMeters, 0.0));
	return saturate(profile + 0.975 * lerp(components.y, components.x, saturate(type)) - 1.0);
}

float MiddleLocalProfile(float4 column, float height)
{
	return column.z / max(VolumetricCloudBuffer[0].middleLayer.z, 1e-6) *
	       MiddleProfile(height, VolumetricCloudBuffer[0].middleShape.x);
}

// Invert the un-eroded carrier mass, rather than a frozen erosion threshold.
// The interior is linear; two Newton steps invert the smoothstep edge integral
// to < 6e-7 edge-coordinate error. This also retains sub-step thin layers.
float MiddleProfileHeight(float mass)
{
	const float soft = VolumetricCloudBuffer[0].middleShape.x;
	const float total = 1.0 - soft;
	const float lower = min(mass, total - mass);
	if (lower >= 0.5 * soft)
		return mass + 0.5 * soft;
	const float u = saturate(lower / soft);
	const float q = pow(u, 1.0 / 3.0);
	float edge = q / (1.0 - q / 6.0);
	[unroll] for (uint i = 0u; i < 2u; ++i)
		edge -= (edge * (1.0 - 0.5 * edge) - u / max(edge * edge, 1e-20)) / (3.0 - 2.0 * edge);
	return mass < 0.5 * total ? soft * edge : 1.0 - soft * edge;
}

struct MiddleInterval
{
	float2 heights;
	float2 masses;
	float2 ratios;
	float2 support;
	float extinction;
};

float MiddleIntervalHeight(MiddleInterval interval, float fraction)
{
	return abs(interval.heights.y - interval.heights.x) > 1e-4 ?
	           MiddleProfileHeight(lerp(interval.masses.x, interval.masses.y, fraction)) :
	           0.5 * (interval.heights.x + interval.heights.y);
}

float MiddleIntervalFraction(MiddleInterval interval, float height)
{
	const float delta = interval.heights.y - interval.heights.x;
	return abs(delta) > 1e-4 ? saturate((height - interval.heights.x) / delta) : 0.5;
}

// Reconstruct an erosion ratio in CARRIER MASS using two Gauss nodes. Freeze
// the sampled noise within each density interval, but reconstruct its channels
// at each node's local profile and height. Integrate and invert the SAME mass.
MiddleInterval MiddleDensityInterval(float4 column0, float4 column1, float3 start, float3 end,
	float footprint, bool reuseNoise = false, float4 sharedNoise = 0.0)
{
	MiddleInterval interval = (MiddleInterval)0;
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	if (column0.z + column1.z <= 0.0)
		return interval;
	if (column0.z <= 0.0)
		column0.xyw = column1.xyw;
	if (column1.z <= 0.0)
		column1.xyw = column0.xyw;
	interval.heights = float2(
		(CloudProfileAltitude(start, FrameBuffer::CameraPosAdjust.xy, info.cloudCurvatureRadius) - column0.x) / max(column0.y - column0.x, 1e-5),
		(CloudProfileAltitude(end, FrameBuffer::CameraPosAdjust.xy, info.cloudCurvatureRadius) - column1.x) / max(column1.y - column1.x, 1e-5));
	if (max(interval.heights.x, interval.heights.y) <= 0.0 || min(interval.heights.x, interval.heights.y) >= 1.0)
		return interval;
	interval.masses = float2(MiddleProfileIntegral(interval.heights.x, info.middleShape.x),
		MiddleProfileIntegral(interval.heights.y, info.middleShape.x));
	interval.extinction = 0.5 * (column0.z + column1.z);
	interval.ratios = 1.0;
	interval.support = float2(0.0, 1.0);
	[branch] if (info.skipShapeNoise == 0u)
	{
		const float centerHeight = MiddleIntervalHeight(interval, 0.5);
		const float centerFraction = MiddleIntervalFraction(interval, centerHeight);
		const float4 column = 0.5 * (column0 + column1);
		const float3 center = lerp(start, end, centerFraction);
		const float4 noise = reuseNoise ? sharedNoise : MiddleShapeNoise(center, footprint, MiddleLocalProfile(column, centerHeight));
		const float distance = length(center - (FrameBuffer::CameraPosAdjust.xyz - float3(0, 0, info.bottomZ))) * GAME_UNIT_TO_M;
		float2 ratio;
		const float2 nodes = float2(0.2113248654, 0.7886751346);
		// In the carrier interior the profile is constant. Above the billow's
		// base transition both reconstruction nodes are identical, including
		// Wisp (storm is zero here). Avoid evaluating the same powers twice.
		[branch] if (min(interval.heights.x, interval.heights.y) >= max(info.middleShape.x, 0.1) &&
					 max(interval.heights.x, interval.heights.y) <= 1.0 - info.middleShape.x)
		{
			const float profile = column.z / max(info.middleLayer.z, 1e-6);
			ratio = MiddleShapedProfile(noise, profile, centerHeight, column.w, distance) / max(profile, 1e-6);
		}
		else
		{
			[unroll] for (uint i = 0u; i < 2u; ++i)
			{
				const float height = MiddleIntervalHeight(interval, nodes[i]);
				const float profile = MiddleLocalProfile(column, height);
				ratio[i] = MiddleShapedProfile(noise, profile, height, column.w, distance) / max(profile, 1e-6);
			}
		}
		// Clip the linear reconstruction at its zero crossing, retaining its
		// occupied mass instead of lifting a negative endpoint to zero.
		const float slope = (ratio.y - ratio.x) * 1.732050808;
		const float2 ends = float2(ratio.x - nodes.x * slope, ratio.y + nodes.x * slope);
		interval.support = float2(ends.x < 0.0 ? saturate(-ends.x / max(slope, 1e-8)) : 0.0,
			ends.y < 0.0 ? saturate(ends.x / max(-slope, 1e-8)) : 1.0);
		interval.ratios = max(ends, 0.0);
	}
	return interval;
}

float MiddleIntervalAverage(MiddleInterval interval)
{
	const float delta = abs(interval.heights.y - interval.heights.x);
	const float profile = delta > 1e-4 ? abs(interval.masses.y - interval.masses.x) / delta :
	                                     MiddleProfile(0.5 * (interval.heights.x + interval.heights.y), VolumetricCloudBuffer[0].middleShape.x);
	return interval.extinction * profile * 0.5 * (interval.ratios.x + interval.ratios.y) * (interval.support.y - interval.support.x);
}

// Inverse of the linear ratio's integral, followed by the carrier inverse.
// The returned material density is local, while the interval average is used
// only for view transport. This preserves the two-point scattering quadrature.
float3 MiddleIntervalMaterial(MiddleInterval interval, float quantile)
{
	const float target = saturate(quantile) * 0.5 * (interval.ratios.x + interval.ratios.y);
	const float slope = interval.ratios.y - interval.ratios.x;
	const float ratio = sqrt(max(interval.ratios.x * interval.ratios.x + 2.0 * slope * target, 0.0));
	const float fraction = lerp(interval.support.x, interval.support.y, saturate(2.0 * target / max(interval.ratios.x + ratio, 1e-8)));
	const float height = MiddleIntervalHeight(interval, fraction);
	const float spatial = abs(interval.heights.y - interval.heights.x) > 1e-4 ? MiddleIntervalFraction(interval, height) : fraction;
	return float3(interval.extinction * MiddleProfile(height, VolumetricCloudBuffer[0].middleShape.x) * ratio, saturate(height), spatial);
}

// Integral of the positive linear reconstruction through two Gauss values.
float MiddleRatioMass(float2 samples, float fraction)
{
	const float slope = (samples.y - samples.x) * 1.732050808;
	const float2 ends = float2(samples.x - 0.2113248654 * slope, samples.y + 0.2113248654 * slope);
	const float left = ends.x < 0.0 ? saturate(-ends.x / max(slope, 1e-8)) : 0.0;
	const float right = ends.y < 0.0 ? saturate(ends.x / max(-slope, 1e-8)) : 1.0;
	const float width = max(min(fraction, right) - left, 0.0);
	return width * (max(ends.x, 0.0) + 0.5 * slope * width);
}

// Coarse column transport uses one filtered noise value. Integrate its LOCAL
// reconstruction separately over each edge and the constant-profile interior.
// The edge nodes are the inverse smoothstep-integral at the two Gauss masses.
float2 MiddleColumnDepths(float4 column, float3 position, float footprint = 0.0)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	const float curve = CloudCurvatureOffset(position.xy - FrameBuffer::CameraPosAdjust.xy, info.cloudCurvatureRadius);
	const float width = max(column.y - column.x, 1e-5);
	const float h = saturate((position.z + curve - column.x) / width);
	const float soft = info.middleShape.x;
	const float mass = MiddleProfileIntegral(h, soft);
	const float peak = column.z / max(info.middleLayer.z, 1e-6);
	float down = mass, total = 1.0 - soft;
	[branch] if (info.skipShapeNoise == 0u && peak > 0.0)
	{
		const float3 center = float3(position.xy, 0.5 * (column.x + column.y) - curve);
		const float4 noise = MiddleShapeNoise(center, max(footprint, width * 0.5), peak);
		const float distance = length(center - (FrameBuffer::CameraPosAdjust.xyz - float3(0, 0, info.bottomZ))) * GAME_UNIT_TO_M;
		const float2 nodes = float2(0.5230296028, 0.8931839209);
		float2 lower, upper;
		[unroll] for (uint i = 0u; i < 2u; ++i)
		{
			const float profile = peak * nodes[i] * nodes[i] * (3.0 - 2.0 * nodes[i]);
			lower[i] = MiddleShapedProfile(noise, profile, soft * nodes[i], column.w, distance) / max(profile, 1e-6);
			upper[1u - i] = MiddleShapedProfile(noise, profile, 1.0 - soft * nodes[i], column.w, distance) / max(profile, 1e-6);
		}
		const float core = MiddleShapedProfile(noise, peak, 0.5, column.w, distance) / max(peak, 1e-6);
		const float edgeMass = 0.5 * soft;
		const float coreMass = 1.0 - 2.0 * soft;
		down = edgeMass * (MiddleRatioMass(lower, saturate(mass / edgeMass)) +
							  MiddleRatioMass(upper, saturate((mass - edgeMass - coreMass) / edgeMass))) +
		       core * clamp(mass - edgeMass, 0.0, coreMass);
		total = edgeMass * (MiddleRatioMass(lower, 1.0) + MiddleRatioMass(upper, 1.0)) + core * coreMass;
	}
	return column.z * width * GAME_UNIT_TO_M * float2(max(total - down, 0.0), down);
}

float MiddleCloudDensity(float3 position, out float4 model, out float2 opticalDepth, float footprint = 0.0)
{
	model = 0.0;
	opticalDepth = 0.0;
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	if (info.middleLayer.z <= 0.0)
		return 0.0;
	const float altitude = CloudProfileAltitude(position, FrameBuffer::CameraPosAdjust.xy, info.cloudCurvatureRadius);
	const float4 column = MiddleCloudColumn(position.xy, footprint, 0.0, true, altitude);
	if (column.z <= 0.0 || altitude <= column.x || altitude >= column.y)
		return 0.0;
	const float h = (altitude - column.x) / max(column.y - column.x, 1e-5);
	const float profile = MiddleLocalProfile(column, h);
	const float4 noise = info.skipShapeNoise != 0u ? 0.0 : MiddleShapeNoise(position, footprint, profile);
	const float shaped = MiddleShapedProfile(noise, profile, h, column.w, length(position - (FrameBuffer::CameraPosAdjust.xyz - float3(0, 0, info.bottomZ))) * GAME_UNIT_TO_M);
	model = float4(column.z / info.middleLayer.z, h, column.w, shaped);
	opticalDepth = MiddleColumnDepths(column, position, footprint);
	return info.middleLayer.z * shaped;
}

// XY are local extinction samples, ZW their heights. Keep the same independent
// lighting interface, including one self-shadow query per macro view step.
float MiddleCloudSegment(inout float3 position, float3 start, float3 end, inout float4 startColumn, out float4 materialSamples, out float2 opticalDepth, float footprint = 0.0)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	const float3 midpoint = 0.5 * (start + end);
	const float bandFootprint = 0.5 * length(end.xy - start.xy);
	const float4 middleColumn = MiddleCloudColumn(midpoint.xy, footprint, bandFootprint);
	const float4 endColumn = MiddleCloudColumn(end.xy, footprint, bandFootprint);
	const MiddleInterval a = MiddleDensityInterval(startColumn, middleColumn, start, midpoint, footprint);
	const MiddleInterval b = MiddleDensityInterval(middleColumn, endColumn, midpoint, end, footprint);
	const float2 density = float2(MiddleIntervalAverage(a), MiddleIntervalAverage(b));
	const float sum = density.x + density.y;
	const float tau = 0.5 * sum * length(end - start) * GAME_UNIT_TO_M * info.viewExtinction;
	materialSamples = 0.0;
	float fraction = 0.5;
	if (sum > 1e-8) {
		const float2 nodes = float2(0.2113248654, 0.7886751346);
		const float2 quantiles = tau < 0.001 ? nodes + 0.5 * tau * nodes * (nodes - 1.0) :
		                                       -log(1.0 - nodes * (1.0 - exp(-tau))) / tau;
		float2 fractions;
		[unroll] for (uint i = 0u; i < 2u; ++i)
		{
			const float target = quantiles[i] * sum;
			const bool first = target < density.x;
			float3 material;
			if (first)
				material = MiddleIntervalMaterial(a, target / max(density.x, 1e-8));
			else
				material = MiddleIntervalMaterial(b, (target - density.x) / max(density.y, 1e-8));
			materialSamples[i] = material.x;
			materialSamples[i + 2u] = material.y;
			fractions[i] = (first ? 0.0 : 0.5) + 0.5 * material.z;
		}
		fraction = 0.5 * (fractions.x + fractions.y);
	}
	float4 column0 = fraction < 0.5 ? startColumn : middleColumn;
	float4 column1 = fraction < 0.5 ? middleColumn : endColumn;
	if (column0.z <= 0.0)
		column0.xyw = column1.xyw;
	if (column1.z <= 0.0)
		column1.xyw = column0.xyw;
	const float4 column = lerp(column0, column1, fraction < 0.5 ? fraction * 2.0 : fraction * 2.0 - 1.0);
	startColumn = endColumn;
	position = lerp(start, end, fraction);
	opticalDepth = 0.0;
	[branch] if (sum > 0.0)
		opticalDepth = MiddleColumnDepths(column, position, footprint);
	return 0.5 * sum;
}

// HP-style local self-shadow, limited to 3 km or the upper layer exit. Each
// interval integrates a carrier reconstructed between its endpoint columns,
// so thin clouds do not fall between light probes. Distinct from the distant shadow
// cast onto low clouds, which can start well below the middle layer.
float MiddleSelfShadowDepth(float3 position, float3 direction)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	[branch] if (info.middleLayer.z <= 0.0) return 0.0;
	const float3 origin = float3(position.xy - FrameBuffer::CameraPosAdjust.xy, position.z);
	const float2 interval = IntersectCloudBelow(origin, direction, info.middleLayer.y, info.cloudCurvatureRadius);
	const float distance = clamp(interval.y, 0.0, 3000.0 * GAME_UNITS_PER_METER);
	if (distance <= 0.0)
		return 0.0;
	const uint samples = 8u;
	const float4 tailNoise = info.skipShapeNoise != 0u ? 0.0 : MiddleShapeNoise(position + direction * (0.5 * distance), distance, 0.0);
	float4 column0 = MiddleCloudColumn(position.xy);
	float depth = 0.0;
	// Specialize the fixed probe spacing and detail/shared-noise queries in
	// the upper view. Reflections already distribute view steps across lanes
	// and run faster with the compact light loop.
#if defined(NO_CLOUD_STORMS) && !defined(CLOUD_REFLECTION)
	[unroll]
#else
	[loop]
#endif
		for (uint i = 0u; i < samples; ++i)
	{
		// Keep the eight probes, concentrating their resolution at the visible
		// surface. Uniform layer-wide intervals filtered away the same Billow/
		// Wisp relief that the view density was meant to reveal.
		const float begin = float(i) / float(samples);
		const float finish = float(i + 1u) / float(samples);
		const float stepSize = (finish * sqrt(finish) - begin * sqrt(begin)) * distance;
		const float3 start = position + direction * (begin * sqrt(begin) * distance);
		const float3 end = start + direction * stepSize;
		const float4 column1 = MiddleCloudColumn(end.xy, stepSize * length(direction.xy));
		// Resolve nearby surface detail, then reuse path-filtered noise. Every
		// interval retains its own carrier and As/Ac reconstruction.
		[branch] if (i < 2u)
			depth += MiddleIntervalAverage(MiddleDensityInterval(column0, column1, start, end, stepSize)) * stepSize;
		else depth += MiddleIntervalAverage(MiddleDensityInterval(column0, column1, start, end, stepSize, true, tailNoise)) * stepSize;
		column0 = column1;
	}
	return depth * GAME_UNIT_TO_M;
}

// One weather column at the first layer crossing. Replace the soft edges by
// equal-area inset bounds: the full vertical optical depth is unchanged.
// Lighting is independent of the camera's finite view-march distance.
float2 MiddleRayDepth(float3 position, float3 direction)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	[branch] if (info.middleLayer.z <= 0.0) return 0.0;
	const float3 origin = float3(position.xy - FrameBuffer::CameraPosAdjust.xy, position.z);
	const float2 center = IntersectCloudBelow(origin, direction, 0.5 * (info.middleLayer.x + info.middleLayer.y), info.cloudCurvatureRadius);
	const float distance = max(center.x > 0.0 ? center.x : center.y, 0.0);
	const float4 column = MiddleCloudColumn(position.xy + direction.xy * min(distance, 1e8), info.rayMarchRange / 128.0);
	const float inset = 0.5 * info.middleShape.x * (column.y - column.x);
	const float2 outer = max(IntersectCloudBelow(origin, direction, column.y - inset, info.cloudCurvatureRadius), 0.0);
	const float2 inner = clamp(IntersectCloudBelow(origin, direction, column.x + inset, info.cloudCurvatureRadius), outer.x, outer.y);
	const float length = max((inner.x - outer.x) + (outer.y - inner.y), 0.0);
	const float3 hit = position + direction * min(distance, 1e8);
	const float2 vertical = MiddleColumnDepths(column, hit, info.rayMarchRange / 128.0);
	const float verticalDepth = vertical.x + vertical.y;
	return float2(verticalDepth * length / max((column.y - column.x) * (1.0 - info.middleShape.x), 1e-5), verticalDepth);
}

// Hemispheric diffusion fit, not direct-beam Beer transmission. Transport
// extinction retains forward scattering; 2 is the mean-cosine path estimate.
float2 MiddleAmbientResponse(float2 depth)
{
	return rcp(1.0 + 2.0 * VolumetricCloudBuffer[0].msTransportExtinction * max(depth, 0.0));
}

float3 MiddleCloudDisplacement(float3 position)
{
	const VolumetricCloudData info = VolumetricCloudBuffer[0];
	const float2 relative = position.xy - FrameBuffer::CameraPosAdjust.xy;
	const float2 previous = relative + FrameBuffer::CameraPosAdjust.xy - info.previousCamera.xy - info.cirrusWindDelta;
	const float previousRadius = info.noisePadding.x > 0.0 ? info.noisePadding.x : info.cloudCurvatureRadius;
	const float curveDelta = dot(previous - relative, previous + relative) / info.cloudCurvatureRadius +
	                         dot(previous, previous) / previousRadius * ((info.cloudCurvatureRadius - previousRadius) / info.cloudCurvatureRadius);
	return float3(info.cirrusWindDelta, curveDelta);
}

#endif
