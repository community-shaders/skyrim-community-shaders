// Physical Glare — Cooley-Tukey radix-2 FFT (row/column pass)
// Community Shaders / Post Processing — Author: Jiaye, 2026
//
// One-dimensional DFT via the Cooley-Tukey algorithm using
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

#ifndef FFT_SIZE
#	define FFT_SIZE 1024
#endif

groupshared float2 gs_buffer[FFT_SIZE];
groupshared float2 gs_twiddle[FFT_SIZE / 2];

float2 ComplexMul(float2 a, float2 b)
{
	return float2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

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

[numthreads(FFT_SIZE / 2, 1, 1)] void CS_FFT(uint3 groupId : SV_GroupID, uint threadIdx : SV_GroupThreadID) {
	const uint N = FFT_SIZE;
	uint bits = firstbithigh(N);

	[unroll] for (uint i = 0; i < 2; ++i)
	{
		uint index = threadIdx + i * (FFT_SIZE / 2);
#ifdef ROW_PASS
		uint2 pos = uint2(index, groupId.x);
#else
		uint2 pos = uint2(groupId.x, index);
#endif
		uint rev = reversebits(index) >> (32 - bits);
		gs_buffer[rev] = TexInput[pos];
	}
	gs_twiddle[threadIdx] = Twiddle(threadIdx, N);
	GroupMemoryBarrierWithGroupSync();

	[unroll] for (uint stage = 1; stage < N; stage <<= 1)
	{
		uint butterflyIdx = threadIdx % stage;
		uint topIdx = (threadIdx / stage) * (stage << 1) + butterflyIdx;
		uint botIdx = topIdx + stage;
		float2 tw = gs_twiddle[butterflyIdx * (N / (stage << 1))];
		float2 top = gs_buffer[topIdx];
		float2 bot = ComplexMul(tw, gs_buffer[botIdx]);
		gs_buffer[topIdx] = top + bot;
		gs_buffer[botIdx] = top - bot;
		GroupMemoryBarrierWithGroupSync();
	}

	[unroll] for (uint i = 0; i < 2; ++i)
	{
		uint index = threadIdx + i * (FFT_SIZE / 2);
		float2 result = gs_buffer[index];
#ifdef INVERSE
		result /= float(N);
#endif
#ifdef ROW_PASS
		uint2 pos = uint2(index, groupId.x);
#else
		uint2 pos = uint2(groupId.x, index);
#endif
		RWTexOutput[pos] = result;
	}
}
