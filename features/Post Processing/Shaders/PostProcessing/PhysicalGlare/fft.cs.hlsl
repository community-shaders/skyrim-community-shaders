// Physical Glare — Stockham radix-2 FFT (row/column pass)
// Community Shaders / Post Processing — Author: Jiaye, 2026
//
// One-dimensional DFT via the Stockham auto-sort algorithm using
// groupshared memory.  Compiled with defines:
//   ROW_PASS / COL_PASS — selects transform axis.
//   FORWARD / INVERSE   — selects twiddle factor sign.
// Each thread group processes one row or column; dispatch (N, 1, 1).
//
// References:
//   [1] Delavennat (2021), Physically-based Real-time Glare, LiU.

// Input complex texture (RG32F: R=real, G=imaginary)
Texture2D<float2> TexInput : register(t0);

// Output complex texture
RWTexture2D<float2> RWTexOutput : register(u0);

cbuffer GlareCB : register(b1)
{
	float Threshold;
	float Intensity;
	float ScatterStrength;
	uint ApertureMode;

	int ApertureBlades;
	float ApertureRotation;
	float AdaptSpeed;
	float DeltaTime;

	uint FFTResolution;
	float PaddingRatio;
	float ScreenWidth;
	float ScreenHeight;

	uint ChannelIndex;
	float FresnelExponent;
	float ChromaticSpread;
	float ApertureSize;

	float PSFSharpness;
	float PSFNoiseFloor;
	uint EnableEyelashes;
	float EyelashCurvature;
};

static const float PI = 3.14159265358979323846;

// Max FFT size supported (must match FFT_MAX in C++ code)
#define MAX_FFT_SIZE 1024

// Shared memory for the FFT butterfly operations
// Two buffers for ping-pong  (2 × 1024 × 8B = 16 KB, within 32 KB CS 5.0 limit)
groupshared float2 gs_buffer0[MAX_FFT_SIZE];
groupshared float2 gs_buffer1[MAX_FFT_SIZE];

// Complex multiplication
float2 ComplexMul(float2 a, float2 b)
{
	return float2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

// Compute twiddle factor W_N^k = exp(-2*pi*i*k/N) for forward, exp(+2*pi*i*k/N) for inverse
float2 Twiddle(uint k, uint N)
{
#ifdef INVERSE
	float angle = 2.0 * PI * float(k) / float(N);
#else
	float angle = -2.0 * PI * float(k) / float(N);
#endif
	float s, c;
	sincos(angle, s, c);
	return float2(c, s);
}

// Each group processes one row or column.
// 1024 threads = CS 5.0 max; handles up to 1024-point FFT.
// Smaller FFTs guard with `active = (threadIdx < N)`.
[numthreads(1024, 1, 1)] void CS_FFT(uint3 groupId : SV_GroupID, uint threadIdx : SV_GroupThreadID) {
	uint lineIdx = groupId.x;  // which row or column
	uint N = FFTResolution;

	bool active = (threadIdx < N);

	// Load input data into shared memory
	if (active) {
		uint2 readPos;
#ifdef ROW_PASS
		readPos = uint2(threadIdx, lineIdx);
#else
		readPos = uint2(lineIdx, threadIdx);
#endif
		gs_buffer0[threadIdx] = TexInput[readPos];
	}

	GroupMemoryBarrierWithGroupSync();

	// Bit-reversal permutation
	if (active) {
		uint bits = firstbithigh(N) - firstbithigh(1);  // log2(N)
		uint rev = 0;
		uint tmp = threadIdx;
		for (uint b = 0; b < bits; b++) {
			rev = (rev << 1) | (tmp & 1);
			tmp >>= 1;
		}
		gs_buffer1[rev] = gs_buffer0[threadIdx];
	}

	GroupMemoryBarrierWithGroupSync();

	// Copy back to buffer0 for butterfly stages
	if (active)
		gs_buffer0[threadIdx] = gs_buffer1[threadIdx];

	GroupMemoryBarrierWithGroupSync();

	// Iterative Cooley-Tukey butterfly
	for (uint stage = 1; stage < N; stage <<= 1) {
		if (active) {
			uint halfStage = stage;
			uint fullStage = stage << 1;

			uint butterflyGroup = threadIdx / fullStage;
			uint butterflyIdx = threadIdx % fullStage;

			if (butterflyIdx < halfStage) {
				uint topIdx = butterflyGroup * fullStage + butterflyIdx;
				uint botIdx = topIdx + halfStage;

				float2 tw = Twiddle(butterflyIdx, fullStage);
				float2 top = gs_buffer0[topIdx];
				float2 bot = ComplexMul(tw, gs_buffer0[botIdx]);

				gs_buffer1[topIdx] = top + bot;
				gs_buffer1[botIdx] = top - bot;
			}
		}

		GroupMemoryBarrierWithGroupSync();

		if (active)
			gs_buffer0[threadIdx] = gs_buffer1[threadIdx];

		GroupMemoryBarrierWithGroupSync();
	}

	// Write output
	if (active) {
		float2 result = gs_buffer0[threadIdx];
#ifdef INVERSE
		result /= float(N);
#endif

		uint2 writePos;
#ifdef ROW_PASS
		writePos = uint2(threadIdx, lineIdx);
#else
		writePos = uint2(lineIdx, threadIdx);
#endif

		RWTexOutput[writePos] = result;
	}
}
