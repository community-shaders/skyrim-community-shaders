RWTexture2D<float4> tex_noise : register(u0);

static const float GAUSSIAN_AVG = 0.0;
static const float GAUSSIAN_STD = 1.0;

uint CoordToFlatId(uint2 coord, uint width)
{
	return coord.y * width + coord.x;
}

float PackFloats(float a, float b)
{
	uint a16 = f32tof16(a);
	uint b16 = f32tof16(b);
	uint abPacked = (a16 << 16) | b16;
	return asfloat(abPacked);
}

uint WangHash(uint seed)
{
	seed = (seed ^ 61) ^ (seed >> 16);
	seed *= 9;
	seed = seed ^ (seed >> 4);
	seed *= 0x27d4eb2d;
	seed = seed ^ (seed >> 15);
	return seed;
}

void RandXorshift(inout uint rngState)
{
	// Xorshift algorithm from George Marsaglia's paper
	rngState ^= (rngState << 13);
	rngState ^= (rngState >> 17);
	rngState ^= (rngState << 5);
}

float RandXorshiftFloat(inout uint rngState)
{
	RandXorshift(rngState);
	float res = float(rngState) * (1.0 / 4294967296.0);
	return res;
}

float Erf(float x)
{
	// Save the sign of x
	int sign = 1;
	if (x < 0)
		sign = -1;
	x = abs(x);

	// A&S formula 7.1.26
	float t = 1.0 / (1.0 + 0.3275911 * x);
	float y = 1.0 - (((((1.061405429 * t + -1.453152027) * t) + 1.421413741) * t + -0.284496736) * t + 0.254829592) * t * exp(-x * x);

	return sign * y;
}

float ErfInv(float x)
{
	float w, p;
	w = -log((1.0f - x) * (1.0f + x));
	if (w < 5.000000f) {
		w = w - 2.500000;
		p = 2.81022636e-08;
		p = 3.43273939e-07 + p * w;
		p = -3.5233877e-06 + p * w;
		p = -4.39150654e-06 + p * w;
		p = 0.00021858087 + p * w;
		p = -0.00125372503 + p * w;
		p = -0.00417768164 + p * w;
		p = 0.246640727 + p * w;
		p = 1.50140941 + p * w;
	} else {
		w = sqrt(w) - 3.000000;
		p = -0.000200214257;
		p = 0.000100950558 + p * w;
		p = 0.00134934322 + p * w;
		p = -0.00367342844 + p * w;
		p = 0.00573950773 + p * w;
		p = -0.0076224613 + p * w;
		p = 0.00943887047 + p * w;
		p = 1.00167406 + p * w;
		p = 2.83297682 + p * w;
	}
	return p * x;
}

float CDF(float x, float mu, float sigma)
{
	float U = 0.5 * (1 + Erf((x - mu) / (sigma * sqrt(2.0))));
	return U;
}

float InvCDF(float U, float mu, float sigma)
{
	float x = sigma * sqrt(2.0) * ErfInv(2.0 * U - 1.0) + mu;
	return x;
}

[numthreads(32, 32, 1)] void main(const uint2 tid : SV_DispatchThreadID) {
	uint2 size;
	tex_noise.GetDimensions(size.x, size.y);
	int offset = size.x * size.y * 0x69420;

	// Generate random numbers for this cell and the next ones in X and Y
	int2 pixelCoord00 = tid.xy;
	uint rngState00 = WangHash(CoordToFlatId(pixelCoord00 * 123, size.x) + offset);
	float u00 = RandXorshiftFloat(rngState00);
	float g00 = InvCDF(RandXorshiftFloat(rngState00), GAUSSIAN_AVG, GAUSSIAN_STD);

	int2 pixelCoord01 = (pixelCoord00 + int2(0, 1)) % size;
	uint rngState01 = WangHash(CoordToFlatId(pixelCoord01 * 123, size.x) + offset);
	float u01 = RandXorshiftFloat(rngState01);
	float g01 = InvCDF(RandXorshiftFloat(rngState01), GAUSSIAN_AVG, GAUSSIAN_STD);

	int2 pixelCoord10 = (pixelCoord00 + int2(1, 0)) % size;
	uint rngState10 = WangHash(CoordToFlatId(pixelCoord10 * 123, size.x) + offset);
	float u10 = RandXorshiftFloat(rngState10);
	float g10 = InvCDF(RandXorshiftFloat(rngState10), GAUSSIAN_AVG, GAUSSIAN_STD);

	int2 pixelCoord11 = (pixelCoord00 + int2(1, 1)) % size;
	uint rngState11 = WangHash(CoordToFlatId(pixelCoord11 * 123, size.x) + offset);
	float u11 = RandXorshiftFloat(rngState11);
	float g11 = InvCDF(RandXorshiftFloat(rngState11), GAUSSIAN_AVG, GAUSSIAN_STD);

	// Pack 8 values into 4
	tex_noise[tid.xy] = float4(PackFloats(u00, g00), PackFloats(u01, g01), PackFloats(u10, g10), PackFloats(u11, g11));
}