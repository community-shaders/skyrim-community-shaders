#ifndef COMPUTESHADER
#	define COMPUTESHADER
#endif

#define OMIT_PS_NAMESPACE
#define PS_PREPASS_SAMPLERS
#define PS_PREPASS_RSRCS
#define PS_NO_RSRCS
#define PS_ENABLE_DIRLIGHT_TRANSMITTANCE
#define PS_LINEAR_SHADOW_SAMPLER
#define CLOUD_SHADOW_REGISTER t4
#include "Common/FrameBuffer.hlsli"
#include "Common/Random.hlsli"
Texture3D<float> TexShadowVolume : register(t5);
#include "PhysicalSky/Common.hlsli"

#if defined(HALF_RES)
#	define RES_MULT 2
#else
#	define RES_MULT 1
#endif

Texture2D<float> TexDepth : register(t0);
#include "PhysicalSky/ApShadow.hlsli"

RWTexture2D<unorm float> RWTexOutput : register(u0);

float TraceShadow(float2 pixelCenter, float depth, float jitter)
{
	const SharedData::PhysSkyData data = SharedData::physSkyData;
	const float2 uv = FrameBuffer::GetDynamicResolutionUnadjustedScreenPosition(pixelCenter * data.rcpTexDim);
	const bool skyRay = depth >= 1.0;
	float4 posWorld = float4(2 * float2(uv.x, -uv.y + 1) - 1, skyRay ? 0.5 : depth, 1);
	posWorld = mul(FrameBuffer::CameraViewProjInverse, posWorld);
	posWorld.xyz /= posWorld.w;
	const float distance = length(posWorld.xyz);
	return AtmosphereShadow::TraceShadow(posWorld.xyz / max(distance, 1e-20), skyRay ? AP_MAX_DIST : distance, jitter);
}

[numthreads(8, 8, 1)] void main(uint2 tid : SV_DispatchThreadID) {
	const uint2 frameDim = uint2(SharedData::physSkyData.frameDim);
	const uint2 pxCoords = tid * RES_MULT;
	if (any(pxCoords >= frameDim))
		return;

	const uint2 seed = Random::pcg2d(tid);
	const float jitter = Random::R2Modified(SharedData::FrameCountAlwaysActive, seed / 4294967295.f).x;

#if defined(HALF_RES)
	const uint2 lastCoords = min(pxCoords + 1u, frameDim - 1u);
	const float4 depths = float4(
		TexDepth[pxCoords],
		TexDepth[uint2(lastCoords.x, pxCoords.y)],
		TexDepth[uint2(pxCoords.x, lastCoords.y)],
		TexDepth[lastCoords]);
	const float4 linearDepths = SharedData::GetScreenDepths(depths);
	const float minDepth = min(min(linearDepths.x, linearDepths.y), min(linearDepths.z, linearDepths.w));
	const float maxDepth = max(max(linearDepths.x, linearDepths.y), max(linearDepths.z, linearDepths.w));
	const bool sharedRay = maxDepth - minDepth <= minDepth * 0.01;
	const uint rayCount = sharedRay ? 1u : 4u;
	[loop] for (uint i = 0; i < rayCount; ++i)
	{
		const uint2 pixel = pxCoords + uint2(i & 1u, i >> 1u);
		if (any(pixel >= frameDim))
			continue;
		const float2 pixelCenter = sharedRay ? (float2(pxCoords) + float2(lastCoords) + 1.0) * 0.5 : pixel + 0.5;
		const float depth = sharedRay ? dot(depths, 0.25) : depths[i];
		const float shadow = TraceShadow(pixelCenter, depth, jitter);
		[branch] if (sharedRay)
		{
			[unroll] for (uint j = 0; j < 4; ++j)
			{
				const uint2 outputPixel = pxCoords + uint2(j & 1u, j >> 1u);
				if (all(outputPixel < frameDim))
					RWTexOutput[outputPixel] = shadow;
			}
		}
		else
		{
			RWTexOutput[pixel] = shadow;
		}
	}
#else
	RWTexOutput[pxCoords] = TraceShadow(pxCoords + 0.5, TexDepth[pxCoords], jitter);
#endif
}
