#pragma once
#include "D3D.h"
#include "Utils/Input.h"
#include <SimpleMath.h>
#include <d3d11.h>
#include <imgui.h>  // For ImVec4
#include <openvr.h>
#include <vector>

// Forward declarations - actual definitions are in Features/VR.h
using ControllerDevice = InputDeviceType;
using ButtonCombo = InputCombo;

/**
 * @brief VR utility functions and helpers for OpenVR integration
 *
 * This namespace provides a collection of utility functions for VR development,
 * including overlay management, matrix transformations, controller utilities,
 * and UI drawing functions for VR-specific elements.
 */
namespace Util
{
	// -----------------------------------------------------------------------------
	// Centralized UI Colors for Util functions
	// -----------------------------------------------------------------------------
	namespace Colors
	{
		constexpr ImVec4 Primary = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);    // Yellow
		constexpr ImVec4 Secondary = ImVec4(0.0f, 0.5f, 1.0f, 1.0f);  // Blue
		constexpr ImVec4 Both = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);       // Green
		constexpr ImVec4 Default = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);    // White
	}

	inline ImVec4 GetControllerPrimaryColor() { return Colors::Primary; }
	inline ImVec4 GetControllerSecondaryColor() { return Colors::Secondary; }
	inline ImVec4 GetControllerBothColor() { return Colors::Both; }
	inline ImVec4 GetControllerDefaultColor() { return Colors::Default; }

	/**
	 * @brief Draws a button combination in the ImGui interface with color coding
	 * @param combo Vector of ButtonCombo structures representing the key combination
	 * @param showControllerLabels Whether to show controller device labels (Primary/Secondary/Both)
	 *
	 * This function renders button combinations with color-coded text:
	 * - Green: Primary controller
	 * - Blue: Secondary controller
	 * - Purple: Both controllers
	 *
	 * @example
	 * ```cpp
	 * std::vector<ButtonCombo> combo = { ButtonCombo::Primary(kTrigger), ButtonCombo::Secondary(kGrip) };
	 * Util::DrawButtonCombo(combo, true);
	 * ```
	 */
	void DrawButtonCombo(const std::vector<ButtonCombo>& combo, bool showControllerLabels);

	/**
	 * @brief Computes a transformation matrix for positioning an overlay relative to the HMD
	 * @param offsetX Horizontal offset from HMD in meters (positive = right)
	 * @param offsetY Vertical offset from HMD in meters (positive = up)
	 * @param offsetZ Depth offset from HMD in meters (positive = away from user)
	 * @return HMD transformation matrix with applied offsets
	 *
	 * This function gets the current HMD pose and applies the specified offsets
	 * in HMD local space to create a transformation matrix suitable for overlay positioning.
	 */
	vr::HmdMatrix34_t ComputeOverlayTransformFromHMD(float offsetX, float offsetY, float offsetZ);

	/**
	 * @brief Common OpenVR system access pattern with validation
	 *
	 * This struct provides a standardized way to access OpenVR interfaces
	 * with proper validation and error handling. It encapsulates the common
	 * pattern of getting BSOpenVR singleton and extracting the system and overlay interfaces.
	 */
	struct OpenVRContext
	{
		RE::BSOpenVR* openvr = nullptr;     ///< BSOpenVR singleton instance
		vr::IVRSystem* system = nullptr;    ///< OpenVR system interface
		vr::IVROverlay* overlay = nullptr;  ///< OpenVR overlay interface

		/**
		 * @brief Constructor that initializes all OpenVR interfaces
		 *
		 * Automatically retrieves the BSOpenVR singleton and extracts
		 * the system and overlay interfaces for immediate use.
		 */
		OpenVRContext();

		/**
		 * @brief Check if basic VR system is available
		 * @return true if both openvr and system interfaces are valid
		 */
		bool IsValid() const { return openvr && system; }

		/**
		 * @brief Check if overlay functionality is available
		 * @return true if all interfaces (including overlay) are valid
		 */
		bool HasOverlay() const { return IsValid() && overlay; }
	};

	/**
	 * @brief Get controller index for our ControllerDevice enum
	 * @param device The controller device type (Primary/Secondary)
	 * @param isLeftHanded Whether the user is left-handed (affects primary/secondary mapping)
	 * @return Tracked device index or vr::k_unTrackedDeviceIndexInvalid if not found
	 *
	 * This function maps our ControllerDevice enum to actual OpenVR tracked device indices,
	 * taking into account user handedness for primary/secondary controller assignment.
	 */
	vr::TrackedDeviceIndex_t GetControllerIndexForDevice(ControllerDevice device, bool isLeftHanded);

	/**
	 * @brief Get controller world matrix from OpenVR pose
	 * @param index The tracked device index of the controller
	 * @param out Output array[3][4] for the transformation matrix
	 * @return true if the pose was valid and matrix was retrieved successfully
	 *
	 * This function retrieves the current world-space transformation matrix
	 * for a VR controller in a format compatible with OpenVR matrix operations.
	 */
	bool GetControllerWorldMatrix(vr::TrackedDeviceIndex_t index, float out[3][4]);

	/**
	 * @brief OpenComposite-compatible function to get device poses
	 * @param eOrigin The tracking universe origin
	 * @param fPredictedSecondsToPhotonsFromNow Prediction time for poses
	 * @param pTrackedDevicePoseArray Output array for tracked device poses
	 * @param unTrackedDevicePoseArrayCount Number of poses to retrieve
	 * @return true if poses were retrieved successfully
	 *
	 * This function provides a compatibility layer for getting device poses that works
	 * with both standard OpenVR and OpenComposite. It uses the compositor interface
	 * when available for better OpenComposite compatibility.
	 */
	bool GetDeviceToAbsoluteTrackingPoseCompatible(vr::ETrackingUniverseOrigin eOrigin, float fPredictedSecondsToPhotonsFromNow, vr::TrackedDevicePose_t* pTrackedDevicePoseArray, uint32_t unTrackedDevicePoseArrayCount);

	//=============================================================================
	// MATRIX CONVERSION UTILITIES
	//=============================================================================

	/**
	 * @brief Converts an OpenVR HmdMatrix34_t to a DirectX SimpleMath Matrix
	 * @param m The OpenVR 3x4 transformation matrix to convert
	 * @return DirectX SimpleMath 4x4 Matrix with bottom row set to [0,0,0,1]
	 *
	 * This function converts between OpenVR's 3x4 transformation matrix format
	 * and DirectX SimpleMath's 4x4 matrix format, adding the implicit bottom row.
	 */
	inline Matrix HmdMatrix34ToMatrix(const vr::HmdMatrix34_t& m)
	{
		// OpenVR matrices are row-major but designed for column-vector math (M * v).
		// DirectX SimpleMath uses row-vector math (v * M).
		// We need to transpose the rotation and move translation to the bottom row.
		return Matrix(
			m.m[0][0], m.m[1][0], m.m[2][0], 0.0f,
			m.m[0][1], m.m[1][1], m.m[2][1], 0.0f,
			m.m[0][2], m.m[1][2], m.m[2][2], 0.0f,
			m.m[0][3], m.m[1][3], m.m[2][3], 1.0f);
	}

	/**
	 * @brief Converts a DirectX SimpleMath Matrix to an OpenVR HmdMatrix34_t
	 * @param mat The DirectX SimpleMath 4x4 matrix to convert
	 * @return OpenVR 3x4 transformation matrix (bottom row is discarded)
	 *
	 * This function converts from DirectX SimpleMath's 4x4 matrix format
	 * to OpenVR's 3x4 transformation matrix format, discarding the bottom row.
	 */
	inline vr::HmdMatrix34_t MatrixToHmdMatrix34(const Matrix& mat)
	{
		vr::HmdMatrix34_t m{};
		// Transpose rotation back (row-vector → column-vector) and extract translation from row 4
		m.m[0][0] = mat._11;
		m.m[0][1] = mat._21;
		m.m[0][2] = mat._31;
		m.m[0][3] = mat._41;
		m.m[1][0] = mat._12;
		m.m[1][1] = mat._22;
		m.m[1][2] = mat._32;
		m.m[1][3] = mat._42;
		m.m[2][0] = mat._13;
		m.m[2][1] = mat._23;
		m.m[2][2] = mat._33;
		m.m[2][3] = mat._43;
		return m;
	}

	/**
	 * @brief Converts a raw 3x4 float array to an OpenVR HmdMatrix34_t
	 * @param m Raw 3x4 float array in [row][column] format
	 * @return OpenVR HmdMatrix34_t structure
	 *
	 * This function provides a convenient way to convert raw transformation
	 * matrices from other APIs into OpenVR's matrix format.
	 */
	inline vr::HmdMatrix34_t Float3x4ToHmdMatrix34(const float m[3][4])
	{
		vr::HmdMatrix34_t mat;
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 4; ++j)
				mat.m[i][j] = m[i][j];
		return mat;
	}

	/**
	 * @brief Gets the Inter-Pupillary Distance (IPD) from the HMD
	 * @return IPD in meters, or 0.064 (average human IPD) as fallback
	 *
	 * Tries multiple methods to determine IPD:
	 * 1. Query Prop_UserIpdMeters_Float property directly
	 * 2. Calculate from eye-to-head transforms
	 * 3. Fallback to average human IPD (64mm)
	 */
	inline float GetIPDFromHMD()
	{
		RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
		if (!openvr || !openvr->vrSystem)
			return 0.064f;  // Default fallback IPD in meters

		// Method 1: Query IPD property directly
		vr::ETrackedPropertyError error = vr::TrackedProp_UnknownProperty;
		float ipd = openvr->vrSystem->GetFloatTrackedDeviceProperty(
			vr::k_unTrackedDeviceIndex_Hmd,
			vr::Prop_UserIpdMeters_Float,
			&error);

		if (error == vr::TrackedProp_Success && ipd > 0.0f && ipd < 0.1f) {
			return ipd;
		}

		// Method 2: Calculate from eye-to-head transforms
		vr::HmdMatrix34_t leftEye = openvr->vrSystem->GetEyeToHeadTransform(vr::Eye_Left);
		vr::HmdMatrix34_t rightEye = openvr->vrSystem->GetEyeToHeadTransform(vr::Eye_Right);

		// Eye separation is in the X translation component (m[0][3])
		float eyeSeparation = std::abs(leftEye.m[0][3] - rightEye.m[0][3]);

		if (eyeSeparation > 0.0f && eyeSeparation < 0.1f) {
			return eyeSeparation;
		}

		// Fallback to average human IPD
		return 0.064f;
	}

}