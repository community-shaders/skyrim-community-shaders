#ifndef __RANDOM_DEPENDENCY_HLSL__
#define __RANDOM_DEPENDENCY_HLSL__

namespace Random
{

	// https://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare/
	const static float2 SpiralSampleOffsets8[] = {
		{ -0.7071, 0.7071 },
		{ -0.0000, -0.8750 },
		{ 0.5303, 0.5303 },
		{ -0.6250, -0.0000 },
		{ 0.3536, -0.3536 },
		{ -0.0000, 0.3750 },
		{ -0.1768, -0.1768 },
		{ 0.1250, 0.0000 }
	};

	const static float2 PoissonSampleOffsets16[] = {
		{ -0.39721655431143005f, 0.8056832047758435f },
		{ 0.5039282018939948f, 0.8175656806923035f },
		{ -0.14008842764914875f, 0.5239821992425406f },
		{ -0.9385950332618932f, 0.3214309237575714f },
		{ 0.25282140190834174f, 0.37924512167905416f },
		{ 0.9223936032296853f, 0.332539962017578f },
		{ -0.5496451570034276f, 0.1790707762170039f },
		{ -0.37302426066908373f, -0.13877390665733164f },
		{ 0.03579198121130009f, -0.15676850000512513f },
		{ 0.4919267941196914f, -0.23310683759624068f },
		{ -0.8035590829710795f, -0.29468514494873416f },
		{ 0.8535425755478637f, -0.27933954067261685f },
		{ -0.34182152498958196f, -0.6383963127313933f },
		{ 0.0426828706429914f, -0.554272463115887f },
		{ 0.47470719643897386f, -0.7164602403811255f },
		{ 0.07000602455182418f, -0.9868660954557704f },
	};

	///////////////////////////////////////////////////////////
	// WHITE-LIKE HASHES
	///////////////////////////////////////////////////////////

	// https://www.shadertoy.com/view/XlGcRh
	// Helper Functions
	uint rotl(uint x, uint r)
	{
		return (x << r) | (x >> (32u - r));
	}

	uint rotr(uint x, uint r)
	{
		return (x >> r) | (x << (32u - r));
	}

	uint fmix(uint h)
	{
		h ^= h >> 16;
		h *= 0x85ebca6bu;
		h ^= h >> 13;
		h *= 0xc2b2ae35u;
		h ^= h >> 16;
		return h;
	}

	uint murmur3(uint3 x, uint seed = 0)
	{
		static const uint c1 = 0xcc9e2d51u;
		static const uint c2 = 0x1b873593u;

		uint h = seed;
		uint k = x.x;

		k *= c1;
		k = rotl(k, 15u);
		k *= c2;

		h ^= k;
		h = rotl(h, 13u);
		h = h * 5u + 0xe6546b64u;

		k = x.y;

		k *= c1;
		k = rotl(k, 15u);
		k *= c2;

		h ^= k;
		h = rotl(h, 13u);
		h = h * 5u + 0xe6546b64u;

		k = x.z;

		k *= c1;
		k = rotl(k, 15u);
		k *= c2;

		h ^= k;
		h = rotl(h, 13u);
		h = h * 5u + 0xe6546b64u;

		h ^= 12u;

		return fmix(h);
	}

	uint pcg(uint v)
	{
		uint state = v * 747796405u + 2891336453u;
		uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
		return (word >> 22u) ^ word;
	}

	uint2 pcg2d(uint2 v)
	{
		v = v * 1664525u + 1013904223u;

		v.x += v.y * 1664525u;
		v.y += v.x * 1664525u;

		v = v ^ (v >> 16u);

		v.x += v.y * 1664525u;
		v.y += v.x * 1664525u;

		v = v ^ (v >> 16u);

		return v;
	}

	uint3 pcg3d(uint3 v)
	{
		v = v * 1664525u + 1013904223u;

		v.x += v.y * v.z;
		v.y += v.z * v.x;
		v.z += v.x * v.y;

		v ^= v >> 16u;

		v.x += v.y * v.z;
		v.y += v.z * v.x;
		v.z += v.x * v.y;

		return v;
	}

	uint iqint3(uint2 x)
	{
		uint2 q = 1103515245U * ((x >> 1U) ^ (x.yx));
		uint n = 1103515245U * ((q.x) ^ (q.y >> 3U));

		return n;
	}

	///////////////////////////////////////////////////////////
	// BLUE-LIKE HASHES / LOW DISCREPANCY SEQUENCES
	///////////////////////////////////////////////////////////

	// Derived from the interleaved gradient function from Jimenez 2014 http://goo.gl/eomGso
	float InterleavedGradientNoise(float2 pxCoord)
	{
		float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
		return frac(magic.z * frac(dot(pxCoord, magic.xy)));
	}

	float InterleavedGradientNoise(float2 pxCoord, uint frame)
	{
		// Temporal factor
		float frameStep = float(frame % 16) * 0.0625f;
		pxCoord.x += frameStep * 4.7526;
		pxCoord.y += frameStep * 3.1914;

		return InterleavedGradientNoise(pxCoord);
	}

	// https://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/
	// https://www.shadertoy.com/view/mts3zN
	float R1Sequence(float idx, float seed = 0)
	{
		return frac(seed + idx * 0.61803398875);
	}

	float R1Modified(float idx, float seed = 0)
	{
		return frac(seed + idx * 0.38196601125);
	}

	float2 R2Sequence(float idx, float2 seed = 0)
	{
		return frac(seed + idx * float2(0.7548776662467, 0.569840290998));
	}

	float2 R2Modified(float idx, float2 seed = 0)
	{
		return frac(seed + idx * float2(0.245122333753, 0.430159709002));
	}

	float3 R3Sequence(float idx, float3 seed = 0)
	{
		return frac(seed + idx * float3(0.8191725133961, 0.6710436067038, 0.5497004779019));
	}

	float3 R3Modified(float idx, float3 seed = 0)
	{
		return frac(seed + idx * float3(0.180827486604, 0.328956393296, 0.450299522098));
	}

	///////////////////////////////////////////////////////////
	// NOISES
	///////////////////////////////////////////////////////////

	// https://www.shadertoy.com/view/slB3z3
	float3 perlinGradient(uint hash)
	{
		switch (int(hash) & 15) {  // look at the last four bits to pick a gradient direction
		case 0:
			return float3(1, 1, 0);
		case 1:
			return float3(-1, 1, 0);
		case 2:
			return float3(1, -1, 0);
		case 3:
			return float3(-1, -1, 0);
		case 4:
			return float3(1, 0, 1);
		case 5:
			return float3(-1, 0, 1);
		case 6:
			return float3(1, 0, -1);
		case 7:
			return float3(-1, 0, -1);
		case 8:
			return float3(0, 1, 1);
		case 9:
			return float3(0, -1, 1);
		case 10:
			return float3(0, 1, -1);
		case 11:
			return float3(0, -1, -1);
		case 12:
			return float3(1, 1, 0);
		case 13:
			return float3(-1, 1, 0);
		case 14:
			return float3(0, -1, 1);
		case 15:
			return float3(0, -1, -1);
		}
	}

	// range: -1 to 1
	float perlinNoise(float3 position, uint seed = 0x578437ADu)
	{
		float3 i_f = floor(position);
		float3 f = position - i_f;
		uint3 i = asuint(int3(i_f));
		float value1 = dot(perlinGradient(murmur3(i, seed)), f);
		float value2 = dot(perlinGradient(murmur3((i + uint3(1, 0, 0)), seed)), f - float3(1, 0, 0));
		float value3 = dot(perlinGradient(murmur3((i + uint3(0, 1, 0)), seed)), f - float3(0, 1, 0));
		float value4 = dot(perlinGradient(murmur3((i + uint3(1, 1, 0)), seed)), f - float3(1, 1, 0));
		float value5 = dot(perlinGradient(murmur3((i + uint3(0, 0, 1)), seed)), f - float3(0, 0, 1));
		float value6 = dot(perlinGradient(murmur3((i + uint3(1, 0, 1)), seed)), f - float3(1, 0, 1));
		float value7 = dot(perlinGradient(murmur3((i + uint3(0, 1, 1)), seed)), f - float3(0, 1, 1));
		float value8 = dot(perlinGradient(murmur3((i + uint3(1, 1, 1)), seed)), f - float3(1, 1, 1));

		float3 u = f * f * (3.0 - 2.0 * f);  // wetness specific
		return lerp(
			lerp(lerp(value1, value2, u.x), lerp(value3, value4, f.x), u.y),
			lerp(lerp(value5, value6, u.x), lerp(value7, value8, f.x), u.y),
			u.z);
	}

}

#endif  // __RANDOM_DEPENDENCY_HLSL__