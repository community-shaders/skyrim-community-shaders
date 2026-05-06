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

	/**
	 * @brief Clamps already dynamic-resolution-adjusted UVs to the valid render region.
	 *
	 * Use this when `screenPositionDR` has already been transformed into dynamic-resolution
	 * space by custom math (for example, after jitter removal or other UV manipulation).
	 * This function only clamps; it does not apply dynamic-resolution scaling.
	 *
	 * In VR, clamping is restricted to the current eye half to avoid cross-eye sampling.
	 *
	 * @param[in] screenPositionDR UVs already expressed in dynamic-resolution space.
	 * @param[in] screenPosition Original normalized screen UVs (used to infer eye in VR).
	 * @param[in] stereo Whether to apply stereo eye-half clamping in VR. Default is 1.
	 * @return Clamped dynamic-resolution UVs.
	 */
	float2 ClampDynamicResolutionAdjustedScreenPosition(float2 screenPositionDR, float2 screenPosition, uint stereo = 1)
	{
		float2 minValue = 0;
		float2 maxValue = float2(DynamicResolutionParams2.z, DynamicResolutionParams1.y);
#if defined(VR)
		// VR uses side-by-side stereo packing in the shared render target.
		// Clamp within the current eye's half to avoid cross-eye sampling.
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

	// Projects a world-space (camera-relative) point into NDC using the eye's CameraViewProj
	// and returns the post-perspective z (NDC depth). Combine with SharedData::GetScreenDepth
	// to get a linear view-space distance suitable for cascade-split comparisons.
	float GetShadowDepth(float3 positionWS, uint eyeIndex)
	{
		float4 positionCS = mul(FrameBuffer::CameraViewProj[eyeIndex], float4(positionWS, 1));
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
	 * @param[in] stereo Whether to apply stereo eye-half clamping in VR. Default is 1.
	 * @return Dynamic-resolution-adjusted and clamped UVs.
	 */
	float2 GetDynamicResolutionAdjustedScreenPosition(float2 screenPosition, uint stereo = 1)
	{
		float2 screenPositionDR = DynamicResolutionParams1.xy * screenPosition;
		return ClampDynamicResolutionAdjustedScreenPosition(screenPositionDR, screenPosition, stereo);
	}

	/**
	 * @brief float3 overload of `GetDynamicResolutionAdjustedScreenPosition(float2, uint)`.
	 *
	 * Applies dynamic-resolution adjustment/clamp to XY and preserves Z unchanged.
	 */
	float3 GetDynamicResolutionAdjustedScreenPosition(float3 screenPositionDR, uint stereo = 1)
	{
		return float3(GetDynamicResolutionAdjustedScreenPosition(screenPositionDR.xy, stereo), screenPositionDR.z);
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
		return pow(abs(linearColor), FrameParams.x);
	}

	float3 WorldToView(float3 x, bool is_position = true, uint a_eyeIndex = 0)
	{
		float4 newPosition = float4(x, (float)is_position);
		return mul(CameraView[a_eyeIndex], newPosition).xyz;
	}

	float3 ViewToWorld(float3 x, bool is_position = true, uint a_eyeIndex = 0)
	{
		float4 newPosition = float4(x, (float)is_position);
		return mul(CameraViewInverse[a_eyeIndex], newPosition).xyz;
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
		return any(uv < float2(0, 0)) || any(uv > max.xy);
	}

}

#endif  //__FRAMEBUFFER_DEPENDENCY_HLSL__
