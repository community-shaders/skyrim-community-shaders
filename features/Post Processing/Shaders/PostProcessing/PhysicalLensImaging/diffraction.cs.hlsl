#include "PostProcessing/PhysicalLensImaging/trace.cs.hlsl"

// Zero padding resolves the PSF core without inventing a larger aperture.
static const uint WaveTransformSize = 512;
#include "PostProcessing/PhysicalLensImaging/diffraction_parameters.hlsli"
RWTexture2DArray<float4> WavePupil : register(u2);
#include "PostProcessing/PhysicalLensImaging/diffraction_boundary.hlsli"
#include "PostProcessing/PhysicalLensImaging/diffraction_domain.hlsli"
#include "PostProcessing/PhysicalLensImaging/microstructure.hlsli"
#include "PostProcessing/PhysicalLensImaging/spectrum.hlsli"
#include "PostProcessing/PhysicalLensImaging/surface_diffraction.hlsli"

float4 TraceWaveCell(float2 q, float wavelength, float margin)
{
	float qz = sqrt(max(1 - dot(q, q), 1e-8));
	RayInput ray;
	ray.PupilField = float4(0, 0, q / qz);
	ray.Conjugate = float4(0, wavelength, 0, 0);
	RayOutput hit = Trace(ray, true, margin);
	float objectZ = 1 - InverseFocusDistance * SensorZ;
	float radius2 = dot(hit.SensorEnergy.xy, hit.SensorEnergy.xy);
	float reference = radius2 * InverseFocusDistance / max(sqrt(objectZ * objectZ + radius2 * InverseFocusDistance * InverseFocusDistance) + objectZ, 1e-8);
	float phase = 2 * PI * (hit.Direction.w + reference) / (wavelength * .001);
	float2 wave;
	sincos(phase, wave.y, wave.x);
	return float4(wave * sqrt(hit.SensorEnergy.z), hit.SensorEnergy.w, hit.SensorEnergy.z);
}

[numthreads(8, 8, 1)] void BuildWavePupil(uint3 id : SV_DispatchThreadID) {
	int2 p = int2(id.xy) - int((WaveTransformSize - WavePupilSize) / 2);
	float4 field = 0;
	if (all(p >= 0) && all(p < int(WavePupilSize))) {
		float4 transfer = PupilTransfer[id.z];
		float slope = 1.2 * transfer.z / transfer.y;
		float extent = abs(slope) * rsqrt(1 + slope * slope);
		float step = 2 * extent / WavePupilSize;
		float2 q = (float2(p) + .5 - .5 * WavePupilSize) * step;
		float wavelength = id.z == 0 ? .65 : id.z == 1 ? .55 :
		                                                 .45;
		field = TraceWaveCell(q, wavelength, 4.8 / WavePupilSize);
		if (WaveSurface.x > 0 && WaveSurface.w > 0) {
			float stepMM = 2.4 * WaveSurface.z / WavePupilSize;
			float2 positionMM = (float2(p) + .5 - .5 * WavePupilSize) * stepMM;
			float2 defect = Microstructure(positionMM, stepMM, uint(WaveSurface.y));
			float2 phase;
			sincos(2 * PI * WaveSurface.x * defect.y / wavelength, phase.y, phase.x);
			field.xy = float2(field.x * phase.x - field.y * phase.y, field.x * phase.y + field.y * phase.x) * exp(-WaveSurface.x * defect.x);
			field.w = dot(field.xy, field.xy);
		}
	}
	WavePupil[id] = field;
}

Texture2DArray<float4> WaveInput : register(t1);
RWTexture2DArray<float4> WaveTransform : register(u3);
RWTexture2DArray<float4> DiffractionPSF : register(u4);
// Exact area of the local tangent half-plane within a square pupil cell.
// Only the boundary cells change; this is quadrature, not optical apodization.
float PupilCoverage(float clearance, float2 gradient)
{
	float a = max(abs(gradient.x), abs(gradient.y));
	float b = min(abs(gradient.x), abs(gradient.y));
	float t = max(.5 * (a + b) - abs(clearance), 0);
	float area = t >= b ? (t - .5 * b) / max(a, 1e-20) : t * t / max(2 * a * b, 1e-20);
	return clearance >= 0 ? 1 - area : area;
}
[numthreads(8, 8, 1)] void ResolveWavePupil(uint3 id : SV_DispatchThreadID) {
	float4 value = WaveInput.Load(int4(id, 0));
	float coverage = value.z >= 0 ? 1 : 0;
	[branch] if (abs(value.z) < 2.4 / WavePupilSize && value.w > 0)
	{
		float dx = .5 * (WaveInput.Load(int4(int2(id.xy) + int2(1, 0), id.z, 0)).z - WaveInput.Load(int4(int2(id.xy) - int2(1, 0), id.z, 0)).z);
		float dy = .5 * (WaveInput.Load(int4(int2(id.xy) + int2(0, 1), id.z, 0)).z - WaveInput.Load(int4(int2(id.xy) - int2(0, 1), id.z, 0)).z);
		coverage = PupilCoverage(value.z, float2(dx, dy));
	}
	value.xy *= coverage;
	value.w = dot(value.xy, value.xy);
	WaveTransform[id] = value;
} RWStructuredBuffer<float4> WaveKernel : register(u5);
groupshared float4 FourierValues[512];

// Two identical row transforms with transposed stores form the 2D FFT.
// Carry energy alongside the complex field, so the second pass supplies the
// exact Parseval normalization without a separate reduction or CPU readback.
[numthreads(512, 1, 1)] void TransformWave(uint3 group : SV_GroupID, uint lane : SV_GroupIndex) {
	uint reversed = reversebits(lane) >> 23;
	FourierValues[lane] = WaveInput.Load(int4(reversed, group.x, group.y, 0));
	GroupMemoryBarrierWithGroupSync();
	[unroll] for (uint length = 2; length <= 512; length <<= 1)
	{
		uint phaseIndex = lane % (length / 2);
		uint evenIndex = lane / length * length + phaseIndex;
		float4 even = FourierValues[evenIndex], odd = FourierValues[evenIndex + length / 2];
		float2 phase;
		sincos(-2 * PI * phaseIndex / length, phase.y, phase.x);
		float2 rotated = float2(phase.x * odd.x - phase.y * odd.y, phase.x * odd.y + phase.y * odd.x);
		float4 value = float4(even.xy + (lane & (length / 2) ? -rotated : rotated), 0, even.w + odd.w);
		GroupMemoryBarrierWithGroupSync();
		FourierValues[lane] = value;
		GroupMemoryBarrierWithGroupSync();
	}
	WaveTransform[uint3(group.x, lane, group.y)] = FourierValues[lane];
}

	[numthreads(8, 8, 1)] void NormalizeWaveFFT(uint3 id : SV_DispatchThreadID)
{
	float4 value = WaveInput.Load(int4((id.xy + 256) % 512, id.z, 0));
	float intensity = dot(value.xy, value.xy) / max(value.w * (512 * 512), 1e-30);
	DiffractionPSF[id] = float4(intensity, intensity, 0, value.w);
}
float WaveIntegralCorner(float2 p, bool2 mirror, uint band)
{
	float2 c = min(p, 255.5) - .5;
	c = float2(mirror.x ? 511 - c.x : c.x, mirror.y ? 511 - c.y : c.y);
	int2 base = int2(floor(c));
	float2 f = frac(c);
	float a = WaveInput.Load(int4(clamp(base, 0, 511), band, 0)).y;
	float b = WaveInput.Load(int4(clamp(base + int2(1, 0), 0, 511), band, 0)).y;
	float d = WaveInput.Load(int4(clamp(base + int2(0, 1), 0, 511), band, 0)).y;
	float e = WaveInput.Load(int4(clamp(base + 1, 0, 511), band, 0)).y;
	return lerp(lerp(a, b, f.x), lerp(d, e, f.x), f.y) * saturate(p.x + .5) * saturate(p.y + .5);
}
float IntegrateWaveFFT(float2 lo, float2 hi, uint band)
{
	if (any(hi <= lo))
		return 0;
	lo += 256;
	hi += 256;
	float sum = 0;
	[loop] for (uint y = 0; y < 2; ++y)[loop] for (uint x = 0; x < 2; ++x)
	{
		bool2 mirror = bool2(x != 0, y != 0);
		float2 a = float2(x ? 511 - hi.x : lo.x, y ? 511 - hi.y : lo.y);
		float2 b = float2(x ? 511 - lo.x : hi.x, y ? 511 - lo.y : hi.y);
		if (all(a < 255.5) && all(b > -.5))
			sum += WaveIntegralCorner(b, mirror, band) - WaveIntegralCorner(float2(a.x, b.y), mirror, band) - WaveIntegralCorner(float2(b.x, a.y), mirror, band) + WaveIntegralCorner(a, mirror, band);
	}
	return max(sum, 0);
}
[numthreads(8, 8, 1)] void NormalizePSF(uint3 id : SV_DispatchThreadID) {
	float4 original = WaveInput.Load(int4(id, 0));
	float2 position = WaveDomain(float2(id.xy) - 256);
	float2 lo = WaveDomain(float2(id.xy) - 256 - .5), hi = WaveDomain(float2(id.xy) - 256 + .5);
	float area = (hi.x - lo.x) * (hi.y - lo.y);
	float2 core = 1.5 / WaveSampling[id.z].xy;
	float radius = length(position);
	float start = WavePupilSize == 512 ? 128 : 64, end = WavePupilSize == 512 ? 224 : 128;
	if (WaveSurface.w > 0) {
		// Preserve resolved interior-screen frequencies before switching to the
		// aperture-boundary asymptote, which contains no interior defects.
		start = 192;
		end = 240;
	}
	float blend = smoothstep(start, end, radius);
	float nearMass = 0, farDensity = 0;
	[branch] if (blend < 1) nearMass = max(IntegrateWaveFFT(lo, hi, id.z) - IntegrateWaveFFT(max(lo, -core), min(hi, core), id.z), 0);
	[branch] if (blend > 0) farDensity = BoundaryDiffraction(position, id.z, original.w);
	float support = 1 - smoothstep(4096, 8192, radius);
	float tail = lerp(nearMass, farDensity * area, blend) * support;
	DiffractionPSF[id] = float4(original.x, tail, tail / max(area, 1e-20), original.w);
	// Retain the analytic ensemble for 2-12 micrometre scratches below this
	// pupil grid's resolution. Resolved grooves/film interfere coherently above;
	// the fine population supplies the broad-angle tail without FFT aliasing.
	if (WaveSurface.x > 0) {
		float scatter = max(SurfaceMass(lo, hi, id.z) - SurfaceMass(max(lo, -core), min(hi, core), id.z), 0) * support;
		float2 pixel = float2(id.xy) - 256;
		float inner = SurfaceMass(max(pixel - .5, -core), min(pixel + .5, core), id.z);
		DiffractionPSF[id] = float4(original.x, tail, scatter, inner);
	}
}

	// Exact box integrals of the weak PSF tail support extended emitters with four
	// lookups per pixel, rather than replacing their area with a point star.
	[numthreads(512, 1, 1)] void PrefixTail(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
	// Sum from each outer edge toward the optical axis. A single global prefix
	// loses the faint wings when subtracting nearly equal core-sized totals.
	uint coordinate = lane < 256 ? lane : 767 - lane;
	float4 value = WaveInput.Load(int4(coordinate, group.x, group.y, 0));
	FourierValues[lane] = value;
	GroupMemoryBarrierWithGroupSync();
	[unroll] for (uint offset = 1; offset < 256; offset <<= 1)
	{
		float add = lane % 256 >= offset ? FourierValues[lane - offset].y : 0;
		GroupMemoryBarrierWithGroupSync();
		FourierValues[lane].y += add;
		GroupMemoryBarrierWithGroupSync();
	}
	WaveTransform[uint3(group.x, coordinate, group.y)] = FourierValues[lane];
}

groupshared float3 CoreSums[1536];
groupshared float TailBounds[512];
[numthreads(512, 1, 1)] void BuildCoreKernel(uint3 group : SV_GroupID, uint lane : SV_GroupIndex) {
	uint band = group.x;
	float2 step = WaveSampling[band].xy * (WaveStarburst.w > 1 ? .5 : 1);
	float x = (float(lane) - 256) * step.x;
	float bound = 0;
	[loop] for (uint iy = 0; iy < 512; ++iy)
	{
		float2 position = WaveDomain(float2(lane, iy) - 256);
		float r2 = dot(position, position);
		if (r2 >= 1024)
			bound = max(bound, WaveInput.Load(int4(lane, iy, band, 0)).z * r2);
	}
	TailBounds[lane] = bound;
	float3 columns = max(min(x + .5 * step.x, float3(-.5, .5, 1.5)) - max(x - .5 * step.x, float3(-1.5, -.5, .5)), 0) / step.x;
	float3 rows[3] = { float3(0, 0, 0), float3(0, 0, 0), float3(0, 0, 0) };
	if (any(columns > 0)) {
		for (uint iy = 0; iy < 512; ++iy) {
			float y = (float(iy) - 256) * step.y;
			float3 coverage = max(min(y + .5 * step.y, float3(-.5, .5, 1.5)) - max(y - .5 * step.y, float3(-1.5, -.5, .5)), 0) / step.y;
			float value = WaveInput.Load(int4(lane, iy, band, 0)).x;
			[unroll] for (uint row = 0; row < 3; ++row) rows[row] += value * coverage[row] * columns;
		}
	}
	[unroll] for (uint row = 0; row < 3; ++row) CoreSums[row * 512 + lane] = rows[row];
	GroupMemoryBarrierWithGroupSync();
	[unroll] for (uint stride = 256; stride > 0; stride >>= 1)
	{
		if (lane < stride) {
			TailBounds[lane] = max(TailBounds[lane], TailBounds[lane + stride]);
			[unroll] for (uint row = 0; row < 3; ++row) CoreSums[row * 512 + lane] += CoreSums[row * 512 + lane + stride];
		}
		GroupMemoryBarrierWithGroupSync();
	}
	if (lane == 0) {
		float mass = dot(CoreSums[0] + CoreSums[512] + CoreSums[1024], 1);
		float tailMass = WaveInput.Load(int4(255, 255, band, 0)).y + WaveInput.Load(int4(256, 255, band, 0)).y +
		                 WaveInput.Load(int4(255, 256, band, 0)).y + WaveInput.Load(int4(256, 256, band, 0)).y;
		[unroll] for (uint row = 0; row < 3; ++row) WaveKernel[band * 4 + row] = float4(CoreSums[row * 512] / max(mass, 1e-20), 0);
		WaveKernel[band * 4 + 3] = float4(tailMass, 1 - tailMass, max(1 - mass - tailMass, 0), 2 * TailBounds[0]);
		if (WaveStarburst.w > 0)
			WaveKernel[band * 4 + 3] = float4(tailMass, mass, rcp(max(mass + tailMass, 1e-20)), 2 * TailBounds[0]);
	}
}

// Conservative maximum-density hierarchy for screen-tile culling. It keeps
// narrow physical spikes even when the other pixels of a distant tile are dark.
RWTexture2DArray<float> WaveMaxOutput : register(u3);
groupshared float WaveMaxValues[64];
[numthreads(8, 8, 1)] void BuildWaveMax(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID, uint lane : SV_GroupIndex) {
	WaveMaxValues[lane] = WaveInput.Load(int4(group.xy * 8 + thread.xy, group.z, 0)).z;
	GroupMemoryBarrierWithGroupSync();
	[unroll] for (uint step = 32; step > 0; step >>= 1)
	{
		if (lane < step)
			WaveMaxValues[lane] = max(WaveMaxValues[lane], WaveMaxValues[lane + step]);
		GroupMemoryBarrierWithGroupSync();
	}
	if (lane == 0)
		WaveMaxOutput[group] = WaveMaxValues[0];
}
	[numthreads(8, 8, 1)] void ReduceWaveMax(uint3 id : SV_DispatchThreadID)
{
	uint w, h, layers;
	WaveMaxOutput.GetDimensions(w, h, layers);
	if (any(id.xy >= uint2(w, h)))
		return;
	float peak = 0;
	[unroll] for (uint y = 0; y < 2; ++y)[unroll] for (uint x = 0; x < 2; ++x)
		peak = max(peak, WaveInput.Load(int4(id.xy * 2 + uint2(x, y), id.z, 0)).x);
	WaveMaxOutput[id] = peak;
}

StructuredBuffer<float4> SurfaceNormalization : register(t2);
float SampleWaveComponent(float2 p, uint band, uint component)
{
	int2 base = int2(floor(p));
	float2 f = frac(p);
	float a = WaveInput.Load(int4(base, band, 0))[component];
	float b = WaveInput.Load(int4(base + int2(1, 0), band, 0))[component];
	float c = WaveInput.Load(int4(base + int2(0, 1), band, 0))[component];
	float d = WaveInput.Load(int4(base + 1, band, 0))[component];
	return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}
// Hullin section 3.3 integrates scaled power spectra (5 nm in the paper). Three
// monochromatic RGB PSFs alone leave false high-contrast radial fringes. This
// finite-band integral is cached, so the runtime tail/composite cost is unchanged.
[numthreads(8, 8, 1)] void FilterWaveSpectrum(uint3 id : SV_DispatchThreadID) {
	float2 position = WaveDomain(float2(id.xy) - 256);
	float2 lo = WaveDomain(float2(id.xy) - 256 - .5), hi = WaveDomain(float2(id.xy) - 256 + .5);
	float area = (hi.x - lo.x) * (hi.y - lo.y);
	float2 core = 1.5 / WaveSampling[id.z].xy;
	float2 overlap = max(min(hi, core) - max(lo, -core), 0);
	float tailArea = max(area - overlap.x * overlap.y, 0);
	float central = 0, density = 0;
	[loop] for (uint sample = 0; sample < 31; ++sample)
	{
		float weight = LensSpectrumWeights[sample][id.z];
		if (weight < 1e-6)
			continue;
		float wavelength = .4 + .01 * sample;
		uint referenceBand = wavelength > .6 ? 0 : wavelength > .5 ? 1 :
		                                                             2;
		float referenceWavelength = .65 - .1 * referenceBand;
		float2 ratio = WaveSampling[id.z].xy / WaveSampling[referenceBand].xy * (referenceWavelength / wavelength);
		float jacobian = ratio.x * ratio.y;
		central += SampleWaveComponent((float2(id.xy) - 256) * ratio + 256, referenceBand, 0) * jacobian * weight;
		float2 q = position * ratio;
		float value = length(q) < 32 ? SampleWaveComponent(q + 256, referenceBand, 0) :
		                               SampleWaveComponent(WaveDomainInverse(q) + 256, referenceBand, 2);
		density += value * jacobian * weight;
	}
	DiffractionPSF[id] = float4(central, density * tailArea, density * tailArea / max(area, 1e-20), 0);
} groupshared float SurfaceSums[512];
[numthreads(512, 1, 1)] void NormalizeSurfaceEnergy(uint3 group : SV_GroupID, uint lane : SV_GroupIndex) {
	float mass = 0;
	for (uint y = 0; y < 512; ++y) {
		float4 value = WaveInput.Load(int4(lane, y, group.x, 0));
		mass += value.z + value.w;
	}
	SurfaceSums[lane] = mass;
	GroupMemoryBarrierWithGroupSync();
	for (uint stride = 256; stride > 0; stride >>= 1) {
		if (lane < stride)
			SurfaceSums[lane] += SurfaceSums[lane + stride];
		GroupMemoryBarrierWithGroupSync();
	}
	if (lane == 0)
		WaveKernel[group.x * 4] = float4(rcp(max(SurfaceSums[0], 1e-20)), 0, 0, 0);
}
	[numthreads(8, 8, 1)] void ApplySurfaceDiffraction(uint3 id : SV_DispatchThreadID)
{
	float4 value = WaveInput.Load(int4(id, 0));
	float fraction = .003 * saturate(WaveSurface.x);
	float normalization = SurfaceNormalization[id.z * 4].x;
	float2 lo = WaveDomain(float2(id.xy) - 256 - .5), hi = WaveDomain(float2(id.xy) - 256 + .5);
	float area = (hi.x - lo.x) * (hi.y - lo.y);
	float tail = (1 - fraction) * value.y + fraction * value.z * normalization;
	// value.w is already clipped to the 3x3 sensor core. Divide out coverage
	// because BuildCoreKernel integrates the uniform-grid density a second time.
	float2 core = 1.5 / WaveSampling[id.z].xy;
	float2 pixel = float2(id.xy) - 256;
	float2 coverage = max(min(pixel + .5, core) - max(pixel - .5, -core), 0);
	float central = (1 - fraction) * value.x + fraction * value.w * normalization / max(coverage.x * coverage.y, 1e-20);
	DiffractionPSF[id] = float4(central, tail, tail / max(area, 1e-20), 0);
}
