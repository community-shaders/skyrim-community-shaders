/// By ProfJack/五脚猫, 2024-2-17 UTC
/// ref:
/// https://bruop.github.io/exposure/
/// https://knarkowicz.wordpress.com/2016/01/09/automatic-exposure/

#include "PostProcessing/HistogramAutoExposure/common.hlsli"

#include "Common/Color.hlsli"

RWStructuredBuffer<uint> RWBufferHistogram : register(u0);
RWStructuredBuffer<float> RWBufferAdaptation : register(u1);

Texture2D<float4> TexColor : register(t0);

const static float MinLogLum = -8;    // -5 EV
const static float LogLumRange = 21;  // -5 to 16 EV
const static float RcpLogLumRange = rcp(LogLumRange);

// Increased thread count per group for better occupancy
groupshared uint histogramShared[256];

// Optimized hash function using fewer operations
float2 hash2D(float2 p)
{
	float3 p3 = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
	p3 += dot(p3, p3.yzx + 33.33);
	return frac((p3.xx + p3.yz) * p3.zy);
}

// Precompute box bounds to avoid per-pixel calculations
float4 ComputeBoxBounds(float2 dims)
{
	float4 box = float4(.5 - AdaptArea * .5, .5 + AdaptArea * .5);
	return float4(
		dims.x * box.r,
		dims.y * box.g,
		dims.x * box.b,
		dims.y * box.a);
}

[numthreads(32, 32, 1)] void CS_Histogram(uint2 tid
										  : SV_DispatchThreadID, uint gidx
										  : SV_GroupIndex) {
	uint2 dims;
	TexColor.GetDimensions(dims.x, dims.y);

	// Initialize shared memory - only need to do this once per group
	if (gidx < 256) {
		histogramShared[gidx] = 0;
	}
	GroupMemoryBarrierWithGroupSync();

	// Sample spacing - take fewer samples (1 out of N pixels)
	const uint SAMPLE_SPACING = 8;  // Sample every 8th pixel for 64x reduction in samples

	// Compute base pixel coordinate with spacing
	uint2 pxCoord = tid * (2 * SAMPLE_SPACING);

	// Calculate jitter offset using blue noise distribution
	float2 jitter = hash2D(tid) * (SAMPLE_SPACING - 0.5) * 2.0;
	int2 jitterOffset = int2(jitter);

	// Optimized bounds checking
	pxCoord = uint2(max(0, min(int2(pxCoord) + jitterOffset, int2(dims) - 1)));

	// Precompute box bounds
	float4 boxBounds = ComputeBoxBounds(dims);

	// Optimized box check using precomputed bounds
	bool inBox = (pxCoord.x > boxBounds.x) && (pxCoord.x < boxBounds.z) &&
	             (pxCoord.y > boxBounds.y) && (pxCoord.y < boxBounds.w);

	if (inBox) {
		float3 color = TexColor[pxCoord].rgb;
		float luma = Color::RGBToLuminance(color);

		// Optimized bin calculation - avoid unnecessary saturate
		if (luma > 1e-10) {
			float logLuma = (log2(luma) - MinLogLum) * RcpLogLumRange;
			uint bin = uint(lerp(1, 255, min(1.0, max(0.0, logLuma))));
			InterlockedAdd(histogramShared[bin], SAMPLE_SPACING * SAMPLE_SPACING);
		}
	}

	GroupMemoryBarrierWithGroupSync();

	// Save to texture - only need to do this once per group
	if (gidx < 256) {
		InterlockedAdd(RWBufferHistogram[gidx], histogramShared[gidx]);
	}
};

[numthreads(256, 1, 1)] void CS_Average(uint gidx
										: SV_GroupIndex) {
	uint2 dims;
	TexColor.GetDimensions(dims.x, dims.y);
	uint numPixels = dims.x * dims.y * AdaptArea.x * AdaptArea.y * 0.25;

	// Optimized initialization
	if (gidx < 256) {
		uint pixelsInBin = RWBufferHistogram[gidx];
		histogramShared[gidx] = pixelsInBin * gidx;
		RWBufferHistogram[gidx] = 0;  // for next frame
	}
	GroupMemoryBarrierWithGroupSync();

	// Optimized reduction using bit shifts
	[unroll] for (uint offset = 128; offset > 0; offset >>= 1)
	{
		if (gidx < offset)
			histogramShared[gidx] += histogramShared[gidx + offset];
		GroupMemoryBarrierWithGroupSync();
	}

	// Optimized average calculation
	if (gidx == 0) {
		float logAvgLum = float(histogramShared[0]) / max(numPixels, 1.0) - 1.0;
		float avgLum = exp2(((logAvgLum / 254.0) * LogLumRange) + MinLogLum);
		float adaptedLum = lerp(max(1e-5, RWBufferAdaptation[0]), avgLum, AdaptLerp);
		RWBufferAdaptation[0] = adaptedLum;
	}
}