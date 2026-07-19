#ifndef __MOTION_BLUR_DEPENDENCY_HLSL__
#define __MOTION_BLUR_DEPENDENCY_HLSL__

#include "Common/FrameBuffer.hlsli"

namespace MotionBlur
{
	// Get camera motion vector in screen space
	float2 GetSSMotionVector2(float4 a_wsPosition, uint a_eyeIndex = 0)
	{
		float4 cameraMovement = FrameBuffer::CameraPosAdjust - FrameBuffer::CameraPreviousPosAdjust;
		float4 screenPosition = mul(FrameBuffer::CameraViewProjUnjittered, a_wsPosition);
		float4 previousScreenPosition = mul(FrameBuffer::CameraPreviousViewProjUnjittered, a_wsPosition + cameraMovement);
		screenPosition.xy = screenPosition.xy / screenPosition.ww;
		previousScreenPosition.xy = previousScreenPosition.xy / previousScreenPosition.ww;
		return float2(-0.5, 0.5) * (screenPosition.xy - previousScreenPosition.xy);
	}

	// Get object motion vector in screen space, note camera movement are not included, the diff in previousWSPosition should contain that
	float2 GetSSMotionVector(float4 a_wsPosition, float4 a_previousWSPosition, uint a_eyeIndex = 0)
	{
		float4 screenPosition = mul(FrameBuffer::CameraViewProjUnjittered, a_wsPosition);
		float4 previousScreenPosition = mul(FrameBuffer::CameraPreviousViewProjUnjittered, a_previousWSPosition);
		screenPosition.xy = screenPosition.xy / screenPosition.ww;
		previousScreenPosition.xy = previousScreenPosition.xy / previousScreenPosition.ww;
		return float2(-0.5, 0.5) * (screenPosition.xy - previousScreenPosition.xy);
	}
}

#endif  // __MOTION_BLUR_DEPENDENCY_HLSL__
