// PhysicalGlare - Stockham Radix-2 FFT
// Performs a 1D FFT along rows or columns using groupshared memory.
// Compiled with defines: ROW_PASS or COL_PASS, FORWARD or INVERSE
// Each thread group processes one row/column of the FFT.
// Dispatch: (FFTResolution, 1, 1) - one group per row/column

// Input complex texture (RG32F: R=real, G=imaginary)
Texture2D<float2> TexInput : register(t0);

// Output complex texture
RWTexture2D<float2> RWTexOutput : register(u0);

cbuffer GlareCB : register(b1)
{
	float Threshold : packoffset(c0.x);
	float Intensity : packoffset(c0.y);
	float ScatterStrength : packoffset(c0.z);
	float ChromaticDispersion : packoffset(c0.w);

	int ApertureBlades : packoffset(c1.x);
	float ApertureRotation : packoffset(c1.y);
	float AdaptSpeed : packoffset(c1.z);
	float DeltaTime : packoffset(c1.w);

	uint FFTResolution : packoffset(c2.x);
	float RcpFFTResolution : packoffset(c2.y);
	float ScreenWidth : packoffset(c2.z);
	float ScreenHeight : packoffset(c2.w);

	uint ChannelIndex : packoffset(c3.x);
};

static const float PI = 3.14159265358979323846;

// Max FFT size supported (must match FFT_MAX in C++ code)
#define MAX_FFT_SIZE 512

// Shared memory for the FFT butterfly operations
// Two buffers for ping-pong
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

// Each group processes one row or column
// Thread count = FFTResolution/2 (each thread handles one butterfly)
// Use 256 threads (handles up to 512-point FFT)
[numthreads(256, 1, 1)] void CS_FFT(uint3 groupId : SV_GroupID, uint threadIdx : SV_GroupThreadID) {
	uint lineIdx = groupId.x;  // which row or column
	uint N = FFTResolution;

	if (threadIdx >= N)
		return;

	// Load input data into shared memory
	uint2 readPos;
#ifdef ROW_PASS
	readPos = uint2(threadIdx, lineIdx);
#else
	readPos = uint2(lineIdx, threadIdx);
#endif

	float2 val = (threadIdx < N) ? TexInput[readPos] : float2(0, 0);
	gs_buffer0[threadIdx] = val;

	GroupMemoryBarrierWithGroupSync();

	// Bit-reversal permutation
	uint bits = firstbithigh(N) - firstbithigh(1);  // log2(N)
	uint rev = 0;
	uint tmp = threadIdx;
	for (uint b = 0; b < bits; b++) {
		rev = (rev << 1) | (tmp & 1);
		tmp >>= 1;
	}

	gs_buffer1[rev] = gs_buffer0[threadIdx];

	GroupMemoryBarrierWithGroupSync();

	// Copy back to buffer0 for butterfly stages
	gs_buffer0[threadIdx] = gs_buffer1[threadIdx];

	GroupMemoryBarrierWithGroupSync();

	// Iterative Cooley-Tukey butterfly
	for (uint stage = 1; stage < N; stage <<= 1) {
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

		GroupMemoryBarrierWithGroupSync();

		gs_buffer0[threadIdx] = gs_buffer1[threadIdx];

		GroupMemoryBarrierWithGroupSync();
	}

	// For inverse FFT, divide by N
	float2 result = gs_buffer0[threadIdx];
#ifdef INVERSE
	result /= float(N);
#endif

	// Write output
	uint2 writePos;
#ifdef ROW_PASS
	writePos = uint2(threadIdx, lineIdx);
#else
	writePos = uint2(lineIdx, threadIdx);
#endif

	RWTexOutput[writePos] = result;
}
