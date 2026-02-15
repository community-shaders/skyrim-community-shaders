#include "Features/VR.h"
#include "RE/B/BSOpenVR.h"
#include "Utils/VRUtils.h"

#include <SimpleMath.h>
#include <cmath>
#include <openvr.h>

using namespace DirectX::SimpleMath;
using AttachMode = VR::Settings::OverlayAttachMode;

bool VR::ComputeWandIntersectionForOverlayType(OverlayType type, vr::TrackedDeviceIndex_t controllerIndex, ImVec2& outUV)
{
	float controllerM[3][4];
	if (!Util::GetControllerWorldMatrix(controllerIndex, controllerM)) {
		return false;
	}
	Matrix controllerWorld = Util::HmdMatrix34ToMatrix(Util::Float3x4ToHmdMatrix34(controllerM));
	Vector3 rayOrigin = controllerWorld.Translation();
	Vector3 rayDir = controllerWorld.Forward();

	// Update debug state
	wandState.rayOrigin = rayOrigin;
	wandState.rayDirection = rayDir;
	Matrix overlayWorld;
	if (type == OverlayType::HMD) {
		if (settings.VRMenuPositioningMethod == 1) {  // Fixed World
			overlayWorld = fixedWorldOverlayPosition.m;
		} else {  // HMD Relative
			vr::TrackedDevicePose_t hmdPose;
			if (!Util::GetDeviceToAbsoluteTrackingPoseCompatible(vr::TrackingUniverseStanding, 0, &hmdPose, 1))
				return false;
			if (!hmdPose.bPoseIsValid)
				return false;
			Matrix hmdWorld = Util::HmdMatrix34ToMatrix(hmdPose.mDeviceToAbsoluteTracking);
			Matrix offset = Matrix::CreateTranslation(settings.VRMenuOffsetX, settings.VRMenuOffsetY, settings.VRMenuOffsetZ);
			overlayWorld = offset * hmdWorld;
		}
	} else {  // Controller Relative
		vr::TrackedDeviceIndex_t attachIndex = Util::GetControllerIndexForDevice(settings.VRMenuAttachController, lastKnownLeftHandedMode);
		if (attachIndex == vr::k_unTrackedDeviceIndexInvalid)
			return false;

		float attachM[3][4];
		if (!Util::GetControllerWorldMatrix(attachIndex, attachM))
			return false;
		Matrix attachWorld = Util::HmdMatrix34ToMatrix(Util::Float3x4ToHmdMatrix34(attachM));

		Matrix offset = Matrix::CreateTranslation(settings.VRMenuControllerOffsetX, settings.VRMenuControllerOffsetY, settings.VRMenuControllerOffsetZ);
		overlayWorld = offset * attachWorld;
	}

	if (settings.VRMenuScale < 1e-4f)
		return false;
	overlayWorld = Config::CreateOverlayScaleMatrix(settings.VRMenuScale) * overlayWorld;

	Matrix worldToOverlay = overlayWorld.Invert();
	Vector3 localOrigin = Vector3::Transform(rayOrigin, worldToOverlay);
	Vector3 localDir = Vector3::TransformNormal(rayDir, worldToOverlay);

	if (std::abs(localDir.z) < 1e-6f)
		return false;

	float t = -localOrigin.z / localDir.z;
	if (t < 0.0f)
		return false;

	Vector3 hit = localOrigin + t * localDir;

	if (hit.x < -0.5f || hit.x > 0.5f || hit.y < -0.5f || hit.y > 0.5f)
		return false;

	outUV.x = hit.x + 0.5f;
	outUV.y = 0.5f - hit.y;

	return true;
}

bool VR::ComputeWandIntersection(vr::TrackedDeviceIndex_t controllerIndex, ImVec2& outUV)
{
	bool intersected = false;
	if (settings.attachMode == AttachMode::HMDOnly || settings.attachMode == AttachMode::Both) {
		if (ComputeWandIntersectionForOverlayType(OverlayType::HMD, controllerIndex, outUV)) {
			intersected = true;
		}
	}
	if (!intersected && (settings.attachMode == AttachMode::ControllerOnly || settings.attachMode == AttachMode::Both)) {
		if (ComputeWandIntersectionForOverlayType(OverlayType::Controller, controllerIndex, outUV)) {
			intersected = true;
		}
	}

	if (intersected) {
		wandState.isIntersecting = true;
		wandState.uvCoordinates = outUV;
		wandState.controllerIndex = controllerIndex;
	} else {
		wandState.isIntersecting = false;
	}

	return intersected;
}

void VR::UpdateCursorFromWandPointing()
{
	if (!settings.EnableWandPointing || !globals::menu || !globals::menu->IsEnabled)
		return;

	ImGuiIO& io = ImGui::GetIO();

	vr::TrackedDeviceIndex_t pointingController = vr::k_unTrackedDeviceIndexInvalid;

	if (settings.attachMode == AttachMode::ControllerOnly || settings.attachMode == AttachMode::Both) {
		ControllerDevice oppositeController = (settings.VRMenuAttachController == ControllerDevice::Primary) ?
		                                          ControllerDevice::Secondary :
		                                          ControllerDevice::Primary;
		pointingController = Util::GetControllerIndexForDevice(oppositeController, lastKnownLeftHandedMode);
	} else {
		pointingController = Util::GetControllerIndexForDevice(ControllerDevice::Primary, lastKnownLeftHandedMode);
	}

	if (pointingController == vr::k_unTrackedDeviceIndexInvalid) {
		wandState.isIntersecting = false;
		return;
	}

	ImVec2 uv;
	bool intersected = ComputeWandIntersection(pointingController, uv);

	if (intersected) {
		float screenX = uv.x * io.DisplaySize.x;
		float screenY = uv.y * io.DisplaySize.y;

		screenX = std::clamp(screenX, 0.0f, io.DisplaySize.x);
		screenY = std::clamp(screenY, 0.0f, io.DisplaySize.y);

		io.MousePos = ImVec2(screenX, screenY);
		io.AddMousePosEvent(screenX, screenY);
		io.MouseDrawCursor = true;
		io.WantSetMousePos = true;
	} else {
		wandState.isIntersecting = false;
		io.MouseDrawCursor = false;
		io.WantSetMousePos = false;
	}
}
