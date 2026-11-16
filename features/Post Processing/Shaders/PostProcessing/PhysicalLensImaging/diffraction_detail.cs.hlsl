// Cache-only, zero-padded pupil propagation. The screen-space atlas
// remains 512 squared; only its generating Fourier field has higher density.
#include "PostProcessing/PhysicalLensImaging/diffraction.cs.hlsl"

static const uint DetailSize = 1024;
groupshared float4 DetailValues[1024];

// Integral of a unit disk over [0,x] x [0,y], extended with signed limits.
// Analytic pixel coverage preserves tiny opaque particles' blocked area; a
// blurred distance field would make them progressively transparent.
float PaperDiskPrimitive(float2 q)
{
	float2 a = min(abs(q), 1);
	float t = min(a.x, sqrt(max(1 - a.y * a.y, 0)));
	float integral = a.y * t + .5 * (a.x * sqrt(max(1 - a.x * a.x, 0)) + asin(a.x) - t * sqrt(max(1 - t * t, 0)) - asin(t));
	return integral * sign(q.x) * sign(q.y);
}
float PaperDiskCoverage(float2 delta, float radius, float pixelWidth)
{
	float coverage = 0;
	float2 nearCorner = max(abs(delta) - .5 * pixelWidth, 0);
	if (dot(nearCorner, nearCorner) < radius * radius) {
		float2 farCorner = abs(delta) + .5 * pixelWidth;
		if (dot(farCorner, farCorner) <= radius * radius) {
			coverage = 1;
		} else {
			float2 lo = (delta - .5 * pixelWidth) / radius, hi = (delta + .5 * pixelWidth) / radius;
			float area = PaperDiskPrimitive(hi) - PaperDiskPrimitive(float2(lo.x, hi.y)) - PaperDiskPrimitive(float2(hi.x, lo.y)) + PaperDiskPrimitive(lo);
			coverage = saturate(area * radius * radius / (pixelWidth * pixelWidth));
		}
	}
	return coverage;
}

// Hullin et al. 2011, Sec. 5 / Fig. 14: dots and lines in the real-valued
// transmission mask. Counts, size distributions and angular clusters below
// are empirical artistic statistics, NOT values published by the paper.
float PupilTransmission(float2 q, float pixelWidth, uint seed)
{
	float transmission = 1;
	seed = seed * 173 + 119;
	// Independent populations of fine dust, medium particles and rare larger
	// marks. Two independently jittered candidates per cell avoid enforcing
	// a minimum particle spacing. At density 1: about 3,000 particles per pupil.
	[loop] for (uint layer = 0; layer < 3; ++layer)
	{
		float cellWidth = layer == 0 ? .035 : layer == 1 ? .075 :
		                                                   .21;
		float minRadius = layer == 0 ? .0018 : layer == 1 ? .004 :
		                                                    .012;
		float maxRadius = layer == 0 ? .0045 : layer == 1 ? .012 :
		                                                    .028;
		float occupancy = layer == 2 ? .18 : .5;
		int2 cell = int2(floor(q / cellWidth));
		[loop] for (int y = -1; y <= 1; ++y)[loop] for (int x = -1; x <= 1; ++x)
		{
			int2 c = cell + int2(x, y);
			[unroll] for (uint candidate = 0; candidate < 2; ++candidate)
			{
				uint key = uint(c.x) * 73856093u ^ uint(c.y) * 19349663u ^ (seed + layer * 104729 + candidate * 15485863);
				if (MicroRandom(key + 2) >= occupancy * WavePupilDirt.x)
					continue;
				float2 center = (float2(c) + float2(MicroRandom(key), MicroRandom(key + 1))) * cellWidth;
				float radius = exp2(lerp(log2(minRadius), log2(maxRadius), MicroRandom(key + 3))) * WavePupilDirt.y;
				float coverage = PaperDiskCoverage(q - center, radius, pixelWidth);
				transmission *= 1 - WaveSurface.w * coverage;
			}
		}
	}
	// Some scratches share a preferred orientation, with small independent
	// deviations and unequal lengths. Their FFT produces neighboring fine
	// streaks and interference; no spokes are drawn in the output image.
	[loop] for (uint i = 0; i < 64; ++i)
	{
		uint group = i < 48 ? i / 3 : i - 32;
		uint key = seed + 571 + 29 * i, family = seed + 104729 + 31 * group;
		if (MicroRandom(key + 7) >= .5 * WavePupilDirt.x)
			continue;
		float2 center = (float2(MicroRandom(family), MicroRandom(family + 1)) - .5) * 1.8;
		center += (float2(MicroRandom(key), MicroRandom(key + 1)) - .5) * .15;
		float angle = Rotation + ((MicroRandom(family + 2) - .5) + .025 * (MicroRandom(key + 2) - .5)) * WavePupilDirt.w;
		float sn, cs;
		sincos(angle, sn, cs);
		float2 delta = q - center;
		float u = dot(delta, float2(cs, sn)), v = dot(delta, float2(-sn, cs));
		float extent = .04 * pow(20, MicroRandom(key + 3)) * WavePupilDirt.z;
		float width = .0008 * pow(5, MicroRandom(key + 4)) * WavePupilDirt.y;
		float coverage = max(min(v + .5 * pixelWidth, width) - max(v - .5 * pixelWidth, -width), 0) / pixelWidth;
		coverage *= saturate(.5 + (extent - abs(u)) / pixelWidth);
		transmission *= 1 - WaveSurface.w * coverage;
	}
	return transmission;
}

[numthreads(8, 8, 1)] void BuildDetailedPupil(uint3 id : SV_DispatchThreadID) {
	int2 p = int2(id.xy) - int((DetailSize - WavePupilSize) / 2);
	float coverage = 0;
	float transmission = 1;
	if (all(p >= 0) && all(p < int(WavePupilSize))) {
		[loop] for (uint sy = 0; sy < 8; ++sy)[loop] for (uint sx = 0; sx < 8; ++sx)
		{
			float2 q = (float2(p) + (float2(sx, sy) + .5) / 8 - .5 * WavePupilSize) * (2.4 / WavePupilSize);
			float clearance = 1 - length(q);
			if (Blades >= 3) {
				float sector = 2 * PI / Blades, angle = atan2(q.y, q.x) - Rotation;
				float delta = angle - sector * floor(angle / sector + .5);
				float2 local = length(q) * float2(cos(delta), sin(delta));
				float apothem = cos(PI / Blades), sag = (1 - apothem) * BladeCurvature;
				float edgeLength = 2 * sin(PI / Blades);
				clearance = min(clearance, apothem + sag - local.x - 4 * sag * local.y * local.y / (edgeLength * edgeLength));
				if (BladeCurvature < 0)
					clearance = CameraIrisClearance(q, Blades, Rotation, BladeCurvature);
			}
			coverage += clearance >= 0 ? 1. / 64 : 0;
		}
		if (WaveSurface.w > 0 && coverage > 0) {
			float step = 2.4 / WavePupilSize;
			transmission = PupilTransmission((float2(p) + .5 - .5 * WavePupilSize) * step, step, uint(WaveSurface.y));
		}
	}
	float amplitude = coverage * transmission;
	WavePupil[id] = float4(amplitude, 0, 0, amplitude * amplitude);
}

	[numthreads(512, 1, 1)] void TransformDetailed(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
	[unroll] for (uint part = 0; part < 2; ++part)
	{
		uint x = lane + part * 512;
		DetailValues[reversebits(x) >> 22] = WaveInput.Load(int4(x, group.x, group.y, 0));
	}
	GroupMemoryBarrierWithGroupSync();
	[unroll] for (uint length = 2; length <= DetailSize; length *= 2)
	{
		uint offset = lane % (length / 2), index = lane / (length / 2) * length + offset;
		float4 a = DetailValues[index], b = DetailValues[index + length / 2];
		float2 rotated = WaveComplexMultiply(b.xy, WavePhaseFactor(-2 * PI * offset / length));
		DetailValues[index] = float4(a.xy + rotated, 0, a.w + b.w);
		DetailValues[index + length / 2] = float4(a.xy - rotated, 0, a.w + b.w);
		GroupMemoryBarrierWithGroupSync();
	}
	[unroll] for (uint storePart = 0; storePart < 2; ++storePart)
	{
		uint x = lane + storePart * 512;
		WaveTransform[uint3(group.x, x, group.y)] = DetailValues[x];
	}
}

[numthreads(8, 8, 1)] void NormalizeDetailed(uint3 id : SV_DispatchThreadID) {
	float4 value = WaveInput.Load(int4((id.xy + 512) % DetailSize, id.z, 0));
	float intensity = dot(value.xy, value.xy) / max(value.w * DetailSize * DetailSize, 1e-30);
	WaveTransform[id] = float4(intensity, intensity, 0, 0);
}

	// Outward-to-inward quadrant sums avoid subtracting the bright central peak
	// when integrating the weak diffraction wings.
	[numthreads(512, 1, 1)] void PrefixDetailed(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
	[unroll] for (uint half = 0; half < 2; ++half)
	{
		uint x = half == 0 ? lane : 1023 - lane;
		DetailValues[lane + half * 512] = WaveInput.Load(int4(x, group.x, group.y, 0));
	}
	GroupMemoryBarrierWithGroupSync();
	[unroll] for (uint offset = 1; offset < 512; offset <<= 1)
	{
		float2 add = lane >= offset ? float2(DetailValues[lane - offset].y, DetailValues[lane + 512 - offset].y) : 0;
		GroupMemoryBarrierWithGroupSync();
		DetailValues[lane].y += add.x;
		DetailValues[lane + 512].y += add.y;
		GroupMemoryBarrierWithGroupSync();
	}
	[unroll] for (uint storeHalf = 0; storeHalf < 2; ++storeHalf)
	{
		uint x = storeHalf == 0 ? lane : 1023 - lane;
		WaveTransform[uint3(group.x, x, group.y)] = DetailValues[lane + storeHalf * 512];
	}
}
