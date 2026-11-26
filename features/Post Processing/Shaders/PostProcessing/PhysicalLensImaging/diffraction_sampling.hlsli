#ifndef PHYSICAL_LENS_DIFFRACTION_SAMPLING
#define PHYSICAL_LENS_DIFFRACTION_SAMPLING
#include "PostProcessing/PhysicalLensImaging/diffraction_domain.hlsli"
#include "PostProcessing/PhysicalLensImaging/diffraction_parameters.hlsli"
#include "PostProcessing/PhysicalLensImaging/regions.hlsli"

// The two ownership fields select the same disjoint radiance layers as source
// extraction. The paper model adds their filtered core; the legacy optical
// reference instead redistributes their energy between the core and tail.
float3 SelectedDiffractionRadiance(float3 color, float4 region)
{
	float luma = dot(max(color, 0), float3(.2126, .7152, .0722));
	float pedestal = max(region.x, region.y);
	float excess = saturate((luma - pedestal) / max(luma, 1e-20)) * smoothstep(2 * pedestal, 4 * pedestal, luma);
	float base = max(saturate((luma - region.x) / max(luma, 1e-20)) - excess, 0);
	return color * ((region.z > 0 ? base : 0) + (region.w > 0 ? excess : 0));
}

float SelectedWaveBand(float3 color, float4 region, StructuredBuffer<float4> sourceValidity, uint band, bool validateProjection)
{
	if (validateProjection) {
		float base = region.z > 0 ? sourceValidity[(uint(region.z) - 1) * 7 + band * 2].w : 0;
		float excess = region.w > 0 ? sourceValidity[(uint(region.w) - 1) * 7 + band * 2].w : 0;
		return SelectedDiffractionRadiance(color, float4(region.xy, region.z, 0))[band] * base +
		       SelectedDiffractionRadiance(color, float4(region.xy, 0, region.w))[band] * excess;
	}
	return SelectedDiffractionRadiance(color, region)[band];
}

bool ActiveWaveRegion(float4 region, StructuredBuffer<float4> validity, uint band, bool validateProjection)
{
	if (!validateProjection)
		return any(region.zw > 0);
	float a = region.z > 0 ? validity[(uint(region.z) - 1) * 7 + band * 2].w : 0;
	float b = region.w > 0 ? validity[(uint(region.w) - 1) * 7 + band * 2].w : 0;
	return a + b > 0;
}

float SampleDiffractionCore(Texture2D<float4> scene, StructuredBuffer<float4> weights,
	StructuredBuffer<float4> regions, StructuredBuffer<float4> sourceValidity, SamplerState linearSampler, float2 sourceUV,
	float2 pixelX, float2 pixelY, uint band, bool validateProjection)
{
	float3 center = scene.SampleLevel(linearSampler, sourceUV, 0).rgb;
	uint width, height;
	scene.GetDimensions(width, height);
	uint2 size = uint2(width, height);
	float2 extent = abs(pixelX) + abs(pixelY);
	uint2 lo = WaveRegionTile(sourceUV - extent, size);
	uint2 hi = WaveRegionTile(sourceUV + extent, size);
	float4 region = regions[lo.y * 64 + lo.x];
	bool active = ActiveWaveRegion(region, sourceValidity, band, validateProjection);
	if (any(lo != hi)) {
		active = active || ActiveWaveRegion(regions[hi.y * 64 + lo.x], sourceValidity, band, validateProjection) || ActiveWaveRegion(regions[lo.y * 64 + hi.x], sourceValidity, band, validateProjection) || ActiveWaveRegion(regions[hi.y * 64 + hi.x], sourceValidity, band, validateProjection);
	}
	// Evaluate the core only at selected highlights. Unselected pixels retain
	// their direct lens sample without a nine-tap convolution.
	float result = center[band];
	[branch] if (active)
	{
		uint2 centerTile = WaveRegionTile(sourceUV, size);
		float selectedCenter = SelectedWaveBand(center, regions[centerTile.y * 64 + centerTile.x], sourceValidity, band, validateProjection);
		float sum = 0;
		[unroll] for (uint y = 0; y < 3; ++y)
		{
			float3 row = weights[band * 4 + y].xyz;
			[unroll] for (uint x = 0; x < 3; ++x)
			{
				float2 uv = sourceUV + (float(x) - 1) * pixelX + (float(y) - 1) * pixelY;
				float3 color = scene.SampleLevel(linearSampler, uv, 0).rgb;
				uint2 tile = WaveRegionTile(uv, size);
				sum += SelectedWaveBand(color, regions[tile.y * 64 + tile.x], sourceValidity, band, validateProjection) * row[x];
			}
		}
		if (WaveStarburst.w > 0) {
			float gain = WaveStarburst.y * pow(5.6 / WaveStarburst.z, 2);
			result = center[band] + sum * gain * weights[band * 4 + 3].y * weights[band * 4 + 3].z;
		} else
			result = center[band] - selectedCenter + sum * (1 - weights[band * 4 + 3].x);
	}
	return result;
}

float DiffractionTailCDF(Texture2DArray<float4> psf, SamplerState linearSampler, float2 coordinate, bool2 mirrored, uint band)
{
	// Each quadrant integrates inward from its outer corner. Clamp at the
	// central seam so bilinear filtering never mixes opposite prefix directions.
	float2 uv = min(coordinate, 255.5) / 512;
	uv = float2(mirrored.x ? 1 - uv.x : uv.x, mirrored.y ? 1 - uv.y : uv.y);
	// Hardware bilinear weights have only eight fractional bits on D3D11.
	// In log-spaced cells that visibly quantizes distant bright diffraction.
	// Gather at the center of the chosen cell: fixed-point addressing must not
	// round into its neighbor while the full-precision lerp still uses this cell.
	float2 cell = floor(uv * 512 - .5);
	float4 taps = psf.GatherGreen(linearSampler, float3((cell + 1) / 512, band));
	float2 f = frac(uv * 512 - .5);
	float integral = lerp(lerp(taps.w, taps.z, f.x), lerp(taps.x, taps.y, f.x), f.y);
	return integral * saturate(coordinate.x + .5) * saturate(coordinate.y + .5);
}

float SampleDiffractionBox(Texture2DArray<float4> psf, SamplerState linearSampler,
	float2 offsetPixels, float2 radius, float2 psfStepPixels, uint band)
{
	// Source moments already include the source pixel's box variance. The
	// point-source limit is one output pixel, not an extra Gaussian blur.
	float2 lo = WaveDomainInverse((offsetPixels - radius) / psfStepPixels) + 256;
	float2 hi = WaveDomainInverse((offsetPixels + radius) / psfStepPixels) + 256;
	if (any(hi < -.5) || any(lo > 511.5))
		return 0;
	float mass = 0;
	[unroll] for (uint y = 0; y < 2; ++y)
	{
		[unroll] for (uint x = 0; x < 2; ++x)
		{
			bool2 mirrored = bool2(x != 0, y != 0);
			float2 a = float2(x != 0 ? 511 - hi.x : lo.x, y != 0 ? 511 - hi.y : lo.y);
			float2 b = float2(x != 0 ? 511 - lo.x : hi.x, y != 0 ? 511 - lo.y : hi.y);
			[branch] if (all(a < 255.5) && all(b > -.5))
			{
				mass += DiffractionTailCDF(psf, linearSampler, b, mirrored, band) -
				        DiffractionTailCDF(psf, linearSampler, float2(a.x, b.y), mirrored, band) -
				        DiffractionTailCDF(psf, linearSampler, float2(b.x, a.y), mirrored, band) +
				        DiffractionTailCDF(psf, linearSampler, a, mirrored, band);
			}
		}
	}
	return max(mass, 0) / (4 * radius.x * radius.y);
}
// A covariance ellipse is a compact finite emitter, not a rectangular patch.
// Resolve it only close to the emitter, where its outline matters; distant
// wings need only the second moments. The core still samples the actual scene.
float SampleDiffractionTail(Texture2DArray<float4> psf, SamplerState linearSampler,
	float2 offsetPixels, float2 variancePixels, float2 psfStepPixels, uint band)
{
	float2 box = max(sqrt(3 * variancePixels), .5);
	float2 footprint = WaveDomainInverse((offsetPixels + box) / psfStepPixels) - WaveDomainInverse((offsetPixels - box) / psfStepPixels);
	float directBlend = (1 - smoothstep(.5, 2, min(footprint.x, footprint.y))) * (1 - smoothstep(12, 16, max(footprint.x, footprint.y)));
	float direct = 0;
	if (directBlend > 0) {
		// Sub-cell footprints must not subtract nearly equal prefix sums.
		// Reconstruct the cached density directly to retain faint distant rays.
		uint2 samples = uint2(clamp(ceil(footprint), 1, 16));
		[loop] for (uint y = 0; y < samples.y; ++y)
		{
			[loop] for (uint x = 0; x < samples.x; ++x)
			{
				float2 offset = offsetPixels + ((float2(x, y) + .5) / samples * 2 - 1) * box;
				float2 p = WaveDomainInverse(offset / psfStepPixels) + 256;
				float4 taps = psf.GatherBlue(linearSampler, float3((floor(p) + 1) / 512, band));
				float2 f = frac(p);
				direct += lerp(lerp(taps.w, taps.z, f.x), lerp(taps.x, taps.y, f.x), f.y);
			}
		}
		direct /= samples.x * samples.y * psfStepPixels.x * psfStepPixels.y;
	}
	float filtered = 0;
	[branch] if (directBlend < 1)
	{
		float far = SampleDiffractionBox(psf, linearSampler, offsetPixels, box, psfStepPixels, band);
		float2 radius = max(2 * sqrt(variancePixels), .5);
		float size = max(radius.x, radius.y);
		float blend = smoothstep(2.5 * size + 8, 4 * size + 8, length(offsetPixels));
		filtered = far;
		[branch] if (size > 1 && blend < 1)
		{
			float total = 0, area = 0;
			[loop] for (uint strip = 0; strip < 32; ++strip)
			{
				float y = 2 * (strip + .5) / 32 - 1;
				float2 halfSize = float2(radius.x * sqrt(max(1 - y * y, 0)), radius.y / 32);
				float weight = halfSize.x * halfSize.y;
				total += weight * SampleDiffractionBox(psf, linearSampler, offsetPixels - float2(0, y * radius.y), halfSize, psfStepPixels, band);
				area += weight;
			}
			filtered = lerp(total / max(area, 1e-20), far, blend);
		}
	}
	return lerp(filtered, direct, directBlend);
}
#endif
