#ifndef __EXPONENTIAL_HEIGHT_FOG_VOLUMETRIC_CS_COMMON_HLSLI__
#define __EXPONENTIAL_HEIGHT_FOG_VOLUMETRIC_CS_COMMON_HLSLI__

#include "Common/FrameBuffer.hlsli"
#include "Common/VR.hlsli"
#include "ExponentialHeightFog/VolumetricFogCommon.hlsli"

cbuffer VolumetricFogCB : register(b0)
{
	uint4 VolumetricFogGridSizeAndFlags;
	float4 VolumetricFogInvGridSizeAndNearFade;
};

#define VolumetricFogGridSize VolumetricFogGridSizeAndFlags.xyz
#define VolumetricFogHasDirectionalShadowMap VolumetricFogGridSizeAndFlags.w
#define VolumetricFogInvGridSize VolumetricFogInvGridSizeAndNearFade.xyz
#define VolumetricFogNearFadeInDistanceInv VolumetricFogInvGridSizeAndNearFade.w

namespace ExponentialHeightFog
{
	bool IsInsideVolumetricGrid(uint3 coord)
	{
		return all(coord < VolumetricFogGridSize);
	}

	float3 ComputeCellWorldPosition(uint3 coord, float3 cellOffset, out uint eyeIndex, out float viewDepth)
	{
		float2 volumeUV = (float2(coord.xy) + cellOffset.xy) * VolumetricFogInvGridSize.xy;
		eyeIndex = Stereo::GetEyeIndexFromTexCoord(volumeUV);
		float2 eyeUV = Stereo::ConvertFromStereoUV(volumeUV, eyeIndex);

		float normalizedSlice = (float(coord.z) + cellOffset.z) * VolumetricFogInvGridSize.z;
		viewDepth = ComputeVolumetricSliceDepth(normalizedSlice);

		float2 ndc = eyeUV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
		float4 farView = mul(FrameBuffer::CameraProjInverse[eyeIndex], float4(ndc, 1.0f, 1.0f));
		farView.xyz *= rcp(max(farView.w, 1e-6f));

		float viewZ = max(abs(farView.z), 1e-4f);
		float3 viewPosition = farView.xyz * (viewDepth / viewZ);
		return FrameBuffer::ViewToWorld(viewPosition, true, eyeIndex);
	}
}

#endif
