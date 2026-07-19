#ifndef __WBOIT_WEIGHT__
#define __WBOIT_WEIGHT__

float WBOITComputeWeight(float alpha, float depth)
{
	float d = max(SharedData::orderIndependentTransparencySettings.WBOITMinProjectedDistance, 1.0 - depth);
	float w = max(SharedData::orderIndependentTransparencySettings.WBOITMinPreAlphaWeight, 3000.0 * d * d * d);
	return clamp(alpha * w, SharedData::orderIndependentTransparencySettings.WBOITWeightMin, SharedData::orderIndependentTransparencySettings.WBOITWeightMax);
}

#endif
