#pragma once
#include "D3D.h"
#include <SimpleMath.h>
#include <d3d11.h>
#include <openvr.h>
#include <vector>

// Forward declarations - actual definitions are in Features/VR.h
enum class ControllerDevice;
struct ButtonCombo;

namespace Util
{
	void DrawButtonCombo(const std::vector<ButtonCombo>& combo, bool showControllerLabels);
	vr::HmdMatrix34_t ComputeOverlayTransformFromHMD(float distance, float offsetX, float offsetY, float offsetZ);

	struct VROverlayResources
	{
		vr::VROverlayHandle_t handle = vr::k_ulOverlayHandleInvalid;
		ID3D11Texture2D* texture = nullptr;
		ID3D11RenderTargetView* rtv = nullptr;

		void Create(ID3D11Device* device, const char* key, const char* name, int width, int height);
		void Destroy();
		void SetInputFlags(vr::IVROverlay* overlay);
		bool SubmitTexture(vr::IVROverlay* overlay);
	};

	// OpenVR matrix conversion helpers
	inline Matrix HmdMatrix34ToMatrix(const vr::HmdMatrix34_t& m)
	{
		return Matrix(
			m.m[0][0], m.m[0][1], m.m[0][2], m.m[0][3],
			m.m[1][0], m.m[1][1], m.m[1][2], m.m[1][3],
			m.m[2][0], m.m[2][1], m.m[2][2], m.m[2][3],
			0, 0, 0, 1);
	}

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

	inline vr::HmdMatrix34_t Float3x4ToHmdMatrix34(const float m[3][4])
	{
		vr::HmdMatrix34_t mat;
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 4; ++j)
				mat.m[i][j] = m[i][j];
		return mat;
	}

	// Controller utilities
	inline vr::TrackedDeviceIndex_t GetControllerIndexForRole(vr::IVRSystem* system, vr::ETrackedControllerRole role, bool fallbackToFirst = true)
	{
		if (!system)
			return vr::k_unTrackedDeviceIndexInvalid;

		vr::TrackedDeviceIndex_t firstController = vr::k_unTrackedDeviceIndexInvalid;
		for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i) {
			if (system->GetTrackedDeviceClass(i) == vr::TrackedDeviceClass_Controller) {
				if (firstController == vr::k_unTrackedDeviceIndexInvalid) {
					firstController = i;
				}
				if (system->GetControllerRoleForTrackedDeviceIndex(i) == role) {
					return i;
				}
			}
		}
		return fallbackToFirst ? firstController : vr::k_unTrackedDeviceIndexInvalid;
	}

	inline bool GetControllerTransform(vr::IVRSystem* system, vr::TrackedDeviceIndex_t index, Matrix& outTransform)
	{
		if (!system || index == vr::k_unTrackedDeviceIndexInvalid)
			return false;

		vr::TrackedDevicePose_t pose;
		system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0, &pose, 1);
		if (!pose.bPoseIsValid)
			return false;

		outTransform = HmdMatrix34ToMatrix(pose.mDeviceToAbsoluteTracking);
		return true;
	}
}