/// Local Exposure Compute Shader
/// Produces a per-pixel exposure multiplier by comparing pixel luminance to a local neighborhood average.
/// Algorithm:
///   1. CSLuminance: Downsample scene to 1/4 res log-luminance
///   2. CSDownsample: Iterative mip chain downsample (Gaussian approximation)
///   3. CSComputeExposure: Compare pre-exposed local base luminance to middle grey,
///      compute exposure multiplier with bilateral-aware upsampling
///
/// Based on Bart Wronski's local tonemapping / exposure fusion technique:
///   https://bartwronski.com/2022/02/28/exposure-fusion-local-tonemapping-for-real-time-rendering/

#include "Common/Color.hlsli"

cbuffer LocalExposureCB : register(b1)
{
	float HighlightContrast;
	float ShadowContrast;
	float DetailStrength;
	float BilateralSigma;
	uint2 InputDims;
	uint2 LowResDims;
	uint MipLevel;
	float2 AdaptationRange;
	float ExposureCompensation;
	float MiddleGrey;
	uint UseGlobalExposure;
	float pad[2];
};

// Shared resource declarations (register assignments are per-entry-point in practice)
// t0: primary input texture (scene color for Pass 1 & 3, luminance mip for Pass 2)
// t1: luminance mip chain (Pass 3 only)
// u0: output (luminance for Pass 1 & 2, exposure for Pass 3)
// s0: linear sampler (Pass 3 only)

Texture2D<float4> TexInput0 : register(t0);  // float4 for scene, .r for luminance
Texture2D<float> TexInput1 : register(t1);   // luminance mip chain (Pass 3)
StructuredBuffer<float> TexAdaptation : register(t2);
SamplerState LinearSampler : register(s0);
RWTexture2D<float> RWTexOutput : register(u0);

// ============================================================
// Pass 1: Compute log-luminance at 1/4 resolution
// Input t0: Scene color (float4), Output u0: log-luminance (float)
// ============================================================
[numthreads(8, 8, 1)] void CSLuminance(uint2 tid : SV_DispatchThreadID) {
	if (any(tid >= LowResDims))
		return;

	// Sample the center of the 4x4 block corresponding to this low-res pixel
	uint2 srcCoord = tid * 4 + 2;
	srcCoord = min(srcCoord, InputDims - 1);

	// Average a 2x2 region for better stability
	float luminance = 0;
	[unroll] for (int dy = 0; dy <= 1; dy++)
	{
		[unroll] for (int dx = 0; dx <= 1; dx++)
		{
			uint2 coord = min(srcCoord + uint2(dx, dy), InputDims - 1);
			float3 color = TexInput0[coord].rgb;
			luminance += Color::RGBToLuminance(color);
		}
	}
	luminance *= 0.25;

	// Store as log-luminance (clamped to avoid log(0))
	RWTexOutput[tid] = log2(max(luminance, 1e-6));
}

	// ============================================================
	// Pass 2: Iterative mip downsample (box filter / 2x2 average)
	// Input t0: Previous mip (.r channel), Output u0: Next mip
	// ============================================================
	[numthreads(8, 8, 1)] void CSDownsample(uint2 tid : SV_DispatchThreadID)
{
	uint2 outDims;
	RWTexOutput.GetDimensions(outDims.x, outDims.y);

	if (any(tid >= outDims))
		return;

	// 2x2 box filter from the source mip (read .r from float4 texture)
	uint2 srcBase = tid * 2;
	float sum = 0;
	sum += TexInput0[srcBase + uint2(0, 0)].r;
	sum += TexInput0[srcBase + uint2(1, 0)].r;
	sum += TexInput0[srcBase + uint2(0, 1)].r;
	sum += TexInput0[srcBase + uint2(1, 1)].r;

	RWTexOutput[tid] = sum * 0.25;
}

// ============================================================
// Pass 3: Compute per-pixel local exposure multiplier
// Uses guided bilateral upsampling to avoid halos
// Input t0: Full-res scene color, t1: Luminance mip chain, t2: adapted average luminance
// Output u0: Exposure map (full res)
// ============================================================
[numthreads(8, 8, 1)] void CSComputeExposure(uint2 tid : SV_DispatchThreadID) {
	if (any(tid >= InputDims))
		return;

	// Get pixel luminance (in log space)
	float3 sceneColor = TexInput0[tid].rgb;
	float pixelLum = Color::RGBToLuminance(sceneColor);
	float logPixelLum = log2(max(pixelLum, 1e-6));

	// Sample local average luminance from the mip chain
	// Use the UV coordinate and bilinear sampling for smooth results
	float2 uv = (float2(tid) + 0.5) / float2(InputDims);

	// Bilateral-aware sampling: sample from the mip chain using a 3x3 kernel
	// weighted by luminance similarity to the current pixel
	float2 mipDims = float2(LowResDims) / float(1u << MipLevel);
	float2 mipPixelSize = 1.0 / max(mipDims, 1.0);

	float localAvgLog = 0;
	float totalWeight = 0;

	// 3x3 bilateral kernel on the mip level
	float rcpSigmaSq = 1.0 / (BilateralSigma * BilateralSigma + 1e-6);

	[unroll] for (int dy = -1; dy <= 1; dy++)
	{
		[unroll] for (int dx = -1; dx <= 1; dx++)
		{
			float2 sampleUV = uv + float2(dx, dy) * mipPixelSize;
			sampleUV = clamp(sampleUV, mipPixelSize * 0.5, 1.0 - mipPixelSize * 0.5);

			float sampleLogLum = TexInput1.SampleLevel(LinearSampler, sampleUV, MipLevel);

			// Bilateral weight: spatial * range
			float spatialW = exp(-0.5 * float(dx * dx + dy * dy) / (1.0 * 1.0));
			float rangeDiff = sampleLogLum - logPixelLum;
			float rangeW = exp(-0.5 * rangeDiff * rangeDiff * rcpSigmaSq);

			float w = spatialW * rangeW;
			localAvgLog += sampleLogLum * w;
			totalWeight += w;
		}
	}

	localAvgLog /= (totalWeight + 1e-6);

	// Match Unreal's pre-exposed local exposure shape: judge the local base
	// luminance after global exposure, then compress/expand it around middle grey.
	float adaptedLum = UseGlobalExposure != 0 ? TexAdaptation[0] : 1.0;
	adaptedLum = clamp(max(adaptedLum, 1e-5), AdaptationRange.x, AdaptationRange.y);
	float globalExposure = UseGlobalExposure != 0 ? (0.18 * ExposureCompensation / adaptedLum) : 1.0;
	float logGlobalExposure = log2(max(globalExposure, 1e-6));
	float logMiddleGrey = log2(max(MiddleGrey, 1e-6));

	float preExposedLogPixel = logPixelLum + logGlobalExposure;
	float preExposedBaseLog = localAvgLog + logGlobalExposure;
	float detailLogLum = preExposedLogPixel - preExposedBaseLog;
	float baseCentered = preExposedBaseLog - logMiddleGrey;
	float contrastScale = baseCentered > 0 ? HighlightContrast : ShadowContrast;

	float targetLogLum = logMiddleGrey + baseCentered * contrastScale + detailLogLum * DetailStrength;
	float exposureAdjust = targetLogLum - preExposedLogPixel;

	// Convert from log2 EV adjustment to linear multiplier
	float localExposure = exp2(exposureAdjust);

	// Clamp to reasonable range to avoid extreme adjustments
	localExposure = clamp(localExposure, 0.25, 4.0);

	// Smooth falloff for very dark pixels to avoid boosting noise
	float darkFalloff = saturate((pixelLum * globalExposure) / 0.01);
	localExposure = lerp(1.0, localExposure, darkFalloff * darkFalloff);

	RWTexOutput[tid] = localExposure;
}
