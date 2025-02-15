///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation
//
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// XeGTAO is based on GTAO/GTSO "Jimenez et al. / Practical Real-Time Strategies for Accurate Indirect Occlusion",
// https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf
//
// Implementation:  Filip Strugar (filip.strugar@intel.com), Steve Mccalla <stephen.mccalla@intel.com>         (\_/)
// Version:         (see XeGTAO.h)                                                                            (='.'=)
// Details:         https://github.com/GameTechDev/XeGTAO                                                     (")_(")
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// with additional edits by FiveLimbedCat/ProfJack

#ifndef SSGI_COMMON
#define SSGI_COMMON

///////////////////////////////////////////////////////////////////////////////

#include "Common/Math.hlsli"
#include "Common/SharedData.hlsli"

cbuffer SSGICB : register(b1)
{
	float4x4 PrevInvViewMat[2];
	float4 NDCToViewMul;
	float4 NDCToViewAdd;

	float2 TexDim;
	float2 RcpTexDim;
	float2 FrameDim;
	float2 RcpFrameDim;

	uint FrameIndex;

	uint NumSlices;
	uint NumSteps;

	float MinScreenRadius;
	float AORadius;
	float GIRadius;
	float EffectRadius;
	float Thickness;
	float2 DepthFadeRange;
	float DepthFadeScaleConst;

	float GISaturation;
	float GIBounceFade;
	float GIDistanceCompensation;
	float GICompensationMaxDist;

	float AOPower;
	float GIStrength;

	float DepthDisocclusion;
	float NormalDisocclusion;
	uint MaxAccumFrames;

	float BlurRadius;
	float DistanceNormalisation;
	float2 pad;
};

SamplerState samplerPointClamp : register(s0);
SamplerState samplerLinearClamp : register(s1);

///////////////////////////////////////////////////////////////////////////////

// first person z
#define FP_Z (18.0)

#define ISNAN(x) (!(x < 0.f || x > 0.f || x == 0.f))
float filterNaN(float v)
{
	return ISNAN(v) ? 0 : v;
}
float2 filterNaN(float2 v) { return float2(filterNaN(v.x), filterNaN(v.y)); }
float4 filterNaN(float4 v) { return float4(filterNaN(v.x), filterNaN(v.y), filterNaN(v.z), filterNaN(v.w)); }

// screenPos - normalised position in FrameDim, one eye only
// uv - normalised position in FrameDim, both eye
// texCoord - texture coordinate

#ifdef HALF_RES
#	define RES_MIP 1
#	define READ_DEPTH(tex, px) tex.Load(int3(px, RES_MIP))
#	define FULLRES_LOAD(tex, px, texCoord, samp) tex.SampleLevel(samp, texCoord, 0)
#	define OUT_FRAME_DIM (FrameDim * 0.5)
#	define RCP_OUT_FRAME_DIM (RcpFrameDim * 2)
#	define OUT_FRAME_SCALE (frameScale * 0.5)
#elif defined(QUARTER_RES)
#	define RES_MIP 2
#	define READ_DEPTH(tex, px) tex.Load(int3(px, RES_MIP))
#	define FULLRES_LOAD(tex, px, texCoord, samp) tex.SampleLevel(samp, texCoord, 0)
#	define OUT_FRAME_DIM (FrameDim * 0.25)
#	define RCP_OUT_FRAME_DIM (RcpFrameDim * 4)
#	define OUT_FRAME_SCALE (frameScale * 0.25)
#else
#	define RES_MIP 0
#	define READ_DEPTH(tex, px) tex[px]
#	define FULLRES_LOAD(tex, px, texCoord, samp) tex[px]
#	define OUT_FRAME_DIM FrameDim
#	define RCP_OUT_FRAME_DIM RcpFrameDim
#	define OUT_FRAME_SCALE frameScale
#endif

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

// "Efficiently building a matrix to rotate one vector to another"
// http://cs.brown.edu/research/pubs/pdfs/1999/Moller-1999-EBA.pdf / https://dl.acm.org/doi/10.1080/10867651.1999.10487509
// (using https://github.com/assimp/assimp/blob/master/include/assimp/matrix3x3.inl#L275 as a code reference as it seems to be best)
float3x3 RotFromToMatrix(float3 from, float3 to)
{
	const float e = dot(from, to);
	const float f = abs(e);  //(e < 0)? -e:e;

	// WARNING: This has not been tested/worked through, especially not for 16bit floats; seems to work in our special use case (from is always {0, 0, -1}) but wouldn't use it in general
	if (f > float(1.0 - 0.0003))
		return float3x3(1, 0, 0, 0, 1, 0, 0, 0, 1);

	const float3 v = cross(from, to);
	/* ... use this hand optimized version (9 mults less) */
	const float h = (1.0) / (1.0 + e); /* optimization by Gottfried Chen */
	const float hvx = h * v.x;
	const float hvz = h * v.z;
	const float hvxy = hvx * v.y;
	const float hvxz = hvx * v.z;
	const float hvyz = hvz * v.y;

	float3x3 mtx;
	mtx[0][0] = e + hvx * v.x;
	mtx[0][1] = hvxy - v.z;
	mtx[0][2] = hvxz + v.y;

	mtx[1][0] = hvxy + v.z;
	mtx[1][1] = e + h * v.y * v.y;
	mtx[1][2] = hvyz - v.x;

	mtx[2][0] = hvxz - v.y;
	mtx[2][1] = hvyz + v.x;
	mtx[2][2] = e + hvz * v.z;

	return mtx;
}

///////////////////////////////////////////////////////////////////////////////

// credit: Olivier Therrien
float specularLobeHalfAngle(float roughness)
{
	float roughness2 = roughness * roughness;
	return clamp(4.1679 * roughness2 * roughness2 - 9.0127 * roughness2 * roughness + 4.6161 * roughness2 + 1.7048 * roughness + 0.1, 0, Math::HALF_PI);
}

// https://www.gdcvault.com/play/1026701/Fast-Denoising-With-Self-Stabilizing
float3 getSpecularDominantDirection(float3 N, float3 V, float roughness)
{
	float f = (1 - roughness) * (sqrt(1 - roughness) + roughness);
	float3 R = reflect(-V, N);
	float3 D = lerp(N, R, f);

	return normalize(D);
}

#endif