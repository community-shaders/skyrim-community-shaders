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

#include "Common/Color.hlsli"
#include "Common/FrameBuffer.hlsli"
#include "Common/GBuffer.hlsli"
#include "NRD/NRDReblurSH.hlsli"
#include "ScreenSpaceGI/common.hlsli"

Texture2D<float4> srcRadiance : register(t0);
#ifdef GI
Texture2D<float4> srcPrevSsgi : register(t1);
Texture2D<float3> srcCurrentGeo : register(t2);
Texture2D<float2> srcMotion : register(t3);
Texture2D<float3> srcPrevGeo : register(t4);
Texture2D<unorm float3> srcAlbedo : register(t5);
#endif

RWTexture2D<float3> outRadiance0 : register(u0);
RWTexture2D<float3> outRadiance1 : register(u1);
RWTexture2D<float3> outRadiance2 : register(u2);
RWTexture2D<float3> outRadiance3 : register(u3);
RWTexture2D<float3> outRadiance4 : register(u4);

float3 RadianceMIPFilter(float3 radiance0, float3 radiance1, float3 radiance2, float3 radiance3)
{
	// Linear filtering for radiance - simple average
	return (radiance0 + radiance1 + radiance2 + radiance3) * 0.25;
}

#ifdef GI
float3 LoadSsgiMultiBounce(uint2 pixCoord)
{
	float3 currentGeo = srcCurrentGeo[pixCoord];
	float viewspaceZ = currentGeo.x;
	if (viewspaceZ <= FP_Z)
		return 0;

	float2 uv = (pixCoord + 0.5) * RCP_OUT_FRAME_DIM;
	float2 previousScreenUV = uv + srcMotion[pixCoord];
	if (any(previousScreenUV <= 0.0) || any(previousScreenUV >= 1.0))
		return 0;

	float2 previousTextureUV = FrameBuffer::GetPreviousDynamicResolutionAdjustedScreenPosition(previousScreenUV);
	float3 previousGeo = srcPrevGeo.SampleLevel(samplerPointClamp, previousTextureUV, 0);
	if (previousGeo.x <= FP_Z)
		return 0;

	float3 viewspacePos = ScreenToViewPosition(uv, viewspaceZ);
	float3 worldPos = ViewToWorldPosition(viewspacePos, FrameBuffer::CameraViewInverse);
	float3 previousWorldPos = worldPos + FrameBuffer::CameraPosAdjust.xyz - FrameBuffer::CameraPreviousPosAdjust.xyz;
	float4 previousClip = mul(FrameBuffer::CameraPreviousViewProjUnjittered, float4(previousWorldPos, 1.0));
	if (previousClip.w <= 0.0)
		return 0;
	float previousExpectedZ = abs(previousClip.w);
	float depthTolerance = max(1.0, abs(previousExpectedZ) * 0.02);
	if (abs(previousGeo.x - previousExpectedZ) > depthTolerance)
		return 0;

	float3 normalWS = GBuffer::DecodeNormal(currentGeo.yz);
	float3 previousNormalWS = GBuffer::DecodeNormal(previousGeo.yz);
	if (dot(normalWS, previousNormalWS) < 0.85)
		return 0;

	float4 packed = srcPrevSsgi.SampleLevel(samplerPointClamp, previousTextureUV, 0);
	// Both REBLUR diffuse formats store total illumination as YCoCg in SH0/base.
	// SH1 contains only the directional moment and is unnecessary for feedback.
	float3 illumination = _NRD_YCoCgToLinear(packed.xyz);

	// History becomes a light source only after reflection by the source surface.
	float3 sourceAlbedo = Color::IrradianceToLinear(srcAlbedo[pixCoord] / Color::PBRLightingScale);
	return max(0, illumination) * saturate(sourceAlbedo);
}
#endif

groupshared float3 g_scratchRadiance[8][8];
[numthreads(8, 8, 1)] void main(uint2 dispatchThreadID : SV_DispatchThreadID, uint2 groupThreadID : SV_GroupThreadID) {
	const float2 frameScale = FrameDim * RcpTexDim;

	// MIP 0
	const uint2 baseCoord = dispatchThreadID;
	const uint2 pixCoord = baseCoord * 2;
	const float2 uv = (pixCoord + .5) * RcpFrameDim;

	float4 rad0 = srcRadiance.GatherRed(samplerPointClamp, uv * frameScale);
	float4 rad1 = srcRadiance.GatherGreen(samplerPointClamp, uv * frameScale);
	float4 rad2 = srcRadiance.GatherBlue(samplerPointClamp, uv * frameScale);

	float3 radiance0 = Color::RadianceToLinear(float3(rad0.w, rad1.w, rad2.w));
	float3 radiance1 = Color::RadianceToLinear(float3(rad0.z, rad1.z, rad2.z));
	float3 radiance2 = Color::RadianceToLinear(float3(rad0.x, rad1.x, rad2.x));
	float3 radiance3 = Color::RadianceToLinear(float3(rad0.y, rad1.y, rad2.y));

#ifdef GI
	[branch] if (MultiBounceMode >= 2)
	{
		radiance0 += LoadSsgiMultiBounce(pixCoord + uint2(0, 0));
		radiance1 += LoadSsgiMultiBounce(pixCoord + uint2(1, 0));
		radiance2 += LoadSsgiMultiBounce(pixCoord + uint2(0, 1));
		radiance3 += LoadSsgiMultiBounce(pixCoord + uint2(1, 1));
	}
#endif

	outRadiance0[pixCoord + uint2(0, 0)] = radiance0;
	outRadiance0[pixCoord + uint2(1, 0)] = radiance1;
	outRadiance0[pixCoord + uint2(0, 1)] = radiance2;
	outRadiance0[pixCoord + uint2(1, 1)] = radiance3;

	// MIP 1
	float3 rm1 = RadianceMIPFilter(radiance0, radiance1, radiance2, radiance3);
	outRadiance1[baseCoord] = rm1;
	g_scratchRadiance[groupThreadID.x][groupThreadID.y] = rm1;

	GroupMemoryBarrierWithGroupSync();

	// MIP 2
	[branch] if (all((groupThreadID.xy % 2) == 0))
	{
		float3 inTL = g_scratchRadiance[groupThreadID.x + 0][groupThreadID.y + 0];
		float3 inTR = g_scratchRadiance[groupThreadID.x + 1][groupThreadID.y + 0];
		float3 inBL = g_scratchRadiance[groupThreadID.x + 0][groupThreadID.y + 1];
		float3 inBR = g_scratchRadiance[groupThreadID.x + 1][groupThreadID.y + 1];

		float3 rm2 = RadianceMIPFilter(inTL, inTR, inBL, inBR);
		outRadiance2[baseCoord / 2] = rm2;
		g_scratchRadiance[groupThreadID.x][groupThreadID.y] = rm2;
	}

	GroupMemoryBarrierWithGroupSync();

	// MIP 3
	[branch] if (all((groupThreadID.xy % 4) == 0))
	{
		float3 inTL = g_scratchRadiance[groupThreadID.x + 0][groupThreadID.y + 0];
		float3 inTR = g_scratchRadiance[groupThreadID.x + 2][groupThreadID.y + 0];
		float3 inBL = g_scratchRadiance[groupThreadID.x + 0][groupThreadID.y + 2];
		float3 inBR = g_scratchRadiance[groupThreadID.x + 2][groupThreadID.y + 2];

		float3 rm3 = RadianceMIPFilter(inTL, inTR, inBL, inBR);
		outRadiance3[baseCoord / 4] = rm3;
		g_scratchRadiance[groupThreadID.x][groupThreadID.y] = rm3;
	}

	GroupMemoryBarrierWithGroupSync();

	// MIP 4
	[branch] if (all((groupThreadID.xy % 8) == 0))
	{
		float3 inTL = g_scratchRadiance[groupThreadID.x + 0][groupThreadID.y + 0];
		float3 inTR = g_scratchRadiance[groupThreadID.x + 4][groupThreadID.y + 0];
		float3 inBL = g_scratchRadiance[groupThreadID.x + 0][groupThreadID.y + 4];
		float3 inBR = g_scratchRadiance[groupThreadID.x + 4][groupThreadID.y + 4];

		float3 rm4 = RadianceMIPFilter(inTL, inTR, inBL, inBR);
		outRadiance4[baseCoord / 8] = rm4;
	}
}
