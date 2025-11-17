#include "Common/Random.hlsli"

// NOISE_FUNC: Cellular3DTileable
// BASE_FREQ
// OCTAVES
// PERSISTENCE
// LACUNARITY
// SEED: single uint

RWTexture3D<unorm float> RWTexOutput : register(u0);

float3 hash33(int3 seed)
{
	return Random::pcg3d(asuint(seed)) / 4294967295.f;
}

float3 hash13(int3 seed)
{
	return Random::murmur3(asuint(seed)) / 4294967295.f;
}

// Simplified Smoothstep https://www.desmos.com/calculator/un0o21eokv
float smoothValue(float x)
{
	x = clamp(x, 0.0, 1.0);
	return x * x * (3.0 - 2.0 * x);
}

float Worley(float3 uv, uint freq)
{
	int3 id = floor(uv);
	float3 p = frac(uv);

	float minDist = 10000.;
	[unroll] for (int x = -1; x <= 1; ++x)
	{
		[unroll] for (int y = -1; y <= 1; ++y)
		{
			[unroll] for (int z = -1; z <= 1; ++z)
			{
				int3 offset = int3(x, y, z);
				float3 h = hash33((id + offset) % freq) * .5 + .5;
				h += offset;
				float3 d = p - h;
				minDist = min(minDist, dot(d, d));
			}
		}
	}

	// inverted worley noise
	return 1. - minDist;
}

// Alligator Noise, originally from Side Effects:
// https://www.sidefx.com/docs/hdk/alligator_2alligator_8_c-example.html
float Alligator(float3 position, uint freq)
{
	int3 id = floor(position);    // Integer coordinates
	float3 grid = position - id;  // Fractional coordinates

	// Initialize results
	float densest;
	float secondDensest;

	// compare to 3x3x3 neighbor cells
	for (int ix = -1; ix <= 1; ++ix) {
		for (int iy = -1; iy <= 1; ++iy) {
			for (int iz = -1; iz <= 1; ++iz) {
				// Offset to the neighbor cell
				int3 offset = int3(ix, iy, iz);

				// Current Cell coordinates
				int3 cell = id + offset;

				// makes the noise repeat at pos 0-1
				cell = cell % freq;

				// Get random center of the Cell
				float3 center = hash33(cell) + offset;

				// Distance from center
				float dist = distance(grid, center);

				// 'if(dist < 1.0)' doesn't have any effect and doesn't improve
				// performance. See: https://www.shadertoy.com/view/MflGWM

				// Get random density scaled by the distance to the random point
				float density = hash13(cell) * smoothValue(1.0 - dist);

				// find largest values
				if (densest < density) {
					// move previous highest to second place
					secondDensest = densest;
					// update highest to current height
					densest = density;

				} else if (secondDensest < density) {
					// update second highest to current height
					secondDensest = density;
				}
			}
		}
	}
	// Subtract two largest density values for the result
	return densest - secondDensest;
}

[numthreads(8, 8, 1)] void main(uint3 tid
								: SV_DispatchThreadID) {
	uint3 dims;
	RWTexOutput.GetDimensions(dims.x, dims.y, dims.z);
	float3 uvw = (tid + 0.5) / dims;

	float mix = 1, freq = BASE_FREQ, val = 0, mix_sum = 0;
	for (uint i = 0; i < OCTAVES; ++i) {
		uint ifreq = freq;
		uint3 jitter = Random::pcg3d(SEED + i);
		val += mix * NOISE_FUNC(uvw * ifreq + (jitter % 100000), ifreq);
		mix_sum += mix;
		mix *= PERSISTENCE;
		freq *= LACUNARITY;
	}
	val /= mix_sum;

	RWTexOutput[tid] = val;
}