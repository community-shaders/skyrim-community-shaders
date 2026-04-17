// Simple hash-based random number generation for compute shaders

#ifndef SSRT_RANDOM_HLSLI
#define SSRT_RANDOM_HLSLI

// Wang hash for seed generation
uint wangHash(uint seed)
{
	seed = (seed ^ 61) ^ (seed >> 16);
	seed *= 9;
	seed = seed ^ (seed >> 4);
	seed *= 0x27d4eb2d;
	seed = seed ^ (seed >> 15);
	return seed;
}

struct RandomState
{
	uint state;

	float rand()
	{
		state = wangHash(state);
		return float(state) / 4294967295.0f;
	}

	float2 rand2()
	{
		return float2(rand(), rand());
	}

	uint randInt(uint maxVal)
	{
		return uint(rand() * maxVal) % maxVal;
	}
};

RandomState MakeRandom(uint id, uint frame)
{
	RandomState r;
	r.state = wangHash(id * 1973u + frame * 277u + 1u);
	return r;
}

#endif  // SSRT_RANDOM_HLSLI
