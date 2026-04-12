// SSRT3 - Screen Space Ray Tracing 3
// Ported from SSRT3 by CDRIN (Olivier Therrien)
// https://github.com/cdrinmatern/SSRT3
//
// Based on GTAO/GTSO "Jimenez et al. / Practical Real-Time Strategies for Accurate Indirect Occlusion"
// https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf

#ifndef SSRT_COMMON
#define SSRT_COMMON

///////////////////////////////////////////////////////////////////////////////

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
	float BackfaceLighting;
	float AOIntensity;

	float Thickness;
	uint LinearThickness;
	uint FallbackSampleCount;
	float FallbackIntensity;

	float FallbackPower;
	float TemporalOffsets;
	float TemporalDirections;
	float pad0;
};

SamplerState samplerPointClamp : register(s0);
SamplerState samplerLinearClamp : register(s1);

///////////////////////////////////////////////////////////////////////////////

// 32-bit bitmask for horizon tracking
#define MAX_RAY 32

#define ISNAN(x) (!(x < 0.f || x > 0.f || x == 0.f))
float filterNaN(float v)
{
	return ISNAN(v) ? 0 : v;
}
float2 filterNaN(float2 v) { return float2(filterNaN(v.x), filterNaN(v.y)); }
float3 filterNaN(float3 v) { return float3(filterNaN(v.x), filterNaN(v.y), filterNaN(v.z)); }
float4 filterNaN(float4 v) { return float4(filterNaN(v.x), filterNaN(v.y), filterNaN(v.z), filterNaN(v.w)); }

float filterInf(float v) { return isinf(v) ? 0 : v; }
float2 filterInf(float2 v) { return float2(filterInf(v.x), filterInf(v.y)); }
float3 filterInf(float3 v) { return float3(filterInf(v.x), filterInf(v.y), filterInf(v.z)); }
float4 filterInf(float4 v) { return float4(filterInf(v.x), filterInf(v.y), filterInf(v.z), filterInf(v.w)); }

///////////////////////////////////////////////////////////////////////////////

// Inputs are screen XY and viewspace depth, output is viewspace position
float3 ScreenToViewPosition(const float2 screenPos, const float viewspaceDepth, const uint eyeIndex)
{
	const float2 _mul = eyeIndex == 0 ? NDCToViewMul.xy : NDCToViewMul.zw;
	const float2 _add = eyeIndex == 0 ? NDCToViewAdd.xy : NDCToViewAdd.zw;

	float3 ret;
	ret.xy = (_mul * screenPos.xy + _add) * viewspaceDepth;
	ret.z = viewspaceDepth;
	return ret;
}

float ScreenToViewDepth(const float screenDepth)
{
	return (SharedData::CameraData.w / (-screenDepth * SharedData::CameraData.z + SharedData::CameraData.x));
}

float3 ViewToWorldPosition(const float3 pos, const float4x4 invView)
{
	float4 worldpos = mul(invView, float4(pos, 1));
	return worldpos.xyz / worldpos.w;
}

float3 ViewToWorldVector(const float3 vec, const float4x4 invView)
{
	return mul((float3x3)invView, vec);
}

///////////////////////////////////////////////////////////////////////////////

// From Activision GTAO paper: https://www.activision.com/cdn/research/s2016_pbs_activision_occlusion.pptx
float SpatialOffsets(int2 position)
{
	return 0.25 * (float)((position.y - position.x) & 3);
}

// Interleaved gradient noise from Jimenez 2014 http://goo.gl/eomGso
float GradientNoise(float2 position)
{
	return frac(52.9829189 * frac(dot(position, float2(0.06711056, 0.00583715))));
}

// From http://byteblacksmith.com/improvements-to-the-canonical-one-liner-glsl-rand-for-opengl-es-2-0/
float ssrtRand(float2 co)
{
	float dt = dot(co.xy, float2(12.9898, 78.233));
	float sn = fmod(dt, 3.14);
	return frac(sin(sn) * 43758.5453);
}

// Fast acos approximation from GTAO
float2 GTAOFastAcos(float2 x)
{
	float2 outVal = -0.156583 * abs(x) + Math::HALF_PI;
	outVal *= sqrt(1.0 - abs(x));
	return x >= 0 ? outVal : Math::PI - outVal;
}

float GTAOFastAcos(float x)
{
	float outVal = -0.156583 * abs(x) + Math::HALF_PI;
	outVal *= sqrt(1.0 - abs(x));
	return x >= 0 ? outVal : Math::PI - outVal;
}

// RGB to HSV conversion (matching SSRT3's RgbToHsv)
float3 RgbToHsv(float3 c)
{
	float4 K = float4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
	float4 p = lerp(float4(c.bg, K.wz), float4(c.gb, K.xy), step(c.b, c.g));
	float4 q = lerp(float4(p.xyw, c.r), float4(c.r, p.yzx), step(p.x, c.r));
	float d = q.x - min(q.w, q.y);
	float e = 1.0e-10;
	return float3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

// HSV to RGB conversion (matching SSRT3's HsvToRgb)
float3 HsvToRgb(float3 c)
{
	float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
	float3 p = abs(frac(c.xxx + K.xyz) * 6.0 - K.www);
	return c.z * lerp(K.xxx, saturate(p - K.xxx), c.y);
}

#endif
