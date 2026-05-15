/// By ProfJack/五脚猫, 2024-2-17 UTC
/// ref:
/// https://bruop.github.io/exposure/
/// https://knarkowicz.wordpress.com/2016/01/09/automatic-exposure/

#include "PostProcessing/HistogramAutoExposure/common.hlsli"

#include "Common/Color.hlsli"

RWStructuredBuffer<uint> RWBufferHistogram : register(u0);
RWStructuredBuffer<float> RWBufferAdaptation : register(u1);

Texture2D<float4> TexColor : register(t0);

const static float MinLogLum = -8;
const static float LogLumRange = 21;
const static float RcpLogLumRange = rcp(LogLumRange);
const static uint HistogramBins = 256;
const static uint FirstLuminanceBin = 1;
const static uint LastLuminanceBin = HistogramBins - 1;
const static uint SampleStride = 8;
const static uint HistogramWeightScale = 16;
const static uint SampleWeight = SampleStride * SampleStride * HistogramWeightScale;
const static float LowPercent = 0.10;
const static float HighPercent = 0.90;

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

[numthreads(32, 32, 1)] void CS_Histogram(uint2 tid : SV_DispatchThreadID, uint gidx : SV_GroupIndex) {
	uint2 dims;
	TexColor.GetDimensions(dims.x, dims.y);

	// Initialize shared memory - only need to do this once per group
	if (gidx < 256) {
		histogramShared[gidx] = 0;
	}
	GroupMemoryBarrierWithGroupSync();

	uint2 baseCoord = tid * SampleStride;
	bool validSample = !any(baseCoord >= dims);

	// Jitter inside each sampled cell to reduce structured aliasing while avoiding edge duplication.
	uint2 jitter = uint2(hash2D(tid) * SampleStride);
	uint2 pxCoord = min(baseCoord + jitter, dims - 1);

	// Precompute box bounds
	float4 boxBounds = ComputeBoxBounds(dims);

	// Optimized box check using precomputed bounds
	bool inBox = validSample &&
	             (pxCoord.x > boxBounds.x) && (pxCoord.x < boxBounds.z) &&
	             (pxCoord.y > boxBounds.y) && (pxCoord.y < boxBounds.w);

	if (inBox) {
		float3 color = TexColor[pxCoord].rgb;
		float luma = Color::RGBToLuminance(color);

		// Optimized bin calculation - avoid unnecessary saturate
		if (luma > 1e-10) {
			float histogramPos = saturate((log2(luma) - MinLogLum) * RcpLogLumRange);
			float fBin = FirstLuminanceBin + histogramPos * (LastLuminanceBin - FirstLuminanceBin);
			uint bin0 = min((uint)fBin, LastLuminanceBin);
			uint bin1 = min(bin0 + 1, LastLuminanceBin);
			float weight1 = frac(fBin);
			float weight0 = 1.0 - weight1;

			InterlockedAdd(histogramShared[bin0], (uint)(weight0 * SampleWeight + 0.5));
			InterlockedAdd(histogramShared[bin1], (uint)(weight1 * SampleWeight + 0.5));
		}
	}

	GroupMemoryBarrierWithGroupSync();

	// Save to texture - only need to do this once per group
	if (gidx < 256) {
		InterlockedAdd(RWBufferHistogram[gidx], histogramShared[gidx]);
	}
};

[numthreads(256, 1, 1)] void CS_Average(uint gidx : SV_GroupIndex) {
	if (gidx == 0) {
		float totalWeight = 0.0;
		[unroll] for (uint i = FirstLuminanceBin; i < HistogramBins; ++i)
		{
			totalWeight += (float)RWBufferHistogram[i];
		}

		float avgLum = max(1e-5, RWBufferAdaptation[0]);
		if (totalWeight > 0.0) {
			float lowCut = totalWeight * LowPercent;
			float highCut = totalWeight * HighPercent;
			float weightedLogLum = 0.0;
			float keptWeight = 0.0;

			[unroll] for (uint bin = FirstLuminanceBin; bin < HistogramBins; ++bin)
			{
				float binWeight = (float)RWBufferHistogram[bin];

				float lowDiscard = min(binWeight, lowCut);
				binWeight -= lowDiscard;
				lowCut -= lowDiscard;
				highCut = max(highCut - lowDiscard, 0.0);

				binWeight = min(binWeight, max(highCut, 0.0));
				highCut -= binWeight;

				float histogramPos = ((float)bin - FirstLuminanceBin) / (LastLuminanceBin - FirstLuminanceBin);
				float logLum = histogramPos * LogLumRange + MinLogLum;
				weightedLogLum += logLum * binWeight;
				keptWeight += binWeight;
			}

			if (keptWeight > 0.0)
				avgLum = exp2(weightedLogLum / keptWeight);
		}

		[unroll] for (uint clearBin = 0; clearBin < HistogramBins; ++clearBin)
		{
			RWBufferHistogram[clearBin] = 0;
		}

		float adaptedLum = lerp(max(1e-5, RWBufferAdaptation[0]), avgLum, AdaptLerp);
		RWBufferAdaptation[0] = adaptedLum;
	}
}
