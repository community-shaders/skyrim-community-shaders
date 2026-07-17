#ifndef __WBOIT__
#define __WBOIT__

#include "OIT/OITCommon.hlsli"
#include "OIT/WBOITWeight.hlsli"
#include "Common/SharedData.hlsli"

Texture2D<float> OITWaterDepthTexture : register(t120);

struct WBOITResult
{
	float4 accumAll;
	float4 revealage;
	float4 accumFront;
};

float4 WBOITQuantizePrototypeColor(float4 color)
{
	return floor(saturate(color) * 255.0 + 0.5) / 255.0;
}

WBOITResult WBOITCapture(float4 color, int2 screenAddress, float depth, uint flags)
{
	WBOITResult result;
	result.accumAll = 0.0.xxxx;
	result.revealage = 1.0.xxxx;
	result.accumFront = 0.0.xxxx;

	uint blend = flags & OIT_FLAGS_BLEND_MODS;

#if !OIT_CAPTURE_IGNORE_ALPHA_THRESHOULD
	color.w = max(0.f, color.w - SharedData::orderIndependentTransparencySettings.AlphaThreshold) / (1.f - SharedData::orderIndependentTransparencySettings.AlphaThreshold);
#endif

	if (blend == OIT_FLAGS_MULTIPLICATIVE_A)
	{
		color = float4(0.0, 0.0, 0.0, (1.0 - color.w) * color.a);
	}
	else if (blend == OIT_FLAGS_ADDITIVE)
	{
		color = float4(color.xyz * color.w, 0.0);
	}
	else if (blend == OIT_FLAGS_MULTIPLICATIVE)
	{
		color = float4(0.0, 0.0, 0.0, 1.0 - color.a);
	}
	else
	{
		color = float4(color.xyz * color.w, color.w);
	}

	color = saturate(color);

	// For additive blend, we use its RGB color to emulate a transparency
	// We don't need to be accurate, just need a smooth curve to blend in the truely transparent part of the sprite
	float a = color.w == 0.f ? 0.01 * sqrt(sqrt(length(color.xyz / sqrt(3)))) : color.w;
	color.w = a;
	// Get the weight for this fragment, it should have larger weight on fragments closer to the camera
	float weight = WBOITComputeWeight(a, depth);

	float waterDepth = OITWaterDepthTexture[screenAddress];
	bool frontOfWater = depth <= waterDepth;
	float writeDepth = (flags & OIT_FLAGS_DEPTH_WRITE) ? depth : 1.0;

	result.accumAll = color * weight;
	result.accumFront = frontOfWater ? result.accumAll : 0.0.xxxx;
	result.revealage = float4(1.0 - a, frontOfWater ? 1.0 - a : 1.0, 0, writeDepth);
	return result;
}

#endif
