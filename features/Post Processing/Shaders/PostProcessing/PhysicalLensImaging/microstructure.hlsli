#ifndef PHYSICAL_LENS_MICROSTRUCTURE
#define PHYSICAL_LENS_MICROSTRUCTURE
// Equivalent-pupil optical depth and optical path (micrometres), evaluated in
// millimetres. Matches PhysicalLensMicrostructure.h's independent CPU reference.
float MicroRandom(uint key)
{
	key ^= key >> 16;
	key *= 0x7feb352d;
	key ^= key >> 15;
	key *= 0x846ca68b;
	key ^= key >> 16;
	return (key & 0xffffff) / 16777216.;
}
float2 Microstructure(float2 position, float pixelWidthMM, uint seed)
{
	float2 result = 0;
	float footprint = pixelWidthMM * pixelWidthMM / 12;
	seed = seed * 173 + 119;
	int2 cell = int2(floor(position / .3));
	[unroll] for (int cy = -1; cy <= 1; ++cy)
		[unroll] for (int cx = -1; cx <= 1; ++cx)
	{
		int2 c = cell + int2(cx, cy);
		uint key = uint(c.x) * 73856093u ^ uint(c.y) * 19349663u ^ (seed + 977u);
		float2 delta = position - (float2(c) + float2(MicroRandom(key), MicroRandom(key + 1))) * .3;
		float radius = .015 + .025 * MicroRandom(key + 2), variance = radius * radius + footprint;
		float profile = radius * radius / variance * exp(-.5 * dot(delta, delta) / variance);
		result += float2(.2 + MicroRandom(key + 3), .04 * (MicroRandom(key + 4) - .5)) * profile;
	}
	[loop] for (uint i = 0; i < 32; ++i)
	{
		uint key = seed + 13 * i;
		float2 delta = position - (float2(MicroRandom(key), MicroRandom(key + 1)) - .5) * 16;
		float radius = .05 + .10 * MicroRandom(key + 2);
		float variance = radius * radius + footprint;
		float r2 = dot(delta, delta);
		if (r2 < 20 * variance)
			result.x += (1 + 3 * MicroRandom(key + 3)) * radius * radius / variance * exp(-.5 * r2 / variance);
	}
	[loop] for (uint j = 0; j < 10; ++j)
	{
		uint key = seed + 571 + 11 * j;
		float2 delta = position - (float2(MicroRandom(key), MicroRandom(key + 1)) - .5) * 12;
		float s, c;
		sincos(6.28318530718 * MicroRandom(key + 2), s, c);
		float u = dot(float2(c, s), delta), v = dot(float2(-s, c), delta);
		float extent = .5 + 3 * MicroRandom(key + 3), width = .04 + .06 * MicroRandom(key + 4);
		v -= (MicroRandom(key + 5) - .5) * .3 * u * u;
		float variance = width * width + footprint;
		if (abs(u) < 1.5 * extent && v * v < 20 * variance) {
			float profile = width * rsqrt(variance) * exp(-.5 * v * v / variance - pow(abs(u / extent), 8));
			result += float2(.18, .04 + .18 * MicroRandom(key + 6)) * profile;
		}
	}
	float2 delta = position - (float2(MicroRandom(seed + 701), MicroRandom(seed + 702)) - .5) * 6;
	float period = .10 + .08 * MicroRandom(seed + 703);
	float radius = length(delta + float2(6, 0));
	float envelope = exp(-.5 * dot(delta * delta, float2(.2, 1. / 3)));
	float filtered = exp(-.5 * pow(6.28318530718 / period, 2) * footprint);
	float modulation = .5 + .5 * sin(1.7 * delta.x + .6 * sin(delta.y * 1.3));
	float ridge = cos(6.28318530718 * radius / period + 1.2 * sin(delta.y * 2) + .7 * sin(delta.x * 3.1));
	result += float2(.025 * envelope, .035 * envelope * modulation * filtered * ridge);
	return result;
}
#endif
