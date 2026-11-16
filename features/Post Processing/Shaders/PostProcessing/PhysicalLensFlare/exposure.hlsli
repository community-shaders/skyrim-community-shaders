#ifndef PHYSICAL_LENS_EXPOSURE
#define PHYSICAL_LENS_EXPOSURE
#include "PostProcessing/PhysicalLensFlare/optics.hlsli"
StructuredBuffer<float> ExposureAdaptation : register(t5);
cbuffer ExposureParameters : register(b1)
{
	float2 ExposureArea, ExposureRange;
	float ExposureLerp, ExposureCompensation, UnusedStart, UnusedMax;
	float UnusedStrength;
	float3 ExposurePadding;
};
float VisibilityExposure()
{
	float exposure = ManualExposure;
	if (AutoExposureEnabled > 0)
		exposure = ExposureCompensation / clamp(ExposureAdaptation[0], ExposureRange.x, ExposureRange.y);
	return exposure * ExposureDecode;
}

#endif
