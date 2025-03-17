// functions that get or set game data

#pragma once

/**
 @def GET_INSTANCE_MEMBER
 @brief Set variable in current namespace based on instance member from GetRuntimeData or GetVRRuntimeData.
  
 @warning The class must have both a GetRuntimeData() and GetVRRuntimeData() function.
  
 @param a_value The instance member value to access (e.g., renderTargets).
 @param a_source The instance of the class (e.g., state).
 @result The a_value will be set as a variable in the current namespace. (e.g., auto& renderTargets = state->renderTargets;)
 */
#define GET_INSTANCE_MEMBER(a_value, a_source) \
	auto& a_value = !REL::Module::IsVR() ? a_source->GetRuntimeData().a_value : a_source->GetVRRuntimeData().a_value;

/**
 @def GET_INSTANCE_MEMBER_PTR
 @brief Return refptr to runtimedata in current namespace based on instance member from GetRuntimeData or GetVRRuntimeData.
  
 @warning The class must have both a GetRuntimeData() and GetVRRuntimeData() function.
  
 @param a_value The instance member value to access (e.g., renderTargets).
 @param a_source The instance of the class (e.g., state).
 @result The a_value will be returned as a refptr. (e.g., &state->renderTargets;)
 */
#define GET_INSTANCE_MEMBER_PTR(a_value, a_source) \
	&(!REL::Module::IsVR() ? a_source->GetRuntimeData().a_value : a_source->GetVRRuntimeData().a_value)

namespace Util
{
	void StoreTransform3x4NoScale(DirectX::XMFLOAT3X4& Dest, const RE::NiTransform& Source);

	float4 TryGetWaterData(float offsetX, float offsetY);
	float4 GetCameraData();
	bool GetTemporal();
	float GetVerticalFOVRad();

	RE::NiPoint3 GetAverageEyePosition();
	RE::NiPoint3 GetEyePosition(int eyeIndex);
	RE::BSGraphics::ViewData GetCameraData(int eyeIndex);

	float2 ConvertToDynamic(float2 a_size);

	struct DispatchCount
	{
		uint x;
		uint y;
	};
	DispatchCount GetScreenDispatchCount(bool a_dynamic = true);

	/**
	 * @brief Checks if dynamic resolution is currently enabled.
	 * 
	 * @return true if dynamic resolution is enabled, false otherwise.
	 */
	bool IsDynamicResolution();

	/**
	 * Usage:
	 * static FrameChecker frame_checker;
	 * if(frame_checker.isNewFrame())
	 *     ...
	*/
	class FrameChecker
	{
	private:
		uint32_t last_frame = UINT32_MAX;

	public:
		bool IsNewFrame(uint32_t frame)
		{
			bool retval = last_frame != frame;
			last_frame = frame;
			return retval;
		}
		bool IsNewFrame();
	};
}  // namespace Util
