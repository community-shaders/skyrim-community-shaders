#ifndef __EXPONENTIAL_HEIGHT_FOG_VOLUMETRIC_CS_COMMON_HLSLI__
#define __EXPONENTIAL_HEIGHT_FOG_VOLUMETRIC_CS_COMMON_HLSLI__

#include "Common/FrameBuffer.hlsli"

cbuffer VolumetricFogCB : register(b0)
{
	uint4 VolumetricFogGridSizeAndFlags;         // near volume
	float4 VolumetricFogInvGridSizeAndNearFade;  // near volume
	float4 VolumetricFogGridZParams;             // near volume
	row_major float4x4 VolumetricFogClipToWorld;
	float4 VolumetricFogFrameJitterOffsets[16];
	float4 VolumetricFogHistoryParameters;
	float4 VolumetricFogJitterParameters;
	uint4 VolumetricFogFarGridSizeAndFlags;  // far volume (quarter lattice)
	float4 VolumetricFogFarInvGridSizeAndNearFade;
	float4 VolumetricFogFarGridZParams;
	float4 VolumetricFogFarRange;  // x = far volume start depth, y = far volume end depth, zw = unused
};

#if defined(VOLUMETRIC_FOG_FAR_GRID)
// The far volume variant reads its own grid parameters from the same constant buffer.
#	define VOLUMETRIC_FOG_FLAGS VolumetricFogFarGridSizeAndFlags.w
#	define VOLUMETRIC_FOG_GRID_SIZE VolumetricFogFarGridSizeAndFlags.xyz
#	define VOLUMETRIC_FOG_INV_GRID_SIZE VolumetricFogFarInvGridSizeAndNearFade.xyz
#	define VOLUMETRIC_FOG_NEAR_FADE_INV VolumetricFogFarInvGridSizeAndNearFade.w
#	define EXP_HEIGHT_FOG_GRID_SIZE_Z VolumetricFogFarGridSizeAndFlags.z
#	define EXP_HEIGHT_FOG_GRID_Z_PARAMS VolumetricFogFarGridZParams.xyz
#else
#	define VOLUMETRIC_FOG_FLAGS VolumetricFogGridSizeAndFlags.w
#	define VOLUMETRIC_FOG_GRID_SIZE VolumetricFogGridSizeAndFlags.xyz
#	define VOLUMETRIC_FOG_INV_GRID_SIZE VolumetricFogInvGridSizeAndNearFade.xyz
#	define VOLUMETRIC_FOG_NEAR_FADE_INV VolumetricFogInvGridSizeAndNearFade.w
#	define EXP_HEIGHT_FOG_GRID_SIZE_Z VolumetricFogGridSizeAndFlags.z
#	define EXP_HEIGHT_FOG_GRID_Z_PARAMS VolumetricFogGridZParams.xyz
#endif

#define VolumetricFogGridSize VOLUMETRIC_FOG_GRID_SIZE
#define VolumetricFogHasDirectionalShadowMap ((VOLUMETRIC_FOG_FLAGS & 1u) != 0u)
#define VolumetricFogHasConservativeDepth ((VOLUMETRIC_FOG_FLAGS & 2u) != 0u)
#define VolumetricFogHasIBL ((VOLUMETRIC_FOG_FLAGS & 4u) != 0u)
#define VolumetricFogHasSkylighting ((VOLUMETRIC_FOG_FLAGS & 8u) != 0u)
#define VolumetricFogHasPrevConservativeDepth ((VOLUMETRIC_FOG_FLAGS & 16u) != 0u)
#define VolumetricFogHasLocalLights ((VOLUMETRIC_FOG_FLAGS & 32u) != 0u)
#define VolumetricFogInvGridSize VOLUMETRIC_FOG_INV_GRID_SIZE
#define VolumetricFogNearFadeInDistanceInv VOLUMETRIC_FOG_NEAR_FADE_INV
#define VolumetricFogHistoryWeight VolumetricFogHistoryParameters.x
#define VolumetricFogHistoryMissSampleCount max(1u, min(16u, (uint)(VolumetricFogHistoryParameters.y + 0.5f)))
#define VolumetricFogSampleJitterMultiplier VolumetricFogJitterParameters.x
#define VolumetricFogStateFrameIndexMod8 ((uint)(VolumetricFogJitterParameters.y + 0.5f))
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
