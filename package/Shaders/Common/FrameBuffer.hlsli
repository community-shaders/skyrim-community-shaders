#ifndef __FRAMEBUFFER_DEPENDENCY_HLSL__
#define __FRAMEBUFFER_DEPENDENCY_HLSL__

namespace FrameBuffer
{

	cbuffer PerFrame : register(b12)
	{
		row_major float4x4 CameraView : packoffset(c0);
		row_major float4x4 CameraProj : packoffset(c4);
		row_major float4x4 CameraViewProj : packoffset(c8);
		row_major float4x4 CameraViewProjUnjittered : packoffset(c12);
		row_major float4x4 CameraPreviousViewProjUnjittered : packoffset(c16);
		row_major float4x4 CameraProjUnjittered : packoffset(c20);
		row_major float4x4 CameraProjUnjitteredInverse : packoffset(c24);
		row_major float4x4 CameraViewInverse : packoffset(c28);
		row_major float4x4 CameraViewProjInverse : packoffset(c32);
		row_major float4x4 CameraProjInverse : packoffset(c36);
		float4 CameraPosAdjust : packoffset(c40);
		float4 CameraPreviousPosAdjust : packoffset(c41);  // fDRClampOffset in w
		float4 FrameParams : packoffset(c42);                 // inverse fGamma in x, some flags in yzw
		float4 DynamicResolutionParams1 : packoffset(c43);    // fDynamicResolutionWidthRatio in x,
															  // fDynamicResolutionHeightRatio in y,
															  // fDynamicResolutionPreviousWidthRatio in z,
															  // fDynamicResolutionPreviousHeightRatio in w
		float4 DynamicResolutionParams2 : packoffset(c44);    // inverse fDynamicResolutionWidthRatio in x, inverse
															  // fDynamicResolutionHeightRatio in y,
															  // fDynamicResolutionWidthRatio - fDRClampOffset in z,
															  // fDynamicResolutionPreviousWidthRatio - fDRClampOffset in w
	}

	/**
	 * @brief Clamps already dynamic-resolution-adjusted UVs to the valid render region.
	 *
	 * Use this when `screenPositionDR` has already been transformed into dynamic-resolution
	 * space by custom math (for example, after jitter removal or other UV manipulation).
	 * This function only clamps; it does not apply dynamic-resolution scaling.
	 *
	 * @param[in] screenPositionDR UVs already expressed in dynamic-resolution space.
	 * @param[in] screenPosition Original normalized screen UVs.
	 * @return Clamped dynamic-resolution UVs.
	 */
	float2 ClampDynamicResolutionAdjustedScreenPosition(float2 screenPositionDR, float2 screenPosition)
	{
		float2 minValue = 0;
		float2 maxValue = float2(DynamicResolutionParams2.z, DynamicResolutionParams1.y);
		return clamp(screenPositionDR, minValue, maxValue);
	}

	// Projects a world-space (camera-relative) point into NDC using CameraViewProj
	// and returns the post-perspective z (NDC depth). Combine with SharedData::GetScreenDepth
	// to get a linear view-space distance suitable for cascade-split comparisons.
	float GetShadowDepth(float3 positionWS)
	{
		float4 positionCS = mul(FrameBuffer::CameraViewProj, float4(positionWS, 1));
		return positionCS.z / positionCS.w;
	}

	/**
	 * @brief Converts normalized screen UVs to dynamic-resolution UVs and clamps them.
	 *
	 * Use this when starting from regular screen UVs in [0, 1] that are not yet adjusted
	 * for dynamic resolution.
	 *
	 * If UVs are already in dynamic-resolution space, use
	 * `ClampDynamicResolutionAdjustedScreenPosition(...)` instead.
	 *
	 * @param[in] screenPosition Normalized screen UVs in non-DR space.
	 * @return Dynamic-resolution-adjusted and clamped UVs.
	 */
	float2 GetDynamicResolutionAdjustedScreenPosition(float2 screenPosition)
	{
		float2 screenPositionDR = DynamicResolutionParams1.xy * screenPosition;
		return ClampDynamicResolutionAdjustedScreenPosition(screenPositionDR, screenPosition);
	}

	/**
	 * @brief float3 overload of `GetDynamicResolutionAdjustedScreenPosition(float2, uint)`.
	 *
	 * Applies dynamic-resolution adjustment/clamp to XY and preserves Z unchanged.
	 */
	float3 GetDynamicResolutionAdjustedScreenPosition(float3 screenPositionDR)
	{
		return float3(GetDynamicResolutionAdjustedScreenPosition(screenPositionDR.xy), screenPositionDR.z);
	}

	/**
	 * @brief Converts dynamic-resolution UVs back to normalized non-DR UVs.
	 */
	float2 GetDynamicResolutionUnadjustedScreenPosition(float2 screenPositionDR)
	{
		return screenPositionDR * DynamicResolutionParams2.xy;
	}

	/**
	 * @brief float3 overload of `GetDynamicResolutionUnadjustedScreenPosition(float2)`.
	 *
	 * Converts XY back to non-DR UVs and preserves Z unchanged.
	 */
	float3 GetDynamicResolutionUnadjustedScreenPosition(float3 screenPositionDR)
	{
		return float3(GetDynamicResolutionUnadjustedScreenPosition(screenPositionDR.xy), screenPositionDR.z);
	}

	float2 GetPreviousDynamicResolutionAdjustedScreenPosition(float2 screenPosition)
	{
		float2 screenPositionDR = DynamicResolutionParams1.zw * screenPosition;
		float2 minValue = 0;
		float2 maxValue = float2(DynamicResolutionParams2.w, DynamicResolutionParams1.w);
		return clamp(screenPositionDR, minValue, maxValue);
	}

	float3 ToSRGBColor(float3 linearColor)
	{
		return pow(abs(linearColor), FrameParams.x);
	}

	float3 WorldToView(float3 x, bool is_position = true)
	{
		float4 newPosition = float4(x, (float)is_position);
		return mul(CameraView, newPosition).xyz;
	}

	float3 ViewToWorld(float3 x, bool is_position = true)
	{
		float4 newPosition = float4(x, (float)is_position);
		return mul(CameraViewInverse, newPosition).xyz;
	}

	float2 ViewToUV(float3 x, bool is_position = true)
	{
		float4 newPosition = float4(x, (float)is_position);
		float4 uv = mul(CameraProj, newPosition);
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
		return any(uv < float2(0, 0)) || any(uv > max.xy);
	}

}

#endif  //__FRAMEBUFFER_DEPENDENCY_HLSL__
