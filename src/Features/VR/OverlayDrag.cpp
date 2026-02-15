#include "Features/VR.h"
#include "RE/B/BSOpenVR.h"
#include "RE/P/PlayerCharacter.h"
#include "Utils/VRUtils.h"

#include <SimpleMath.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <openvr.h>

using namespace DirectX::SimpleMath;
using AttachMode = VR::Settings::OverlayAttachMode;

bool VR::GetGripPressed(bool isLeft, bool isRight) const
{
	bool isLeftHanded = lastKnownLeftHandedMode;

	if (isLeft) {
		if (isLeftHanded) {
			return primaryControllerState[RE::BSOpenVRControllerDevice::Keys::kGrip].isPressed;
		} else {
			return secondaryControllerState[RE::BSOpenVRControllerDevice::Keys::kGrip].isPressed;
		}
	}
	if (isRight) {
		if (isLeftHanded) {
			return secondaryControllerState[RE::BSOpenVRControllerDevice::Keys::kGrip].isPressed;
		} else {
			return primaryControllerState[RE::BSOpenVRControllerDevice::Keys::kGrip].isPressed;
		}
	}
	return false;
}

static bool CanStartAny(vr::ETrackedControllerRole role)
{
	return role != vr::TrackedControllerRole_Invalid;
}

void VR::UpdateOverlayDrag()
{
	if (!CanPerformDrag()) {
		return;
	}

	if (overlayDragState.dragging) {
		UpdateActiveDrag();
	} else {
		TryStartNewDrag();
	}
}

bool VR::CanPerformDrag()
{
	if (!settings.EnableDragToReposition)
		return false;

	if (!globals::menu || !globals::menu->IsEnabled)
		return false;

	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	auto* system = openvr ? openvr->vrSystem : nullptr;
	if (!system)
		return false;

	if (settings.VRMenuControllerDiagnosticsTestMode) {
		return false;
	}

	return true;
}

void VR::UpdateActiveDrag()
{
	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	auto* system = openvr ? openvr->vrSystem : nullptr;
	if (!system)
		return;

	auto resetDragState = [&]() {
		overlayDragState.dragging = false;
		overlayDragState.controllerIndex = vr::k_unTrackedDeviceIndexInvalid;
		overlayDragState.isPrimary = false;
		overlayDragState.isSecondary = false;
	};

	float rawMatrix[3][4];
	if (Util::GetControllerWorldMatrix(overlayDragState.controllerIndex, rawMatrix)) {
		vr::HmdMatrix34_t mat = Util::Float3x4ToHmdMatrix34(rawMatrix);
		Matrix controllerMatrix = Util::HmdMatrix34ToMatrix(mat);

		switch (overlayDragState.mode) {
		case OverlayDragState::DragMode::Controller:
			{
				vr::TrackedDeviceIndex_t attachedControllerIndex = Util::GetControllerIndexForDevice(settings.VRMenuAttachController, lastKnownLeftHandedMode);

				if (attachedControllerIndex != vr::k_unTrackedDeviceIndexInvalid) {
					float attachedM[3][4];
					if (!Util::GetControllerWorldMatrix(attachedControllerIndex, attachedM))
						break;
					{
						Matrix attachedControllerMatrix = Util::HmdMatrix34ToMatrix(Util::Float3x4ToHmdMatrix34(attachedM));

						Vector3 worldDelta(
							controllerMatrix._41 - overlayDragState.initialControllerMatrix._41,
							controllerMatrix._42 - overlayDragState.initialControllerMatrix._42,
							controllerMatrix._43 - overlayDragState.initialControllerMatrix._43);

						Matrix worldToLocal = attachedControllerMatrix.Invert();
						Vector3 localDelta = Vector3::TransformNormal(worldDelta, worldToLocal);

						settings.VRMenuControllerOffsetX = overlayDragState.initialControllerOffset.x + localDelta.x;
						settings.VRMenuControllerOffsetY = overlayDragState.initialControllerOffset.y + localDelta.y;
						settings.VRMenuControllerOffsetZ = overlayDragState.initialControllerOffset.z + localDelta.z;
					}
				}
				break;
			}
		case OverlayDragState::DragMode::FixedWorld:
			{
				Vector3 worldDelta(
					controllerMatrix._41 - overlayDragState.initialControllerMatrix._41,
					controllerMatrix._42 - overlayDragState.initialControllerMatrix._42,
					controllerMatrix._43 - overlayDragState.initialControllerMatrix._43);
				Matrix translated = overlayDragState.initialOverlayMatrix;
				translated._41 += worldDelta.x;
				translated._42 += worldDelta.y;
				translated._43 += worldDelta.z;
				fixedWorldOverlayPosition.m = translated;
				break;
			}
		case OverlayDragState::DragMode::HMD:
			{
				vr::TrackedDevicePose_t hmdPose;
				if (!Util::GetDeviceToAbsoluteTrackingPoseCompatible(vr::TrackingUniverseStanding, 0, &hmdPose, 1))
					break;
				if (hmdPose.bPoseIsValid) {
					Matrix hmdMatrix = Util::HmdMatrix34ToMatrix(hmdPose.mDeviceToAbsoluteTracking);

					Vector3 worldDelta(
						controllerMatrix._41 - overlayDragState.initialControllerMatrix._41,
						controllerMatrix._42 - overlayDragState.initialControllerMatrix._42,
						controllerMatrix._43 - overlayDragState.initialControllerMatrix._43);

					Matrix worldToLocal = hmdMatrix.Invert();
					Vector3 localDelta = Vector3::TransformNormal(worldDelta, worldToLocal);

					static auto lastDeltaLog = std::chrono::steady_clock::now();
					auto nowDelta = std::chrono::steady_clock::now();
					if (std::chrono::duration_cast<std::chrono::milliseconds>(nowDelta - lastDeltaLog).count() > 500) {
						logger::debug("VR Drag Delta - Local: ({:.3f}, {:.3f}, {:.3f})", localDelta.x, localDelta.y, localDelta.z);
						lastDeltaLog = nowDelta;
					}

					settings.VRMenuOffsetX = overlayDragState.initialHMDOffset.x + localDelta.x;
					settings.VRMenuOffsetY = overlayDragState.initialHMDOffset.y + localDelta.y;
					settings.VRMenuOffsetZ = overlayDragState.initialHMDOffset.z + localDelta.z;
					settings.VRMenuScale = overlayDragState.initialHMDScale;

					static std::chrono::steady_clock::time_point lastLog = std::chrono::steady_clock::now();
					auto now = std::chrono::steady_clock::now();
					if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastLog).count() > 500) {
						logger::debug("VR Dragging (3D Mode): Offset ({:.2f}, {:.2f}, {:.2f}), Scale {:.2f}",
							settings.VRMenuOffsetX, settings.VRMenuOffsetY, settings.VRMenuOffsetZ, settings.VRMenuScale);
						lastLog = now;
					}
				}
				break;
			}
		default:
			break;
		}
	}

	// Joystick depth control during grip
	if (overlayDragState.dragging) {
		RE::VRControllerState* gripController = nullptr;
		size_t thumbIdx = 0;
		if (overlayDragState.isPrimary) {
			if (lastKnownLeftHandedMode) {
				gripController = &primaryControllerState;
				thumbIdx = static_cast<size_t>(RE::ControllerRole::Primary);
			} else {
				gripController = &secondaryControllerState;
				thumbIdx = static_cast<size_t>(RE::ControllerRole::Secondary);
			}
		} else if (overlayDragState.isSecondary) {
			if (lastKnownLeftHandedMode) {
				gripController = &secondaryControllerState;
				thumbIdx = static_cast<size_t>(RE::ControllerRole::Secondary);
			} else {
				gripController = &primaryControllerState;
				thumbIdx = static_cast<size_t>(RE::ControllerRole::Primary);
			}
		}

		if (gripController) {
			float thumbY = gripController->thumbsticks[thumbIdx].y;
			const float deadzone = settings.mouseDeadzone;
			const float depthSpeed = 0.02f;
			if (std::abs(thumbY) > deadzone) {
				float depthDelta = -thumbY * depthSpeed;
				if (overlayDragState.mode == OverlayDragState::DragMode::HMD) {
					overlayDragState.initialHMDOffset.z += depthDelta;
					overlayDragState.initialHMDOffset.z = std::clamp(overlayDragState.initialHMDOffset.z, -10.0f, 10.0f);
				} else if (overlayDragState.mode == OverlayDragState::DragMode::Controller) {
					overlayDragState.initialControllerOffset.z += depthDelta;
					overlayDragState.initialControllerOffset.z = std::clamp(overlayDragState.initialControllerOffset.z, -10.0f, 10.0f);
				}
			}
		}
	}

	bool gripPressed = GetGripPressed(overlayDragState.isPrimary, overlayDragState.isSecondary);
	if (!gripPressed) {
		resetDragState();
	}
}

void VR::TryStartNewDrag()
{
	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	auto* system = openvr ? openvr->vrSystem : nullptr;
	if (!system)
		return;

	struct DragMode
	{
		OverlayDragState::DragMode mode;
		bool isActive;
		std::function<bool(vr::ETrackedControllerRole)> canStart;
		std::function<void()> onInit;
	};

	std::vector<DragMode> dragModes;

	// Controller mode - only for opposite hand (highest priority)
	if (settings.attachMode == AttachMode::ControllerOnly || settings.attachMode == AttachMode::Both) {
		dragModes.push_back({ OverlayDragState::DragMode::Controller,
			true,
			[&](vr::ETrackedControllerRole role) {
				vr::TrackedDeviceIndex_t attachedControllerIndex = Util::GetControllerIndexForDevice(settings.VRMenuAttachController, lastKnownLeftHandedMode);
				if (attachedControllerIndex == vr::k_unTrackedDeviceIndexInvalid)
					return false;

				ControllerDevice oppositeDevice = (settings.VRMenuAttachController == ControllerDevice::Primary) ?
			                                          ControllerDevice::Secondary :
			                                          ControllerDevice::Primary;
				vr::TrackedDeviceIndex_t oppositeControllerIndex = Util::GetControllerIndexForDevice(oppositeDevice, lastKnownLeftHandedMode);
				if (oppositeControllerIndex == vr::k_unTrackedDeviceIndexInvalid)
					return false;

				for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i) {
					if (system->GetTrackedDeviceClass(i) == vr::TrackedDeviceClass_Controller) {
						vr::ETrackedControllerRole deviceRole = system->GetControllerRoleForTrackedDeviceIndex(i);
						if (deviceRole == role && i == oppositeControllerIndex)
							return true;
					}
				}
				return false;
			},
			[&]() {
				overlayDragState.initialControllerOffset.x = settings.VRMenuControllerOffsetX;
				overlayDragState.initialControllerOffset.y = settings.VRMenuControllerOffsetY;
				overlayDragState.initialControllerOffset.z = settings.VRMenuControllerOffsetZ;
				overlayDragState.initialControllerMatrix = overlayDragState.startControllerMatrix;
			} });
	}

	// Fixed world mode
	if (settings.VRMenuPositioningMethod == 1) {
		std::function<bool(vr::ETrackedControllerRole)> fixedWorldCanStart;
		if (settings.attachMode == AttachMode::Both) {
			fixedWorldCanStart = [&](vr::ETrackedControllerRole role) {
				vr::TrackedDeviceIndex_t attachedControllerIndex = Util::GetControllerIndexForDevice(settings.VRMenuAttachController, lastKnownLeftHandedMode);
				if (attachedControllerIndex != vr::k_unTrackedDeviceIndexInvalid) {
					vr::ETrackedControllerRole actualAttachedRole = system->GetControllerRoleForTrackedDeviceIndex(attachedControllerIndex);
					return role == actualAttachedRole;
				}
				return false;
			};
		} else {
			fixedWorldCanStart = CanStartAny;
		}

		dragModes.push_back({ OverlayDragState::DragMode::FixedWorld,
			true,
			fixedWorldCanStart,
			[&]() {
				overlayDragState.initialControllerMatrix = overlayDragState.startControllerMatrix;
				overlayDragState.initialOverlayMatrix = fixedWorldOverlayPosition.m;
			} });
	}

	// HMD mode
	if (settings.attachMode == AttachMode::HMDOnly || settings.attachMode == AttachMode::Both) {
		std::function<bool(vr::ETrackedControllerRole)> hmdCanStart;
		if (settings.attachMode == AttachMode::Both) {
			hmdCanStart = [&](vr::ETrackedControllerRole role) {
				vr::TrackedDeviceIndex_t attachedControllerIndex = Util::GetControllerIndexForDevice(settings.VRMenuAttachController, lastKnownLeftHandedMode);
				if (attachedControllerIndex != vr::k_unTrackedDeviceIndexInvalid) {
					vr::ETrackedControllerRole actualAttachedRole = system->GetControllerRoleForTrackedDeviceIndex(attachedControllerIndex);
					return role == actualAttachedRole;
				}
				return false;
			};
		} else {
			hmdCanStart = CanStartAny;
		}

		dragModes.push_back({ OverlayDragState::DragMode::HMD,
			true,
			hmdCanStart,
			[&]() {
				overlayDragState.initialHMDOffset.x = settings.VRMenuOffsetX;
				overlayDragState.initialHMDOffset.y = settings.VRMenuOffsetY;
				overlayDragState.initialHMDOffset.z = settings.VRMenuOffsetZ;
				overlayDragState.initialHMDScale = settings.VRMenuScale;
				overlayDragState.initialControllerMatrix = overlayDragState.startControllerMatrix;
			} });
	}

	for (const auto& mode : dragModes) {
		if (!mode.isActive)
			continue;
		for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i) {
			if (system->GetTrackedDeviceClass(i) != vr::TrackedDeviceClass_Controller)
				continue;
			vr::ETrackedControllerRole role = system->GetControllerRoleForTrackedDeviceIndex(i);
			bool isLeft = (role == vr::ETrackedControllerRole::TrackedControllerRole_LeftHand);
			bool isRight = (role == vr::ETrackedControllerRole::TrackedControllerRole_RightHand);
			if (!mode.canStart(role))
				continue;
			bool gripPressed = GetGripPressed(isLeft, isRight);
			if (!gripPressed)
				continue;
			float rawMatrix[3][4];
			if (!Util::GetControllerWorldMatrix(i, rawMatrix))
				continue;
			vr::HmdMatrix34_t mat = Util::Float3x4ToHmdMatrix34(rawMatrix);
			Matrix controllerMatrix = Util::HmdMatrix34ToMatrix(mat);
			overlayDragState.dragging = true;
			overlayDragState.mode = mode.mode;
			overlayDragState.controllerIndex = i;
			overlayDragState.isPrimary = isLeft;
			overlayDragState.isSecondary = isRight;
			overlayDragState.startControllerMatrix = controllerMatrix;
			mode.onInit();

			if (system && globals::menu->IsEnabled) {
				for (vr::TrackedDeviceIndex_t deviceIdx = 0; deviceIdx < vr::k_unMaxTrackedDeviceCount; ++deviceIdx) {
					if (system->GetTrackedDeviceClass(deviceIdx) == vr::TrackedDeviceClass_Controller) {
						vr::ETrackedControllerRole deviceRole = system->GetControllerRoleForTrackedDeviceIndex(deviceIdx);
						bool isRightController = (deviceRole == vr::ETrackedControllerRole::TrackedControllerRole_RightHand);
						if (isRightController == isRight) {
							openvr->TriggerHapticPulse(isRightController, 25.0f);
							break;
						}
					}
				}
			}

			return;
		}
	}
}

void VR::SetFixedOverlayToCurrentHMD()
{
	vr::HmdMatrix34_t transform = Util::ComputeOverlayTransformFromHMD(
		settings.VRMenuOffsetX,
		settings.VRMenuOffsetY,
		settings.VRMenuOffsetZ);
	fixedWorldOverlayPosition.m = Util::HmdMatrix34ToMatrix(transform);
}

void VR::UpdateFixedWorldPositioning()
{
	if (settings.VRMenuPositioningMethod != 1)
		return;

	if (!fixedWorldOverlayPosition.initialized) {
		fixedWorldOverlayPosition.initialized = true;
		SetFixedOverlayToCurrentHMD();
		auto player = RE::PlayerCharacter::GetSingleton();
		if (player) {
			savedPlayerWorldPos = player->GetPosition();
		}
		return;
	}

	if (settings.VRMenuAutoResetDistance > 0.0f) {
		auto player = RE::PlayerCharacter::GetSingleton();
		if (player) {
			RE::NiPoint3 playerPos = player->GetPosition();
			float sqDist = playerPos.GetSquaredDistance(savedPlayerWorldPos);
			float thresholdSq = settings.VRMenuAutoResetDistance * settings.VRMenuAutoResetDistance;
			if (sqDist > thresholdSq) {
				SetFixedOverlayToCurrentHMD();
				savedPlayerWorldPos = playerPos;
			}
		}
	}
}
