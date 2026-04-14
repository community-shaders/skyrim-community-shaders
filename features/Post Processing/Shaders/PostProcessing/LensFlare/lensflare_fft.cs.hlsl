// Lens Flare — FFT Ghost Convolution Pipeline
// Community Shaders / Post Processing
//
// Self-contained FFT pipeline for convolving lens flare ghosts with bokeh shapes.
// Uses LensFlareConstants CB layout (shared with lensflare.cs.hlsl).
//
// Shaders:
//   CS_FFT           — Stockham radix-2 FFT (row/col, forward/inverse via defines)
//   CS_Multiply      — Frequency-domain complex multiply (scene × bokeh kernel)
//   CSBokehPrepare   — Sample bokeh texture → zero-padded N×N RG32F with FFT-shift
//   CSFFTThreshold   — Convert half-res threshold to N×N RG32F for FFT input
//   CSFFTGhostCompose — IFFT result → multi-scale ghost sampling + halo + tint

#include "PostProcessing/common.hlsli"

static const float PI = 3.14159265358979323846;
static const float EPSILON = 1e-6;
static const int NUM_GHOSTS = 8;

// ============================================================
// Resources
// ============================================================

Texture2D<float4> InputTexture : register(t0);  // Context-dependent (bokeh texture, threshold, FFT result)
Texture2D<float4> FlareTexture : register(t1);  // Secondary input (threshold texture for compose)
SamplerState BokehSampler : register(s0);
SamplerState BorderSampler : register(s1);

// Complex textures (RG32F: R=real, G=imaginary)
Texture2D<float2> TexComplexA : register(t0);  // FFT input A (for multiply: scene FFT)
Texture2D<float2> TexComplexB : register(t1);  // FFT input B (for multiply: bokeh FFT)

RWTexture2D<float2> RWTexComplex : register(u0);  // Complex output (RG32F)
RWTexture2D<float4> RWTexColor : register(u0);    // Color output (RGBA16F, for compose)

// ============================================================
// Constant Buffer — matches LensFlareConstants in LensFlare.h
// ============================================================

cbuffer LensFlareConstants : register(b1)
{
	float OutputWidth;
	float OutputHeight;
	float InputWidth;
	float InputHeight;

	float ThresholdLevel;
	float ThresholdRange;
	float GhostStrength;
	float GhostChromaShift;

	float HaloStrength;
	float HaloRadius;
	float HaloWidth;
	float HaloCompression;

	float HaloChromaShift;
	float Intensity;
	float GlareIntensity;
	float GlareDivider;

	float2 GlareDirection;
	uint FFTResolution;
	float _pad0;

	float3 GlareScale_packed;
	int GLocalMask;

	float3 Tint;
	float _pad1;

	float4 GhostColors[NUM_GHOSTS];
	float4 GhostScalesPacked[2];
}

float GetGhostScale(int i)
{
	return GhostScalesPacked[i / 4][i % 4];
}

// ============================================================
// CS_FFT — Stockham radix-2 FFT (row/column pass)
// Compiled with defines: ROW_PASS/COL_PASS + FORWARD/INVERSE
// Dispatch: (N, 1, 1) where N = FFTResolution
// ============================================================

#define MAX_FFT_SIZE 1024

groupshared float2 gs_buffer0[MAX_FFT_SIZE];
groupshared float2 gs_buffer1[MAX_FFT_SIZE];

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

[numthreads(1024, 1, 1)] void CS_FFT(uint3 groupId : SV_GroupID, uint threadIdx : SV_GroupThreadID) {
	uint lineIdx = groupId.x;
	uint N = FFTResolution;
	bool active = (threadIdx < N);

	// Load
	if (active) {
		uint2 readPos;
#ifdef ROW_PASS
		readPos = uint2(threadIdx, lineIdx);
#else
		readPos = uint2(lineIdx, threadIdx);
#endif
		gs_buffer0[threadIdx] = TexComplexA[readPos];
	}
	GroupMemoryBarrierWithGroupSync();

	// Bit-reversal permutation
	if (active) {
		uint bits = firstbithigh(N) - firstbithigh(1);
		uint rev = 0;
		uint tmp = threadIdx;
		for (uint b = 0; b < bits; b++) {
			rev = (rev << 1) | (tmp & 1);
			tmp >>= 1;
		}
		gs_buffer1[rev] = gs_buffer0[threadIdx];
	}
	GroupMemoryBarrierWithGroupSync();

	if (active)
		gs_buffer0[threadIdx] = gs_buffer1[threadIdx];
	GroupMemoryBarrierWithGroupSync();

	// Cooley-Tukey butterfly
	for (uint stage = 1; stage < N; stage <<= 1) {
		if (active) {
			uint halfStage = stage;
			uint blockIdx = threadIdx / (halfStage * 2);
			uint blockOffset = threadIdx % (halfStage * 2);

			if (blockOffset < halfStage) {
				uint topIdx = blockIdx * halfStage * 2 + blockOffset;
				uint botIdx = topIdx + halfStage;
				float2 tw = Twiddle(blockOffset * (N / (halfStage * 2)), N);
				float2 top = gs_buffer0[topIdx];
				float2 bot = ComplexMul(tw, gs_buffer0[botIdx]);
				gs_buffer0[topIdx] = top + bot;
				gs_buffer0[botIdx] = top - bot;
			}
		}
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
		RWTexComplex[writePos] = result;
	}
}

	// ============================================================
	// CS_Multiply — Frequency-domain complex multiplication
	// scene_FFT × bokeh_FFT, normalized by DC component
	// Dispatch: ((N+7)/8, (N+7)/8, 1)
	// ============================================================

	[numthreads(8, 8, 1)] void CS_Multiply(uint2 tid : SV_DispatchThreadID)
{
	if (tid.x >= FFTResolution || tid.y >= FFTResolution)
		return;

	float2 scene = TexComplexA[tid];
	float2 psf = TexComplexB[tid];

	// Normalize by DC component to preserve brightness
	float psfDC = max(TexComplexB[uint2(0, 0)].x, 1e-6);

	float2 result;
	result.x = scene.x * psf.x - scene.y * psf.y;
	result.y = scene.x * psf.y + scene.y * psf.x;
	result /= psfDC;

	RWTexComplex[tid] = result;
}

// ============================================================
// CSBokehPrepare — Sample bokeh shape texture into N×N RG32F
// Normalizes, zero-pads, and FFT-shifts (center-to-corner)
// t0 = bokeh shape texture (RGBA color), s0 = linear sampler
// u0 = RG32F output (real = luminance, imag = 0)
// Dispatch: ((N+7)/8, (N+7)/8, 1)
// ============================================================

[numthreads(8, 8, 1)] void CSBokehPrepare(uint2 tid : SV_DispatchThreadID) {
	uint N = FFTResolution;
	if (tid.x >= N || tid.y >= N)
		return;

	// Bokeh occupies the center kernelSize × kernelSize region
	// Typical bokeh kernel is ~1/4 of FFT resolution for good padding
	uint kernelSize = N / 4;
	uint halfKernel = kernelSize / 2;
	uint centerX = N / 2;
	uint centerY = N / 2;

	float2 result = float2(0, 0);

	// Check if this pixel falls within the kernel region (centered)
	int dx = (int)tid.x - (int)centerX;
	int dy = (int)tid.y - (int)centerY;

	if (abs(dx) <= (int)halfKernel && abs(dy) <= (int)halfKernel) {
		// Map kernel pixel to [0,1] UV of bokeh texture
		float2 uv = float2(
			(float)(dx + (int)halfKernel) / (float)kernelSize,
			(float)(dy + (int)halfKernel) / (float)kernelSize);

		float3 bokehColor = InputTexture.SampleLevel(BokehSampler, uv, 0).rgb;
		float luminance = dot(bokehColor, float3(0.2126, 0.7152, 0.0722));
		result.x = luminance;
	}

	// FFT-shift: swap quadrants so DC is at corner (0,0)
	// This is required for correct convolution via FFT
	uint sx = (tid.x + N / 2) % N;
	uint sy = (tid.y + N / 2) % N;

	RWTexComplex[uint2(sx, sy)] = result;
}

	// ============================================================
	// CSFFTThreshold — Convert half-res threshold → N×N RG32F
	// Centers the threshold image in the FFT buffer with zero padding
	// t0 = threshold texture (RGBA float), s0 = sampler
	// u0 = RG32F output (real = luminance, imag = 0)
	// Dispatch: ((N+7)/8, (N+7)/8, 1)
	// ============================================================

	[numthreads(8, 8, 1)] void CSFFTThreshold(uint2 tid : SV_DispatchThreadID)
{
	uint N = FFTResolution;
	if (tid.x >= N || tid.y >= N)
		return;

	// Map FFT pixel to threshold texture UV
	// The threshold image is mapped to fill the center of the FFT buffer
	float2 uv = (float2(tid.xy) + 0.5) / float(N);

	// Sample threshold (using InputWidth/InputHeight as the source resolution)
	float3 color = InputTexture.SampleLevel(BokehSampler, uv, 0).rgb;
	float luminance = dot(color, float3(0.333, 0.333, 0.333));

	RWTexComplex[tid] = float2(luminance, 0);
}

// ============================================================
// CSFFTGhostCompose — Compose FFT convolution result into ghost+halo
// Takes IFFT result (RG32F luminance) and applies multi-scale ghost
// sampling with chromatic aberration and tinting, plus halo.
// t0 = FFT convolution result (RG32F: .x = spatial luminance)
// t1 = threshold texture (for halo computation)
// u0 = output ghost+halo texture (RGBA16F, half-res)
// Dispatch: ((halfW+7)/8, (halfH+7)/8, 1)
// ============================================================

// Fisheye UV distortion (same as main shader)
float2 FisheyeUV(float2 uv, float compression, float zoom)
{
	float2 negPosUV = 2.0f * uv - 1.0f;
	float scale = compression * atan(rcp(compression));
	float radiusDist = length(negPosUV) * scale;
	float radiusDir = compression * tan(radiusDist / compression) * zoom;
	float phi = atan2(negPosUV.y, negPosUV.x);
	float2 newUV = float2(radiusDir * cos(phi) + 1.0f, radiusDir * sin(phi) + 1.0f) * 0.5f;
	return newUV;
}

float DiscMask(float2 screenPos)
{
	return saturate(1.0f - dot(screenPos, screenPos));
}

[numthreads(8, 8, 1)] void CSFFTGhostCompose(uint2 tid : SV_DispatchThreadID) {
	uint outW = (uint)OutputWidth;
	uint outH = (uint)OutputHeight;
	if (tid.x >= outW || tid.y >= outH)
		return;

	float2 uv = (float2(tid.xy) + 0.5) / float2(OutputWidth, OutputHeight);
	float3 color = float3(0, 0, 0);

	// --- Ghost generation using FFT-convolved bokeh result ---
	[branch] if (GhostStrength > EPSILON)
	{
		float2 radiantVector = uv - 0.5;

		for (int i = 0; i < NUM_GHOSTS; i++) {
			float4 ghostColor = GhostColors[i];
			float ghostScale = GetGhostScale(i);

			if (abs(ghostColor.a * ghostScale) < 0.00001)
				continue;

			float2 ghostVector = radiantVector * ghostScale;

			// Local mask
			float distanceMask = 1.0 - length(ghostVector);
			float weight;
			if (GLocalMask) {
				float mask1 = smoothstep(0.5, 0.9, distanceMask);
				float mask2 = smoothstep(0.75, 1.0, distanceMask) * 0.95 + 0.05;
				weight = mask1 * mask2;
			} else {
				weight = distanceMask;
			}

			// Sample FFT result at ghost position (UV in FFT space)
			float2 ghostUV = ghostVector + 0.5;

			// Chromatic aberration: offset R and B channels
			float chromaOffset = GhostChromaShift * 8.0;
			float2 dir = normalize(ghostVector + 0.0001);
			float2 uvR = ghostUV + dir * chromaOffset / float2(InputWidth, InputHeight);
			float2 uvG = ghostUV;
			float2 uvB = ghostUV - dir * chromaOffset / float2(InputWidth, InputHeight);

			// Sample FFT convolution result (luminance in .x, imag in .y)
			// Map UV from half-res space to FFT texture space
			float sR = InputTexture.SampleLevel(BokehSampler, uvR, 0).x;
			float sG = InputTexture.SampleLevel(BokehSampler, uvG, 0).x;
			float sB = InputTexture.SampleLevel(BokehSampler, uvB, 0).x;

			// FFT result is grayscale luminance — reconstruct color using ghost tint
			color += float3(sR, sG, sB) * ghostColor.rgb * ghostColor.a * weight;
		}

		// Screen border mask
		float2 screenPos = uv * 2.0 - 1.0;
		float screenBorderMask = DiscMask(screenPos * 0.9);
		color *= screenBorderMask * GhostStrength;
	}

	// --- Halo (same as fast path, uses threshold texture in t1) ---
	if (HaloStrength > EPSILON) {
		float2 fishUV = FisheyeUV(uv, HaloCompression, 1.0);
		float2 haloVector = normalize(0.5 - uv) * HaloWidth;
		float haloMask = distance(uv, 0.5);
		haloMask = saturate(haloMask * 2.0);
		haloMask = smoothstep(HaloRadius, 1.0, haloMask);

		float2 screenPos = uv * 2.0 - 1.0;
		float screenBorderMask = DiscMask(screenPos) * DiscMask(screenPos * 0.8);
		screenBorderMask = screenBorderMask * 0.95 + 0.05;

		float2 uvR = (fishUV - 0.5) * (1.0 + HaloChromaShift) + 0.5 + haloVector;
		float2 uvG = fishUV + haloVector;
		float2 uvB = (fishUV - 0.5) * (1.0 - HaloChromaShift) + 0.5 + haloVector;

		float3 haloColor;
		haloColor.r = FlareTexture.SampleLevel(BorderSampler, uvR, 0).r;
		haloColor.g = FlareTexture.SampleLevel(BorderSampler, uvG, 0).g;
		haloColor.b = FlareTexture.SampleLevel(BorderSampler, uvB, 0).b;

		color += haloColor * screenBorderMask * haloMask * HaloStrength;
	}

	RWTexColor[tid] = float4(color, 1.0);
}
