#ifndef __WBOIT_WEIGHT__
#define __WBOIT_WEIGHT__

float WBOITComputeWeight(float alpha, float depth)
{
	float d = max(0.2, 1.0 - depth);
	float w = max(0.01, 3000.0 * d * d * d);
	return clamp(alpha * w, 0.1, 1.0);
}

#endif
