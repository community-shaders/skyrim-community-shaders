// Separable depth-aware blur of the half-res volumetric ray scattering.
// Compile with HORIZONTAL for the horizontal pass; the vertical pass is the default.
// Taps are rejected by relative linear-depth difference, so the kernel behaves the
// same near and far instead of depending on raw depth-buffer precision.

Texture2D<float> InputTexture : register(t0);
Texture2D<float> LinearDepthTexture : register(t1);
RWTexture2D<float> OutputTexture : register(u0);

// Half-res target dimensions (dynamic resolution area).
cbuffer VLData : register(b1)
{
	int2 ScreenSize;
	int2 ScreenSizeMin1;
}

#define TG_DIM 256
#define WINDOW 12

groupshared float scattering[TG_DIM];
groupshared float linearDepth[TG_DIM];

static const int TapOffsets[5] = { -12, -6, 0, 6, 12 };
static const float TapWeights[5] = { 0.178400, 0.210431, 0.222338, 0.210431, 0.178400 };

#if defined(HORIZONTAL)
[numthreads(TG_DIM, 1, 1)] void main(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID) {
	int idx = groupThreadId.x;
	int base = idx - WINDOW;
	int2 pixel = int2(groupId.x * (TG_DIM - WINDOW * 2) + base, groupId.y);
#else
[numthreads(1, TG_DIM, 1)] void main(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID) {
	int idx = groupThreadId.y;
	int base = idx - WINDOW;
	int2 pixel = int2(groupId.x, groupId.y * (TG_DIM - WINDOW * 2) + base);
#endif

	// Clamp so the halo lanes replicate the edge instead of reading outside the render area.
	int2 clampedPixel = clamp(pixel, 0, ScreenSizeMin1);
	scattering[idx] = InputTexture[clampedPixel];
	linearDepth[idx] = LinearDepthTexture[clampedPixel];

	GroupMemoryBarrierWithGroupSync();

	if (base < 0 || base >= TG_DIM - WINDOW * 2 || any(pixel > ScreenSizeMin1))
		return;

	float centerDepth = linearDepth[idx];
	float rcpCenterDepth = rcp(max(centerDepth, 1e-4));

	// The center tap always has full weight, so weightSum is never zero.
	float weightedSum = 0.0;
	float weightSum = 0.0;
	[unroll] for (uint i = 0; i < 5; i++)
	{
		int tap = idx + TapOffsets[i];
		float relativeDelta = abs(linearDepth[tap] - centerDepth) * rcpCenterDepth;
		float weight = TapWeights[i] * (1.0 - smoothstep(0.1, 0.3, relativeDelta));
		weightedSum += weight * scattering[tap];
		weightSum += weight;
	}

	OutputTexture[pixel] = weightedSum / weightSum;
}
