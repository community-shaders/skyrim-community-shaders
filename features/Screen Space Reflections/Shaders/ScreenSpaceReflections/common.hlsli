#ifndef SSR_COMMON
#define SSR_COMMON

#include "Common/Math.hlsli"
#include "Common/SharedData.hlsli"

cbuffer SSRCB : register(b1)
{
	float2 TexDim;
	float2 RcpTexDim;
	float2 FrameDim;
	float2 RcpFrameDim;
	uint FrameIndex;

	uint SpecMaxSteps;
	uint SpecMaxMips;
	float SpecThickness;

	float NormalBias;
	float BRDFBias;
	float OcclusionStrength;
	uint SpecUseDynamicCubemap;

	float SpecCubemapMult;
	float HitDistA;
	float HitDistB;
	float HitDistC;
};

SamplerState samplerPointClamp : register(s0);
SamplerState samplerLinearClamp : register(s1);

#endif
