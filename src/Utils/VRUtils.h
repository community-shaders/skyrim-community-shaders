#pragma once
#include "D3D.h"
#include <SimpleMath.h>
#include <d3d11.h>
#include <imgui.h>
#include <openvr.h>
#include <vector>

// Forward declarations - actual definitions are in Features/VR.h
enum class ControllerDevice;
struct ButtonCombo;

/**
 * @brief VR utility functions and helpers for OpenVR integration
 *
 * This namespace provides a collection of utility functions for VR development,
 * including overlay management, matrix transformations, controller utilities,
 * and UI drawing functions for VR-specific elements.
 */
namespace Util
{
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
	 * @brief Sets standard input flags for a VR overlay
	 * @param overlay Pointer to the OpenVR overlay interface
	 * @param handle Handle to the VR overlay
	 *
	 * This function configures an overlay to accept VR input events including:
	 * - Scroll events
	 * - Touchpad events
	 * - Gamepad events
	 * - Dashboard visibility
	 */
	void SetOverlayInputFlags(vr::IVROverlay* overlay, vr::VROverlayHandle_t handle);

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
	 * @brief Creates a transformation matrix for controller-relative overlay positioning
	 * @param offsetX Horizontal offset from controller in meters
	 * @param offsetY Vertical offset from controller in meters
	 * @param offsetZ Depth offset from controller in meters
	 * @param width Width of the overlay in meters
	 * @param height Height of the overlay in meters
	 * @return Transformation matrix for controller-relative positioning
	 *
	 * This function creates a transformation matrix that positions an overlay
	 * relative to a VR controller with the specified dimensions and offsets.
	 */
	vr::HmdMatrix34_t CreateControllerOverlayTransform(float offsetX, float offsetY, float offsetZ, float width, float height);

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
	 * @brief Get UI color for controller device type
	 * @param device The controller device type
	 * @param isRecording Whether the device is in recording mode (affects color)
	 * @return ImVec4 color value for UI rendering
	 *
	 * This function provides consistent color coding for different controller
	 * device types in the UI, with special handling for recording mode.
	 */
	ImVec4 GetControllerDeviceColor(ControllerDevice device, bool isRecording = false);

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
		return Matrix(
			m.m[0][0], m.m[0][1], m.m[0][2], m.m[0][3],
			m.m[1][0], m.m[1][1], m.m[1][2], m.m[1][3],
			m.m[2][0], m.m[2][1], m.m[2][2], m.m[2][3],
			0, 0, 0, 1);
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
		m.m[0][0] = mat._11;
		m.m[0][1] = mat._12;
		m.m[0][2] = mat._13;
		m.m[0][3] = mat._14;
		m.m[1][0] = mat._21;
		m.m[1][1] = mat._22;
		m.m[1][2] = mat._23;
		m.m[1][3] = mat._24;
		m.m[2][0] = mat._31;
		m.m[2][1] = mat._32;
		m.m[2][2] = mat._33;
		m.m[2][3] = mat._34;
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

}