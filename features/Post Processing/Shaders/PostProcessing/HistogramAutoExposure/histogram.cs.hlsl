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

groupshared uint histogramShared[256];

[numthreads(16, 16, 1)] void CS_Histogram(uint2 tid
										  : SV_DispatchThreadID, uint gidx
										  : SV_GroupIndex) {
	uint2 dims;
	TexColor.GetDimensions(dims.x, dims.y);

	// half res here
	uint2 pxCoord = tid * 2;

	// init
	histogramShared[gidx] = 0;

	GroupMemoryBarrierWithGroupSync();

	// local histo
	float4 box = float4(.5 - AdaptArea * .5, .5 + AdaptArea * .5);
	if ((pxCoord.x > dims.x * box.r) &&
		(pxCoord.x < dims.x * box.b) &&
		(pxCoord.y > dims.y * box.g) &&
		(pxCoord.y < dims.y * box.a)) {
		uint bin = 0;

		float3 color = TexColor[pxCoord].rgb;
		float luma = Color::RGBToLuminance(color);
		if (luma > 1e-10) {
			float logLuma = saturate((log2(luma) - MinLogLum) * RcpLogLumRange);
			bin = uint(lerp(1, 255, logLuma));
		}

		InterlockedAdd(histogramShared[bin], 1);
	}

	GroupMemoryBarrierWithGroupSync();

	// save to texture
	InterlockedAdd(RWBufferHistogram[gidx], histogramShared[gidx]);
};

[numthreads(256, 1, 1)] void CS_Average(uint gidx
										: SV_GroupIndex) {
	uint2 dims;
	TexColor.GetDimensions(dims.x, dims.y);
	uint numPixels = dims.x * dims.y * AdaptArea.x * AdaptArea.y * 0.25;

	// init
	uint pixelsInBin = RWBufferHistogram[gidx];
	histogramShared[gidx] = pixelsInBin * gidx;
	RWBufferHistogram[gidx] = 0;  // for next frame

	GroupMemoryBarrierWithGroupSync();

	// sum
	[unroll] for (uint cutoff = (256 >> 1); cutoff > 0; cutoff >>= 1)
	{
		if (gidx < cutoff)
			histogramShared[gidx] += histogramShared[gidx + cutoff];
		GroupMemoryBarrierWithGroupSync();
	}

	// average
	if (gidx == 0) {
		// pixelsInBin here is number of zero value pixels
		float logAvgLum = float(histogramShared[0]) / max(numPixels, 1.0) - 1.0;
		float avgLum = exp2(((logAvgLum / 254.0) * LogLumRange) + MinLogLum);
		float adaptedLum = lerp(max(1e-5, RWBufferAdaptation[0]), avgLum, AdaptLerp);
		RWBufferAdaptation[0] = adaptedLum;
	}
}