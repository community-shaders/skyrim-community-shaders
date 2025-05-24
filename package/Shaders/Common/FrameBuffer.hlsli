#ifndef __FRAMEBUFFER_DEPENDENCY_HLSL__
#define __FRAMEBUFFER_DEPENDENCY_HLSL__

namespace FrameBuffer
{

	cbuffer PerFrame : register(b12)
	{
#if !defined(VR)
		row_major float4x4 CameraView[1] : packoffset(c0);
		row_major float4x4 CameraProj[1] : packoffset(c4);
		row_major float4x4 CameraViewProj[1] : packoffset(c8);
		row_major float4x4 CameraViewProjUnjittered[1] : packoffset(c12);
		row_major float4x4 CameraPreviousViewProjUnjittered[1] : packoffset(c16);
		row_major float4x4 CameraProjUnjittered[1] : packoffset(c20);
		row_major float4x4 CameraProjUnjitteredInverse[1] : packoffset(c24);
		row_major float4x4 CameraViewInverse[1] : packoffset(c28);
		row_major float4x4 CameraViewProjInverse[1] : packoffset(c32);
		row_major float4x4 CameraProjInverse[1] : packoffset(c36);
		float4 CameraPosAdjust[1] : packoffset(c40);
		float4 CameraPreviousPosAdjust[1] : packoffset(c41);  // fDRClampOffset in w
		float4 FrameParams : packoffset(c42);                 // inverse fGamma in x, some flags in yzw
		float4 DynamicResolutionParams1 : packoffset(c43);    // fDynamicResolutionWidthRatio in x,
															  // fDynamicResolutionHeightRatio in y,
															  // fDynamicResolutionPreviousWidthRatio in z,
															  // fDynamicResolutionPreviousHeightRatio in w
		float4 DynamicResolutionParams2 : packoffset(c44);    // inverse fDynamicResolutionWidthRatio in x, inverse
															  // fDynamicResolutionHeightRatio in y,
															  // fDynamicResolutionWidthRatio - fDRClampOffset in z,
															  // fDynamicResolutionPreviousWidthRatio - fDRClampOffset in w
#else
		row_major float4x4 CameraView[2] : packoffset(c0);
		row_major float4x4 CameraProj[2] : packoffset(c8);
		row_major float4x4 CameraViewProj[2] : packoffset(c16);
		row_major float4x4 CameraViewProjUnjittered[2] : packoffset(c24);
		row_major float4x4 CameraPreviousViewProjUnjittered[2] : packoffset(c32);
		row_major float4x4 CameraProjUnjittered[2] : packoffset(c40);
		row_major float4x4 CameraProjUnjitteredInverse[2] : packoffset(c48);
		row_major float4x4 CameraViewInverse[2] : packoffset(c56);
		row_major float4x4 CameraViewProjInverse[2] : packoffset(c64);
		row_major float4x4 CameraProjInverse[2] : packoffset(c72);
		float4 CameraPosAdjust[2] : packoffset(c80);
		float4 CameraPreviousPosAdjust[2] : packoffset(c82);  // fDRClampOffset in w
		float4 FrameParams : packoffset(c84);                 // inverse fGamma in x, some flags in yzw
		float4 DynamicResolutionParams1 : packoffset(c85);    // fDynamicResolutionWidthRatio in x,
															  // fDynamicResolutionHeightRatio in y,
															  // fDynamicResolutionPreviousWidthRatio in z,
															  // fDynamicResolutionPreviousHeightRatio in w
		float4 DynamicResolutionParams2 : packoffset(c86);    // inverse fDynamicResolutionWidthRatio in x, inverse
															  // fDynamicResolutionHeightRatio in y,
															  // fDynamicResolutionWidthRatio - fDRClampOffset in z,
															  // fDynamicResolutionPreviousWidthRatio - fDRClampOffset in w
#endif  // !VR
	}

	float2 GetDynamicResolutionAdjustedScreenPosition(float2 screenPosition, uint stereo = 1)
	{
		float2 screenPositionDR = DynamicResolutionParams1.xy * screenPosition;
		float2 minValue = 0;
		float2 maxValue = float2(DynamicResolutionParams2.z, DynamicResolutionParams1.y);
#if defined(VR)
		// VR sometimes will clamp to stereouv
		if (stereo) {
			bool isRight = screenPosition.x >= 0.5;
			float minFactor = isRight ? 1 : 0;
			minValue.x = 0.5 * (DynamicResolutionParams2.z * minFactor);
			float maxFactor = isRight ? 2 : 1;
			maxValue.x = 0.5 * (DynamicResolutionParams2.z * maxFactor);
		}
#endif
		return clamp(screenPositionDR, minValue, maxValue);
	}

	float3 GetDynamicResolutionAdjustedScreenPosition(float3 screenPositionDR, uint stereo = 1)
	{
		return float3(GetDynamicResolutionAdjustedScreenPosition(screenPositionDR.xy, stereo), screenPositionDR.z);
	}

	float2 GetDynamicResolutionUnadjustedScreenPosition(float2 screenPositionDR)
	{
		return screenPositionDR * DynamicResolutionParams2.xy;
	}

	float3 GetDynamicResolutionUnadjustedScreenPosition(float3 screenPositionDR)
	{
		return float3(GetDynamicResolutionUnadjustedScreenPosition(screenPositionDR.xy), screenPositionDR.z);
	}

	float2 GetPreviousDynamicResolutionAdjustedScreenPosition(float2 screenPosition)
	{
		float2 screenPositionDR = DynamicResolutionParams1.zw * screenPosition;
		float2 minValue = 0;
		float2 maxValue = float2(DynamicResolutionParams2.w, DynamicResolutionParams1.w);
#if defined(VR)
		bool isRight = screenPosition.x >= 0.5;
		float minFactor = isRight ? 1 : 0;
		minValue.x = 0.5 * (DynamicResolutionParams2.w * minFactor);
		float maxFactor = isRight ? 2 : 1;
		maxValue.x = 0.5 * (DynamicResolutionParams2.w * maxFactor);
#endif
		return clamp(screenPositionDR, minValue, maxValue);
	}

	float3 ToSRGBColor(float3 linearColor)
	{
		return pow(linearColor, FrameParams.x);
	}

	float3 WorldToView(float3 x, bool is_position = true, uint a_eyeIndex = 0)
	{
		float4 newPosition = float4(x, (float)is_position);
		return mul(CameraView[a_eyeIndex], newPosition).xyz;
	}

	float2 ViewToUV(float3 x, bool is_position = true, uint a_eyeIndex = 0)
	{
		float4 newPosition = float4(x, (float)is_position);
		float4 uv = mul(CameraProj[a_eyeIndex], newPosition);
		return (uv.xy / uv.w) * float2(0.5f, -0.5f) + 0.5f;
	}

	/**
	* @brief Checks if the UV coordinates are outside the frame, considering dynamic resolution if specified.
	*
	* This function is used to determine whether the provided UV coordinates lie outside the valid range of [0,1].
	* If dynamic resolution is enabled, it adjusts the range according to dynamic resolution parameters.
	*
	* @param[in] uv The UV coordinates to check.
	* @param[in] dynamicres Optional flag indicating whether dynamic resolution is applied. Default is false.
	* @return True if the UV coordinates are outside the frame, false otherwise.
	*/
	bool IsOutsideFrame(float2 uv, bool dynamicres = false)
	{
		float2 max = dynamicres ? DynamicResolutionParams1.xy : float2(1, 1);
		return any(uv < float2(0, 0) || uv > max.xy);
	}

}

#endif  //__FRAMEBUFFER_DEPENDENCY_HLSL__