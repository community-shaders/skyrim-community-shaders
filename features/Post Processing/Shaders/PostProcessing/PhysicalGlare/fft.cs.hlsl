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

// Specialised by C++ for each supported resolution.
#ifndef FFT_SIZE
#	define FFT_SIZE 1024
#endif

// Shared memory for the FFT butterfly operations and one twiddle table
// (20 KB at 1024 points, within the CS 5.0 32 KB limit).
groupshared float2 gs_buffer0[FFT_SIZE];
groupshared float2 gs_buffer1[FFT_SIZE];
groupshared float2 gs_twiddle[FFT_SIZE / 2];

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

// Each group processes one row or column. FFT_SIZE exactly matches N for the
// selected shader variant, so every launched thread contributes useful work.
[numthreads(FFT_SIZE, 1, 1)] void CS_FFT(uint3 groupId : SV_GroupID, uint threadIdx : SV_GroupThreadID) {
	uint lineIdx = groupId.x;  // which row or column
	uint N = FFTResolution;

	// Load directly into bit-reversed order. This is identical to the previous
	// load -> bit-reversal -> copy sequence, without two extra group barriers.
	uint2 readPos;
#ifdef ROW_PASS
	readPos = uint2(threadIdx, lineIdx);
#else
	readPos = uint2(lineIdx, threadIdx);
#endif
	uint bits = firstbithigh(N) - firstbithigh(1);  // log2(N)
	uint rev = reversebits(threadIdx) >> (32 - bits);
	gs_buffer0[rev] = TexInput[readPos];
	if (threadIdx < N / 2)
		gs_twiddle[threadIdx] = Twiddle(threadIdx, N);

	GroupMemoryBarrierWithGroupSync();

	// Iterative Cooley-Tukey butterfly. Alternate the two shared buffers
	// instead of copying every stage back to buffer0.
	bool readFromBuffer0 = true;
	for (uint stage = 1; stage < N; stage <<= 1) {
		uint halfStage = stage;
		uint fullStage = stage << 1;
		uint butterflyGroup = threadIdx / fullStage;
		uint butterflyIdx = threadIdx % fullStage;

		if (butterflyIdx < halfStage) {
			uint topIdx = butterflyGroup * fullStage + butterflyIdx;
			uint botIdx = topIdx + halfStage;
			float2 tw = gs_twiddle[butterflyIdx * (N / fullStage)];
			float2 top;
			float2 bot;
			if (readFromBuffer0) {
				top = gs_buffer0[topIdx];
				bot = ComplexMul(tw, gs_buffer0[botIdx]);
				gs_buffer1[topIdx] = top + bot;
				gs_buffer1[botIdx] = top - bot;
			} else {
				top = gs_buffer1[topIdx];
				bot = ComplexMul(tw, gs_buffer1[botIdx]);
				gs_buffer0[topIdx] = top + bot;
				gs_buffer0[botIdx] = top - bot;
			}
		}

		GroupMemoryBarrierWithGroupSync();
		readFromBuffer0 = !readFromBuffer0;
	}

	// Write output
	{
		float2 result = readFromBuffer0 ? gs_buffer0[threadIdx] : gs_buffer1[threadIdx];
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
