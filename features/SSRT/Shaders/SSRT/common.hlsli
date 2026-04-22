#ifndef SSRT_COMMON
#define SSRT_COMMON

#include "Common/Math.hlsli"
#include "Common/SharedData.hlsli"

cbuffer SSRTCB : register(b1)
{
	float4 NDCToViewMul;
	float4 NDCToViewAdd;

	float2 TexDim;
	float2 RcpTexDim;
	float2 FrameDim;
	float2 RcpFrameDim;

	uint FrameIndex;
	uint RotationCount;
	uint StepCount;
	float Radius;

	float ExpFactor;
	float HalfProjScale;
	uint JitterSamples;
	uint ScreenSpaceSampling;

	uint MipOptimization;
	float GIIntensity;
	float AOIntensity;
	float Thickness;

	uint LinearThickness;
	float TemporalOffsets;
	float TemporalDirections;
	float pad0;
};

SamplerState samplerPointClamp : register(s0);
SamplerState samplerLinearClamp : register(s1);

float ScreenToLinearDepth(float deviceDepth)
{
	return SharedData::GetScreenDepth(deviceDepth);
}

#endif
