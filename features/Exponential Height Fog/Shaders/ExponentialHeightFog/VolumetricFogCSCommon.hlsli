#ifndef __EXPONENTIAL_HEIGHT_FOG_VOLUMETRIC_CS_COMMON_HLSLI__
#define __EXPONENTIAL_HEIGHT_FOG_VOLUMETRIC_CS_COMMON_HLSLI__

#include "Common/FrameBuffer.hlsli"

cbuffer VolumetricFogCB : register(b0)
{
	uint4 VolumetricFogGridSizeAndFlags;
	float4 VolumetricFogInvGridSizeAndNearFade;
	float4 VolumetricFogGridZParams;
	row_major float4x4 VolumetricFogClipToWorld;
	float4 VolumetricFogFrameJitterOffsets[16];
	float4 VolumetricFogHistoryParameters;
	float4 VolumetricFogJitterParameters;
};

#define VolumetricFogGridSize VolumetricFogGridSizeAndFlags.xyz
#define VolumetricFogHasDirectionalShadowMap ((VolumetricFogGridSizeAndFlags.w & 1u) != 0u)
#define VolumetricFogHasConservativeDepth ((VolumetricFogGridSizeAndFlags.w & 2u) != 0u)
#define VolumetricFogHasIBL ((VolumetricFogGridSizeAndFlags.w & 4u) != 0u)
#define VolumetricFogHasSkylighting ((VolumetricFogGridSizeAndFlags.w & 8u) != 0u)
#define VolumetricFogHasPrevConservativeDepth ((VolumetricFogGridSizeAndFlags.w & 16u) != 0u)
#define VolumetricFogHasLocalLights ((VolumetricFogGridSizeAndFlags.w & 32u) != 0u)
#define VolumetricFogInvGridSize VolumetricFogInvGridSizeAndNearFade.xyz
#define VolumetricFogNearFadeInDistanceInv VolumetricFogInvGridSizeAndNearFade.w
#define VolumetricFogHistoryWeight VolumetricFogHistoryParameters.x
#define VolumetricFogHistoryMissSampleCount max(1u, min(16u, (uint)(VolumetricFogHistoryParameters.y + 0.5f)))
#define VolumetricFogSampleJitterMultiplier VolumetricFogJitterParameters.x
#define VolumetricFogStateFrameIndexMod8 ((uint)(VolumetricFogJitterParameters.y + 0.5f))

#define EXP_HEIGHT_FOG_GRID_SIZE_Z VolumetricFogGridSizeAndFlags.z
#define EXP_HEIGHT_FOG_GRID_Z_PARAMS VolumetricFogGridZParams.xyz
#include "ExponentialHeightFog/VolumetricFogCommon.hlsli"

namespace ExponentialHeightFog
{
	bool IsInsideVolumetricGrid(uint3 coord)
	{
		return all(coord < VolumetricFogGridSize);
	}

	float3 ComputeCellWorldPosition(uint3 coord, float3 cellOffset, out float viewDepth)
	{
		float2 volumeUV = (float2(coord.xy) + cellOffset.xy) * VolumetricFogInvGridSize.xy;

		viewDepth = ComputeVolumetricSliceDepth(max(float(coord.z) + cellOffset.z, 0.0f));

		float2 ndc = volumeUV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
		float deviceZ = (SharedData::CameraData.x - SharedData::CameraData.w / viewDepth) / SharedData::CameraData.z;
		float4 worldPosition = mul(VolumetricFogClipToWorld, float4(ndc, deviceZ, 1.0f));
		return worldPosition.xyz / worldPosition.w;
	}
}

#endif
