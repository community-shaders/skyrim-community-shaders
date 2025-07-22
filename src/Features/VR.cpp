#include "VR.h"
#include "Menu.h"
#include "RE/B/BSOpenVR.h"
#include <openvr.h>

#include "DX12SwapChain.h"
#include "State.h"
#include "Utils/D3D.h"
#include "Utils/PerfUtils.h"
#include "Utils/UI.h"
#include "Utils/VRUtils.h"
#include <DirectXMath.h>
#include <SimpleMath.h>
#include <cmath>
#include <d3d11.h>
#include <imgui_impl_dx11.h>
#include <magic_enum.hpp>
#include <unordered_map>
#include <windows.h>

using AttachMode = VR::Settings::OverlayAttachMode;

constexpr int kOverlayWidth = 1920;
constexpr int kOverlayHeight = 1080;

// Helper function to get controller index for our ControllerDevice enum
vr::TrackedDeviceIndex_t GetControllerIndexForDevice(ControllerDevice device)
{
	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	auto* system = openvr ? openvr->vrSystem : nullptr;
	if (!system)
		return vr::k_unTrackedDeviceIndexInvalid;

	// Determine the OpenVR role based on handedness and our device enum
	vr::ETrackedControllerRole targetRole;
	bool isLeftHanded = RE::BSOpenVRControllerDevice::IsLeftHandedMode();

	if (device == ControllerDevice::Primary) {
		// Primary controller = dominant hand
		targetRole = isLeftHanded ? vr::ETrackedControllerRole::TrackedControllerRole_LeftHand : vr::ETrackedControllerRole::TrackedControllerRole_RightHand;
	} else {
		// Secondary controller = non-dominant hand
		targetRole = isLeftHanded ? vr::ETrackedControllerRole::TrackedControllerRole_RightHand : vr::ETrackedControllerRole::TrackedControllerRole_LeftHand;
	}

	// Find controller with the target role
	for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i) {
		if (system->GetTrackedDeviceClass(i) == vr::TrackedDeviceClass_Controller) {
			if (system->GetControllerRoleForTrackedDeviceIndex(i) == targetRole) {
				return i;
			}
		}
	}
	return vr::k_unTrackedDeviceIndexInvalid;
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	VR::Settings,
	EnableDepthBufferCulling,
	MinOccludeeBoxExtent,
	VRMenuScale,
	VRMenuPositioningMethod,
	attachMode,
	VRMenuAttachController,
	VRMenuOffsetX,
	VRMenuOffsetY,
	VRMenuOffsetZ,
	VRMenuControllerOffsetX,
	VRMenuControllerOffsetY,
	VRMenuControllerOffsetZ,
	mouseDeadzone,
	mouseSpeed,
	dragHighlightColor,
	VRMenuOpenKeys,
	VRMenuCloseKeys,
	VROverlayOpenKeys,
	VROverlayCloseKeys,
	comboTimeout,
	EnableDragToReposition,
	ShowHowToUseMessage)

vr::HmdMatrix34_t VR::ComputeOverlayTransformFromHMD()
{
	vr::HmdMatrix34_t transform = {};
	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	if (openvr) {
		auto* system = openvr->vrSystem;
		if (system) {
			vr::TrackedDevicePose_t hmdPose;
			system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0, &hmdPose, 1);
			if (hmdPose.bPoseIsValid) {
				float offsetX = settings.VRMenuOffsetX;
				float offsetY = settings.VRMenuOffsetY;
				float offsetZ = settings.VRMenuOffsetZ;
				transform = hmdPose.mDeviceToAbsoluteTracking;
				// Apply HMD overlay offsets (in HMD local space)
				transform.m[0][3] += transform.m[0][0] * offsetX + transform.m[0][1] * offsetY + transform.m[0][2] * offsetZ;
				transform.m[1][3] += transform.m[1][0] * offsetX + transform.m[1][1] * offsetY + transform.m[1][2] * offsetZ;
				transform.m[2][3] += transform.m[2][0] * offsetX + transform.m[2][1] * offsetY + transform.m[2][2] * offsetZ;
			}
		}
	}
	return transform;
}

void VR::DrawOverlay()
{
	static LARGE_INTEGER overlayShowStart = { 0 };
	static LARGE_INTEGER freq = { 0 };

	bool shouldShow = settings.ShowHowToUseMessage && globals::game::ui && globals::game::ui->IsMenuOpen(RE::MainMenu::MENU_NAME);

	if (!shouldShow) {
		overlayShowStart.QuadPart = 0;  // Reset timer when overlay is not shown
		return;
	}

	if (freq.QuadPart == 0) {
		QueryPerformanceFrequency(&freq);
	}

	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);

	if (overlayShowStart.QuadPart == 0) {
		overlayShowStart = now;
	}

	double elapsed = double(now.QuadPart - overlayShowStart.QuadPart) / double(freq.QuadPart);
	const double kAutoHideSeconds = 15.0;
	if (elapsed >= kAutoHideSeconds) {
		return;
	}
	int secondsLeft = int(std::ceil(kAutoHideSeconds - elapsed));

	ImGuiIO& io = ImGui::GetIO();
	ImVec2 overlaySize(480, 0);  // width, height auto
	ImVec2 overlayPos = ImVec2((io.DisplaySize.x - overlaySize.x) * 0.5f, 80.0f);
	ImGui::SetNextWindowPos(overlayPos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(overlaySize, ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.92f);
	// Helper for button color
	auto GetButtonColor = [](ControllerDevice device) -> ImVec4 {
		switch (device) {
		case ControllerDevice::Primary:
			return ImVec4(0.0f, 1.0f, 0.0f, 1.0f);  // Green
		case ControllerDevice::Secondary:
			return ImVec4(0.0f, 0.6f, 1.0f, 1.0f);  // Blue
		case ControllerDevice::Both:
			return ImVec4(0.5f, 0.0f, 0.5f, 1.0f);  // Purple
		default:
			return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
		}
	};
	ImGui::Begin("HowToUseOverlay", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
	ImGui::Text("How to Use VR Community Shaders Menu:");
	ImGui::Separator();
	ImGui::Text("You must be in the Main Menu or Tween Menu for these key binds to work.");
	ImGui::Spacing();
	ImGui::Text("Open Menu: ");
	Util::DrawButtonCombo(settings.VRMenuOpenKeys, true);
	ImGui::Text("\nClose Menu: ");
	Util::DrawButtonCombo(settings.VRMenuCloseKeys, true);
	ImGui::Spacing();
	ImGui::TextDisabled("(This message will auto-disable in %d seconds)", secondsLeft);
	ImGui::TextDisabled("(You can disable this message in VR settings > Controller Instructions)");
	ImGui::End();
}

namespace
{
	void DrawControllerInputInstructions(VR::Settings& settings);
	void DrawGeneralVRSettings(VR::Settings& settings);
	void DrawMenuSettings(VR::Settings& settings);
	void DrawMouseSettings(VR::Settings& settings);
	void DrawDragSettings(VR::Settings& settings);
	void DrawKeyBindings(VR* vr);
	void DrawDebugSection(VR* vr);
}

void VR::DrawSettings()
{
	static std::unordered_map<uint32_t, ControllerDevice> recordingButtonControllers;
	auto menu = globals::menu;
	if (!menu)
		return;
	if (ImGui::BeginTabBar("##VRTabs", ImGuiTabBarFlags_None)) {
		// General Settings Tab
		if (ImGui::BeginTabItem("General")) {
			if (ImGui::BeginChild("##VRGeneralFrame", { 0, 0 }, true)) {
				DrawControllerInputInstructions(settings);
				DrawGeneralVRSettings(settings);
				DrawMenuSettings(settings);
				DrawMouseSettings(settings);
				DrawDragSettings(settings);
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		// Key Bindings Tab
		if (ImGui::BeginTabItem("Bindings")) {
			if (ImGui::BeginChild("##VRBindingsFrame", { 0, 0 }, true)) {
				DrawKeyBindings(this);
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		// Debug Tab (existing debug functionality)
		if (ImGui::BeginTabItem("Debug")) {
			if (ImGui::BeginChild("##VRDebugFrame", { 0, 0 }, true)) {
				DrawDebugSection(this);
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	// Combo recording popup
	if (this->isCapturingCombo) {
		ImGui::OpenPopup("Record Combo");
		ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_FirstUseEver);
		if (ImGui::BeginPopupModal("Record Combo", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			// Helper function to get button name
			auto GetButtonName = [](uint32_t key) -> const char* {
				switch (key) {
				case static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kTrigger):
					return "Trigger";
				case static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kGrip):
					return "Grip";
				case static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kTouchpadClick):
					return "Touchpad";
				case static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kJoystickTrigger):
					return "Stick Click";
				case static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kXA):
					return "A/X";
				case static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kBY):
					return "B/Y";
				default:
					return "Unknown";
				}
			};
			// Helper for button color
			auto GetButtonColor = [](ControllerDevice device) -> ImVec4 {
				switch (device) {
				case ControllerDevice::Primary:
					return ImVec4(0.0f, 1.0f, 0.0f, 1.0f);  // Green
				case ControllerDevice::Secondary:
					return ImVec4(0.0f, 0.6f, 1.0f, 1.0f);  // Blue
				case ControllerDevice::Both:
					return ImVec4(1.0f, 0.65f, 0.0f, 1.0f);  // Orange
				default:
					return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
				}
			};

			ImGui::Text("Recording combo for: %s", this->currentComboName ? this->currentComboName : "Unknown");
			ImGui::Spacing();

			ImGui::TextDisabled("(During recording, any controller's buttons can be used. Requirement is only enforced during use.)");

			ImGui::Spacing();

			// Show countdown timer with color
			double remainingTime = this->comboTimeout - (Util::GetNowSecs() - this->comboStartTime);
			ImVec4 timerColor;
			if (remainingTime > 2.0) {
				timerColor = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);  // Green
			} else if (remainingTime > 1.0) {
				timerColor = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);  // Yellow
			} else {
				timerColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);  // Red
			}
			ImGui::TextColored(timerColor, "Time remaining: %.1f seconds", remainingTime);

			ImGui::Spacing();

			// Show recorded buttons
			if (this->recordedCombo.empty()) {
				ImGui::Text("Press buttons to record combo...");
			} else {
				ImGui::Text("Recorded buttons:");
				// Create a sorted list of decoded buttons for consistent display
				std::vector<ButtonCombo> sortedRecordedCombos;
				for (size_t i = 0; i < this->recordedCombo.size(); ++i) {
					sortedRecordedCombos.push_back(this->recordedCombo[i]);
				}
				std::sort(sortedRecordedCombos.begin(), sortedRecordedCombos.end(),
					[](const ButtonCombo& a, const ButtonCombo& b) {
						return a.GetKey() < b.GetKey();
					});

				Util::DrawButtonCombo(sortedRecordedCombos, false);
			}

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			// Instructions
			ImGui::Text("Press ENTER to accept, ESC to cancel");

			// Handle button recording
			// Check for VR controller button presses - record them (any controller allowed during recording)
			bool buttonPressed = false;
			uint32_t pressedKey = 0;
			ControllerDevice pressedDevice = ControllerDevice::Both;  // Default to Both, will set below

			// Check primary controller buttons
			for (const auto& [keyCode, buttonState] : primaryControllerState.GetActiveButtons()) {
				if (buttonState->isPressed) {
					pressedKey = keyCode;
					buttonPressed = true;
					pressedDevice = ControllerDevice::Primary;
					break;
				}
			}

			// Check secondary controller buttons if primary didn't have any
			if (!buttonPressed) {
				for (const auto& [keyCode, buttonState] : secondaryControllerState.GetActiveButtons()) {
					if (buttonState->isPressed) {
						pressedKey = keyCode;
						buttonPressed = true;
						pressedDevice = ControllerDevice::Secondary;
						break;
					}
				}
			}

			// Record button press
			if (buttonPressed) {
				// Check if this button is already in the combo (avoid duplicates)
				auto it = recordingButtonControllers.find(pressedKey);
				if (it == recordingButtonControllers.end()) {
					// Not yet recorded, add with the current device
					recordingButtonControllers[pressedKey] = pressedDevice;
				} else {
					// Already recorded, if the other controller is now pressed, set to BOTH
					if (it->second != pressedDevice && it->second != ControllerDevice::Both) {
						it->second = ControllerDevice::Both;
					}
				}
				// Update the recordedCombo vector to match the map
				this->recordedCombo.clear();
				for (const auto& [key, device] : recordingButtonControllers) {
					this->recordedCombo.push_back(ButtonCombo(device, key));
				}
			}

			// Handle ENTER key to accept combo
			if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter)) || ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_KeypadEnter))) {
				if (!this->recordedCombo.empty()) {
					// Apply the recorded combo to the correct settings vector
					switch (this->currentComboType) {
					case VR::ComboType::MenuOpen:
						settings.VRMenuOpenKeys = this->recordedCombo;
						break;
					case VR::ComboType::MenuClose:
						settings.VRMenuCloseKeys = this->recordedCombo;
						break;
					case VR::ComboType::OverlayOpen:
						settings.VROverlayOpenKeys = this->recordedCombo;
						break;
					case VR::ComboType::OverlayClose:
						settings.VROverlayCloseKeys = this->recordedCombo;
						break;
					default:
						break;
					}
				}

				// Reset recording state
				this->isCapturingCombo = false;
				this->currentComboType = VR::ComboType::None;
				this->currentComboName = nullptr;
				this->recordedCombo.clear();
				this->comboStartTime = 0.0;
				recordingButtonControllers.clear();
				ImGui::CloseCurrentPopup();
			}

			// Handle ESC key to cancel
			if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape))) {
				// Reset recording state
				this->isCapturingCombo = false;
				this->currentComboType = VR::ComboType::None;
				this->currentComboName = nullptr;
				this->recordedCombo.clear();
				this->comboStartTime = 0.0;
				recordingButtonControllers.clear();
				ImGui::CloseCurrentPopup();
			}

			// Handle timeout - auto-accept if buttons were pressed, auto-cancel if not
			if (remainingTime <= 0.0) {
				if (!this->recordedCombo.empty()) {
					// Auto-accept if buttons were pressed - apply to correct settings vector
					switch (this->currentComboType) {
					case VR::ComboType::MenuOpen:
						settings.VRMenuOpenKeys = this->recordedCombo;
						break;
					case VR::ComboType::MenuClose:
						settings.VRMenuCloseKeys = this->recordedCombo;
						break;
					case VR::ComboType::OverlayOpen:
						settings.VROverlayOpenKeys = this->recordedCombo;
						break;
					case VR::ComboType::OverlayClose:
						settings.VROverlayCloseKeys = this->recordedCombo;
						break;
					default:
						break;
					}
				}
				// Auto-cancel if no buttons were pressed (do nothing, just close)

				// Reset recording state
				this->isCapturingCombo = false;
				this->currentComboType = VR::ComboType::None;
				this->currentComboName = nullptr;
				this->recordedCombo.clear();
				this->comboStartTime = 0.0;
				recordingButtonControllers.clear();
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
	}
}

namespace
{
	void DrawControllerInputInstructions(VR::Settings& settings)
	{
		if (ImGui::CollapsingHeader("Controller Input Instructions", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Checkbox("Show 'How to Use' Message", &settings.ShowHowToUseMessage);
			ImGui::TextWrapped("Menu:");
			ImGui::BulletText("Open Community Shaders Menu: Hold both configured buttons (Primary Controller) while in the main menu or tween menu");
			ImGui::BulletText("Close: Hold the configured buttons on both controllers at the same time");
			ImGui::TextWrapped("Overlay:");
			ImGui::BulletText("Open Overlay: Primary Controller configured button while in the main menu or tween menu");
			ImGui::BulletText("Close Overlay: Secondary Controller configured button while in the main menu or tween menu");
			ImGui::Spacing();
			ImGui::TextWrapped("Controller Input:");
			ImGui::BulletText("Trigger (Both Controllers): Left mouse button");
			ImGui::BulletText("Grip (Both Controllers): Right mouse button");
			ImGui::BulletText("Touchpad Click (Both Controllers): Middle mouse button");
			ImGui::BulletText("Stick Click (Both Controllers): Middle mouse button");
			ImGui::BulletText("A/X (Both Controllers): Enter");
			ImGui::BulletText("B/Y (Primary Controller): Tab");
			ImGui::BulletText("B/Y (Secondary Controller): Shift+Tab");

			// Show dynamic controller assignments based on attach mode
			bool useAttachedControllerForCursor = (settings.attachMode == VR::Settings::OverlayAttachMode::ControllerOnly ||
												   settings.attachMode == VR::Settings::OverlayAttachMode::Both);

			if (useAttachedControllerForCursor) {
				if (settings.VRMenuAttachController == ControllerDevice::Primary) {
					ImGui::BulletText("Primary Controller Thumbstick: Mouse movement (attached controller)");
					ImGui::BulletText("Secondary Controller Thumbstick: Scroll");
				} else {
					ImGui::BulletText("Primary Controller Thumbstick: Scroll");
					ImGui::BulletText("Secondary Controller Thumbstick: Mouse movement (attached controller)");
				}
			} else {
				ImGui::BulletText("Primary Controller Thumbstick: Mouse movement (HMD mode)");
				ImGui::BulletText("Secondary Controller Thumbstick: Scroll");
			}
		}
	}
	void DrawGeneralVRSettings(VR::Settings& settings)
	{
		if (ImGui::CollapsingHeader("General Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Checkbox("Enable Depth Buffer Culling", &settings.EnableDepthBufferCulling);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Enables depth buffer culling for VR performance optimization.");
			}
			ImGui::SliderFloat("Min Occludee Box Extent", &settings.MinOccludeeBoxExtent, 0.0f, 1000.0f, "%.1f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Minimum box extent for occlusion culling in VR.");
			}
		}
	}
	void DrawMenuSettings(VR::Settings& settings)
	{
		if (ImGui::CollapsingHeader("Menu Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::SliderFloat("Menu Scale", &settings.VRMenuScale, 0.5f, 2.0f, "%.2f");
			const char* positioningMethods[] = { "HMD Relative", "Fixed World Position" };
			ImGui::Combo("Menu Positioning Method", &settings.VRMenuPositioningMethod, positioningMethods, IM_ARRAYSIZE(positioningMethods));
			const char* attachModes[] = { "HMD Only", "Controller Only", "Both" };
			int attachModeInt = static_cast<int>(settings.attachMode);
			if (ImGui::Combo("Attach Mode", &attachModeInt, attachModes, IM_ARRAYSIZE(attachModes))) {
				settings.attachMode = static_cast<VR::Settings::OverlayAttachMode>(attachModeInt);
			}

			// Controller-specific settings (only show when controller mode is active)
			if (settings.attachMode == VR::Settings::OverlayAttachMode::ControllerOnly ||
				settings.attachMode == VR::Settings::OverlayAttachMode::Both) {
				const char* attachControllers[] = { "Primary Controller", "Secondary Controller" };
				int attachControllerInt = static_cast<int>(settings.VRMenuAttachController);
				if (ImGui::Combo("Attach to Controller", &attachControllerInt, attachControllers, IM_ARRAYSIZE(attachControllers))) {
					settings.VRMenuAttachController = static_cast<ControllerDevice>(attachControllerInt);
				}

				ImGui::Separator();
				ImGui::Text("Controller Offset Settings");
				ImGui::SliderFloat("Controller Offset X", &settings.VRMenuControllerOffsetX, -2.0f, 2.0f, "%.2f");
				ImGui::SliderFloat("Controller Offset Y", &settings.VRMenuControllerOffsetY, -2.0f, 2.0f, "%.2f");
				ImGui::SliderFloat("Controller Offset Z", &settings.VRMenuControllerOffsetZ, -2.0f, 2.0f, "%.2f");
			}

			// HMD-specific settings (only show when HMD mode is active)
			if (settings.attachMode == VR::Settings::OverlayAttachMode::HMDOnly ||
				settings.attachMode == VR::Settings::OverlayAttachMode::Both) {
				ImGui::Separator();
				ImGui::Text("HMD Offset Settings");
				ImGui::SliderFloat("HMD Offset X", &settings.VRMenuOffsetX, -2.0f, 2.0f, "%.2f");
				ImGui::SliderFloat("HMD Offset Y", &settings.VRMenuOffsetY, -2.0f, 2.0f, "%.2f");
				ImGui::SliderFloat("HMD Offset Z", &settings.VRMenuOffsetZ, -2.0f, 2.0f, "%.2f");
			}
		}
	}
	void DrawMouseSettings(VR::Settings& settings)
	{
		if (ImGui::CollapsingHeader("Mouse Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::SliderFloat("Mouse Deadzone", &settings.mouseDeadzone, 0.0f, 0.5f, "%.2f");
			ImGui::SliderFloat("Mouse Speed", &settings.mouseSpeed, 0.1f, 20.0f, "%.2f");
		}
	}
	void DrawDragSettings(VR::Settings& settings)
	{
		if (ImGui::CollapsingHeader("Drag Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (ImGui::CollapsingHeader("Drag Instructions", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::TextWrapped("Overlay Positioning (Grip + Drag):");
				ImGui::BulletText("Fixed World Position: Any controller can drag (HMD-only mode) or attached controller only (Both modes)");
				ImGui::BulletText("HMD Relative: Any controller can drag (HMD-only mode) or attached controller only (Both modes)");
				ImGui::BulletText("Controller Attached: Only the opposite hand can drag the controller overlay");
			}
			ImGui::Checkbox("Enable drag to reposition overlays", &settings.EnableDragToReposition);
			ImGui::BeginDisabled(!settings.EnableDragToReposition);
			ImGui::ColorEdit4("Drag Highlight Color", settings.dragHighlightColor.data());
			ImGui::EndDisabled();
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Color used to highlight draggable overlays in VR.");
			}
		}
	}
	void DrawKeyBindings(VR* vr)
	{
		VR::Settings& settings = vr->settings;
		static std::unordered_map<uint32_t, ControllerDevice> recordingButtonControllers;
		// Combo Settings
		if (ImGui::CollapsingHeader("Combo Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::SliderFloat("Combo Timeout", &settings.comboTimeout, 1.0f, 10.0f, "%.1f seconds");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Time limit for recording button combinations.");
			}
		}
		ImGui::Separator();
		// Combo box for selecting which combo to record
		const char* comboTypes[] = {
			"Open Community Shaders Menu",
			"Close Community Shaders Menu",
			"Open VR Overlay",
			"Close VR Overlay"
		};
		static int selectedComboIndex = 0;
		ImGui::Text("Select Combo to Record:");
		ImGui::SameLine();
		if (ImGui::Combo("##ComboSelector", &selectedComboIndex, comboTypes, IM_ARRAYSIZE(comboTypes))) {
			// Reset recording state when changing selection
			vr->isCapturingCombo = false;
			vr->currentComboType = VR::ComboType::None;
			vr->recordedCombo.clear();
		}
		if (ImGui::Button("Record Selected Combo")) {
			// Start recording the selected combo
			vr->isCapturingCombo = true;
			vr->currentComboType = static_cast<VR::ComboType>(selectedComboIndex + 1);
			vr->currentComboName = comboTypes[selectedComboIndex];
			vr->recordedCombo.clear();
			vr->comboStartTime = Util::GetNowSecs();
			recordingButtonControllers.clear();
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Clear")) {
			// Clear the selected combo
			switch (selectedComboIndex) {
			case 0:
				settings.VRMenuOpenKeys.clear();
				break;
			case 1:
				settings.VRMenuCloseKeys.clear();
				break;
			case 2:
				settings.VROverlayOpenKeys.clear();
				break;
			case 3:
				settings.VROverlayCloseKeys.clear();
				break;
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Click to start recording a new button combination for the selected action.");
		}
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		// Table for displaying current key bindings
		if (ImGui::BeginTable("##VRBindingsTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn("Action");
			ImGui::TableSetupColumn("Current Binding");
			ImGui::TableSetupColumn("Description");
			ImGui::TableHeadersRow();
			// Define VR key binding configurations
			struct VRKeyBindingConfig
			{
				const char* label;
				std::vector<ButtonCombo>& combos;
				const char* description;
				const char* controllerRequirement;
			};
			std::vector<VRKeyBindingConfig> keyBindingConfigs = {
				{ "Open Community Shaders Menu", settings.VRMenuOpenKeys, "Button combination to open the Community Shaders menu", "Primary" },
				{ "Close Community Shaders Menu", settings.VRMenuCloseKeys, "Button combination to close the Community Shaders menu", "Both" },
				{ "Open VR Overlay", settings.VROverlayOpenKeys, "Button combination to open the VR overlay", "Primary" },
				{ "Close VR Overlay", settings.VROverlayCloseKeys, "Button combination to close the VR overlay", "Secondary" }
			};
			for (size_t row = 0; row < keyBindingConfigs.size(); ++row) {
				const auto& config = keyBindingConfigs[row];
				ImGui::TableNextRow();
				// Highlight the selected row
				if (row == static_cast<size_t>(selectedComboIndex)) {
					ImU32 highlight = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 0.0f, 0.15f));
					ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, highlight);
				}
				// Make row selectable
				ImGui::TableSetColumnIndex(0);
				char selectableId[64];
				snprintf(selectableId, sizeof(selectableId), "##combo_row_%zu", row);
				bool rowSelected = (row == static_cast<size_t>(selectedComboIndex));
				if (ImGui::Selectable(selectableId, rowSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap, ImVec2(0, 0))) {
					selectedComboIndex = static_cast<int>(row);
				}
				ImGui::SameLine(0, 0);
				ImGui::Text("%s", config.label);
				// Current Binding column
				ImGui::TableSetColumnIndex(1);
				Util::DrawButtonCombo(config.combos, false);
				// Description column
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%s", config.description);
			}
			ImGui::EndTable();
		}
		ImGui::Spacing();
		// Reset to defaults button
		if (ImGui::Button("Reset to Defaults")) {
			// Use ButtonCombo structure for cleaner defaults
			settings.VRMenuOpenKeys = {
				ButtonCombo::Primary(static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kXA)),
				ButtonCombo::Primary(static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kBY))
			};
			settings.VRMenuCloseKeys = {
				ButtonCombo::Both(static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kGrip))
			};
			settings.VROverlayOpenKeys = {
				ButtonCombo::Primary(static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kJoystickTrigger))
			};
			settings.VROverlayCloseKeys = {
				ButtonCombo::Secondary(static_cast<uint32_t>(RE::BSOpenVRControllerDevice::Keys::kJoystickTrigger))
			};
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Reset all VR key bindings to their default values.");
		}
	}
	void DrawDebugSection(VR* vr)
	{
		VR::Settings& settings = vr->settings;
		auto menu = globals::menu;
		// Controller Diagnostics Section
		if (ImGui::CollapsingHeader("Controller Diagnostics", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (ImGui::Checkbox("Test Mode: Disable controller menu input (except scroll controller and triggers)", &settings.VRMenuControllerDiagnosticsTestMode)) {
				ImGui::SetScrollHereY(0.0f);  // Scroll to top of the window when toggled
			}
			ImGui::SeparatorText("Button State");
			double nowSecs = Util::GetNowSecs();
			// Get highlight color from theme
			ImVec4 highlightColor = menu->GetTheme().StatusPalette.InfoColor;
			ImU32 highlightColorU32 = ImGui::ColorConvertFloat4ToU32(highlightColor);

			// Determine display order based on handedness
			bool isLeftHanded = RE::BSOpenVRControllerDevice::IsLeftHandedMode();

			if (ImGui::BeginTable("vr_input_state_table", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
				ImGui::TableSetupColumn("Button");
				if (isLeftHanded) {
					// Left-handed: Primary (left hand) on left, Secondary (right hand) on right
					ImGui::TableSetupColumn("Primary State");
					ImGui::TableSetupColumn("Primary Held (s)");
					ImGui::TableSetupColumn("Primary Type");
					ImGui::TableSetupColumn("Secondary State");
					ImGui::TableSetupColumn("Secondary Held (s)");
					ImGui::TableSetupColumn("Secondary Type");
				} else {
					// Right-handed: Secondary (left hand) on left, Primary (right hand) on right
					ImGui::TableSetupColumn("Secondary State");
					ImGui::TableSetupColumn("Secondary Held (s)");
					ImGui::TableSetupColumn("Secondary Type");
					ImGui::TableSetupColumn("Primary State");
					ImGui::TableSetupColumn("Primary Held (s)");
					ImGui::TableSetupColumn("Primary Type");
				}
				ImGui::TableHeadersRow();
				// Helper for button type text
				auto DrawButtonType = [](const RE::ButtonState& state) {
					if (!state.isPressed) {
						if (state.IsClick())
							ImGui::TextUnformatted("Click");
						else if (state.IsHold())
							ImGui::TextUnformatted("Hold");
						else
							ImGui::TextUnformatted("-");
					} else {
						ImGui::TextUnformatted("Held");
					}
				};
				// Helper for printing a row with left/right cell highlight
				auto printRow = [&](const char* label, const RE::ButtonState& left, const RE::ButtonState& right) {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(label);
					ImGui::TableSetColumnIndex(1);
					if (left.isPressed)
						ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, highlightColorU32);
					ImGui::TextUnformatted(left.isPressed ? "Pressed" : "Released");
					ImGui::TableSetColumnIndex(2);
					if (left.isPressed)
						ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, highlightColorU32);
					ImGui::Text("%.2f", left.GetCurrentHeldTime(nowSecs));
					ImGui::TableSetColumnIndex(3);
					if (left.isPressed)
						ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, highlightColorU32);
					DrawButtonType(left);
					ImGui::TableSetColumnIndex(4);
					if (right.isPressed)
						ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, highlightColorU32);
					ImGui::TextUnformatted(right.isPressed ? "Pressed" : "Released");
					ImGui::TableSetColumnIndex(5);
					if (right.isPressed)
						ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, highlightColorU32);
					ImGui::Text("%.2f", right.GetCurrentHeldTime(nowSecs));
					ImGui::TableSetColumnIndex(6);
					if (right.isPressed)
						ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, highlightColorU32);
					DrawButtonType(right);
				};

				// Helper to determine the correct order for display based on handedness
				auto printRowWithHandedness = [&](const char* label, auto key) {
					auto& primary = vr->primaryControllerState[key];
					auto& secondary = vr->secondaryControllerState[key];
					if (isLeftHanded) {
						// Left-handed: Primary (left hand) on left, Secondary (right hand) on right
						printRow(label, primary, secondary);
					} else {
						// Right-handed: Secondary (left hand) on left, Primary (right hand) on right
						printRow(label, secondary, primary);
					}
				};

				printRowWithHandedness("Trigger", RE::BSOpenVRControllerDevice::Keys::kTrigger);
				printRowWithHandedness("Grip", RE::BSOpenVRControllerDevice::Keys::kGrip);
				printRowWithHandedness("GripAlt", RE::BSOpenVRControllerDevice::Keys::kGripAlt);
				printRowWithHandedness("Stick Click", RE::BSOpenVRControllerDevice::Keys::kJoystickTrigger);
				printRowWithHandedness("Touchpad Click", RE::BSOpenVRControllerDevice::Keys::kTouchpadClick);
				printRowWithHandedness("Touchpad Alt", RE::BSOpenVRControllerDevice::Keys::kTouchpadAlt);
				printRowWithHandedness("B/Y", RE::BSOpenVRControllerDevice::Keys::kBY);
				printRowWithHandedness("A/X", RE::BSOpenVRControllerDevice::Keys::kXA);
				ImGui::EndTable();
			}
			ImGui::SeparatorText("VR Thumbstick State");
			// Helper to draw a thumbstick quadrant visualization (returns ImVec2 for label alignment)
			auto DrawThumbstickPad = [&](float x, float y, ImU32 highlightCol) -> ImVec2 {
				ImVec2 padSize = ImVec2(80, 80);
				ImVec2 cursor = ImGui::GetCursorScreenPos();
				ImDrawList* drawList = ImGui::GetWindowDrawList();
				ImVec2 center = ImVec2(cursor.x + padSize.x / 2, cursor.y + padSize.y / 2);
				float radius = padSize.x / 2 - 4;
				ImU32 borderCol = ImGui::GetColorU32(ImGuiCol_Border);
				ImU32 axisCol = ImGui::GetColorU32(ImGuiCol_TextDisabled);
				ImU32 dotCol = ImGui::GetColorU32(ImGuiCol_Text);
				// Draw background
				drawList->AddRectFilled(cursor, ImVec2(cursor.x + padSize.x, cursor.y + padSize.y), ImGui::GetColorU32(ImGuiCol_FrameBg));
				// Draw border
				drawList->AddRect(cursor, ImVec2(cursor.x + padSize.x, cursor.y + padSize.y), borderCol, 4.0f, 0, 2.0f);
				// Draw axes
				drawList->AddLine(ImVec2(center.x, cursor.y + 4), ImVec2(center.x, cursor.y + padSize.y - 4), axisCol, 1.0f);
				drawList->AddLine(ImVec2(cursor.x + 4, center.y), ImVec2(cursor.x + padSize.x - 4, center.y), axisCol, 1.0f);
				// Determine quadrant
				int quad = 0;
				if (x > 0 && y > 0)
					quad = 1;  // top-right
				else if (x < 0 && y > 0)
					quad = 2;  // top-left
				else if (x < 0 && y < 0)
					quad = 3;  // bottom-left
				else if (x > 0 && y < 0)
					quad = 4;  // bottom-right
				// Highlight quadrant
				if (quad != 0) {
					ImVec2 q0 = center;
					ImVec2 q1 = center;
					ImVec2 q2 = center;
					ImVec2 q3 = center;
					if (quad == 1) {  // top-right
						q1.x += radius;
						q1.y -= radius;
						q2.x += radius;
						q2.y += 0;
						q3.x += 0;
						q3.y -= radius;
					} else if (quad == 2) {  // top-left
						q1.x -= radius;
						q1.y -= radius;
						q2.x -= radius;
						q2.y += 0;
						q3.x += 0;
						q3.y -= radius;
					} else if (quad == 3) {  // bottom-left
						q1.x -= radius;
						q1.y += radius;
						q2.x -= radius;
						q2.y += 0;
						q3.x += 0;
						q3.y += radius;
					} else if (quad == 4) {  // bottom-right
						q1.x += radius;
						q1.y += radius;
						q2.x += radius;
						q2.y += 0;
						q3.x += 0;
						q3.y += radius;
					}
					ImVec2 poly[4] = { center, q1, q2, q3 };
					drawList->AddConvexPolyFilled(poly, 4, highlightCol);
				}
				// Draw stick position dot
				ImVec2 dot = ImVec2(center.x + x * radius, center.y - y * radius);
				drawList->AddCircleFilled(dot, 5.0f, dotCol);
				// Return size for label alignment
				return padSize;
			};
			ImU32 highlightCol = ImGui::ColorConvertFloat4ToU32(menu->GetTheme().StatusPalette.InfoColor);
			if (ImGui::BeginTable("##VRThumbstickTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
				if (isLeftHanded) {
					// Left-handed: Primary (left hand) on left, Secondary (right hand) on right
					ImGui::TableSetupColumn("Primary Controller", ImGuiTableColumnFlags_WidthFixed, 200.0f);
					ImGui::TableSetupColumn("Secondary Controller", ImGuiTableColumnFlags_WidthFixed, 200.0f);
				} else {
					// Right-handed: Secondary (left hand) on left, Primary (right hand) on right
					ImGui::TableSetupColumn("Secondary Controller", ImGuiTableColumnFlags_WidthFixed, 200.0f);
					ImGui::TableSetupColumn("Primary Controller", ImGuiTableColumnFlags_WidthFixed, 200.0f);
				}
				ImGui::TableHeadersRow();

				// Left column content
				ImGui::TableSetColumnIndex(0);
				ImGui::BeginGroup();
				if (isLeftHanded) {
					// Left-handed: Show primary controller in left column
					ImVec2 padSizeL = DrawThumbstickPad(vr->primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x, vr->primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y, highlightCol);
					ImGui::Dummy(padSizeL);
					ImGui::SetNextItemWidth(160.0f);
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight());
					ImGui::Text("X: %+1.3f  Y: %+1.3f  [%s]", vr->primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x, vr->primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y, RE::GetQuadrantName(vr->primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x, vr->primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y));
				} else {
					// Right-handed: Show secondary controller in left column
					ImVec2 padSizeL = DrawThumbstickPad(vr->secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].x, vr->secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].y, highlightCol);
					ImGui::Dummy(padSizeL);
					ImGui::SetNextItemWidth(160.0f);
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight());
					ImGui::Text("X: %+1.3f  Y: %+1.3f  [%s]", vr->secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].x, vr->secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].y, RE::GetQuadrantName(vr->secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].x, vr->secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].y));
				}
				ImGui::EndGroup();

				// Right column content
				ImGui::TableSetColumnIndex(1);
				ImGui::BeginGroup();
				if (isLeftHanded) {
					// Left-handed: Show secondary controller in right column
					ImVec2 padSizeR = DrawThumbstickPad(vr->secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].x, vr->secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].y, highlightCol);
					ImGui::Dummy(padSizeR);
					ImGui::SetNextItemWidth(160.0f);
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight());
					ImGui::Text("X: %+1.3f  Y: %+1.3f  [%s]", vr->secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].x, vr->secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].y, RE::GetQuadrantName(vr->secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].x, vr->secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].y));
				} else {
					// Right-handed: Show primary controller in right column
					ImVec2 padSizeR = DrawThumbstickPad(vr->primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x, vr->primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y, highlightCol);
					ImGui::Dummy(padSizeR);
					ImGui::SetNextItemWidth(160.0f);
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight());
					ImGui::Text("X: %+1.3f  Y: %+1.3f  [%s]", vr->primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x, vr->primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y, RE::GetQuadrantName(vr->primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x, vr->primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y));
				}
				ImGui::EndGroup();
				ImGui::EndTable();
			}
			ImGui::SeparatorText("Recent VR Controller Events");
			ImGui::TextDisabled("Note: For thumbstick events, KeyCode/Value columns show X/Y floats.");
			if (ImGui::BeginTable("eventlog", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
				ImGui::TableSetupColumn("Device", ImGuiTableColumnFlags_WidthFixed, 60.0f);
				ImGui::TableSetupColumn("KeyCode/X", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Value/Y", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Pressed", ImGuiTableColumnFlags_WidthFixed, 70.0f);
				ImGui::TableSetupColumn("Known Mapping", ImGuiTableColumnFlags_WidthFixed, 120.0f);
				ImGui::TableSetupColumn("Event Type", ImGuiTableColumnFlags_WidthFixed, 120.0f);
				ImGui::TableHeadersRow();
				for (const auto& e : vr->vrControllerEventLog) {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("%d", e.device);
					ImGui::TableSetColumnIndex(1);
					if (e.heldSource == "thumbstick") {
						ImGui::Text("%.3f", e.thumbstickX);
					} else {
						ImGui::Text("%d", e.keyCode);
					}
					ImGui::TableSetColumnIndex(2);
					if (e.heldSource == "thumbstick") {
						ImGui::Text("%.3f", e.thumbstickY);
					} else {
						ImGui::Text("%d", e.value);
					}
					ImGui::TableSetColumnIndex(3);
					ImGui::Text("%s", e.pressed ? "Pressed" : "Released");
					ImGui::TableSetColumnIndex(4);
					if (e.heldSource == "thumbstick") {
						ImGui::TextUnformatted(e.controllerRole.c_str());
					} else {
						ImGui::TextUnformatted(RE::GetOpenVRButtonName(e.keyCode));
					}
					ImGui::TableSetColumnIndex(5);
					if (e.heldSource == "thumbstick") {
						ImGui::TextUnformatted("-");
					} else {
						// Show click/hold for release events if available
						if (!e.pressed) {
							if (e.heldTime > 0.0) {
								if (e.heldTime < 0.5) {
									ImGui::Text("Click (%.2fs)", e.heldTime);
								} else {
									ImGui::Text("Hold (%.2fs)", e.heldTime);
								}
							} else {
								ImGui::Text("Release");
							}
						} else if (e.pressed) {
							if (e.heldTime > 0.0) {
								ImGui::Text("Held for %.2fs", e.heldTime);
							} else {
								ImGui::Text("Press");
							}
						}
					}
				}
				ImGui::EndTable();
			}
		}
	}
}  // namespace

void VR::LoadSettings(json& o_json)
{
	settings = o_json.get<Settings>();
}

void VR::SaveSettings(json& o_json)
{
	o_json = settings;
}

void VR::RestoreDefaultSettings()
{
	settings = {};
}

void VR::PostPostLoad()
{
	gDepthBufferCulling = reinterpret_cast<bool*>(REL::Offset(0x1EC6B88).address());
	gMinOccludeeBoxExtent = reinterpret_cast<float*>(REL::Offset(0x1ED64E8).address());

	// Patches BSGeometry::CopyTransformAndBounds to copy the model-bound translation across correctly instead of overwriting it with the bounding sphere centre
	REL::safe_write(REL::RelocationID(0, 0, 69528).address() + REL::Relocate(0, 0, 0xD9) + 0x2, 0x148);
	REL::safe_write(REL::RelocationID(0, 0, 69528).address() + REL::Relocate(0, 0, 0xE5) + 0x2, 0x14C);
	REL::safe_write(REL::RelocationID(0, 0, 69528).address() + REL::Relocate(0, 0, 0xF1) + 0x2, 0x150);
}

void VR::DataLoaded()
{
	*gDepthBufferCulling = settings.EnableDepthBufferCulling;
	*gMinOccludeeBoxExtent = settings.MinOccludeeBoxExtent;
}

void SetOverlayInputFlags(vr::IVROverlay* overlay, vr::VROverlayHandle_t handle)
{
	overlay->SetOverlayFlag(handle, vr::VROverlayFlags_SendVRScrollEvents, true);
	overlay->SetOverlayFlag(handle, vr::VROverlayFlags_SendVRTouchpadEvents, true);
	overlay->SetOverlayFlag(handle, vr::VROverlayFlags_AcceptsGamepadEvents, true);
	overlay->SetOverlayFlag(handle, vr::VROverlayFlags_VisibleInDashboard, true);
}

void VR::UpdateVROverlayPosition()
{
	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	auto* system = openvr ? openvr->vrSystem : nullptr;
	if (!system)
		return;

	if (menuOverlayHandle == vr::k_ulOverlayHandleInvalid) {
		return;
	}

	auto* overlay = openvr ? RE::BSOpenVR::GetIVROverlayFromContext(&openvr->vrContext) : nullptr;
	if (!overlay)
		return;

	// Determine positioning strategy based on settings
	bool showOnController = (settings.attachMode == AttachMode::ControllerOnly || settings.attachMode == AttachMode::Both);
	bool showOnHMD = (settings.attachMode == AttachMode::HMDOnly || settings.attachMode == AttachMode::Both);

	// Texture size
	float aspect = static_cast<float>(kOverlayHeight) / kOverlayWidth;
	float baseWidth = 1.0f;
	float overlayWidth = baseWidth * settings.VRMenuScale;
	float overlayHeight = overlayWidth * aspect;
	float offsetX = settings.VRMenuOffsetX;
	float offsetY = settings.VRMenuOffsetY;
	float offsetZ = settings.VRMenuOffsetZ;

	static int lastPositioningMethod = -1;
	bool justSwitchedToFixed = (lastPositioningMethod != 1 && settings.VRMenuPositioningMethod == 1);
	lastPositioningMethod = settings.VRMenuPositioningMethod;

	// Handle HMD positioning
	if (showOnHMD) {
		if (settings.VRMenuPositioningMethod == 0) {
			// HMD Relative positioning
			vr::TrackedDevicePose_t hmdPose;
			system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0, &hmdPose, 1);

			if (hmdPose.bPoseIsValid) {
				// Calculate position in front of HMD using offsets directly
				float height = 0.0f;

				// Create transform matrix - start with identity
				vr::HmdMatrix34_t hmdTransform;
				hmdTransform.m[0][0] = 1.0f;
				hmdTransform.m[0][1] = 0.0f;
				hmdTransform.m[0][2] = 0.0f;
				hmdTransform.m[0][3] = 0.0f;
				hmdTransform.m[1][0] = 0.0f;
				hmdTransform.m[1][1] = 1.0f;
				hmdTransform.m[1][2] = 0.0f;
				hmdTransform.m[1][3] = 0.0f;
				hmdTransform.m[2][0] = 0.0f;
				hmdTransform.m[2][1] = 0.0f;
				hmdTransform.m[2][2] = 1.0f;
				hmdTransform.m[2][3] = 0.0f;

				// Copy HMD position
				hmdTransform.m[0][3] = hmdPose.mDeviceToAbsoluteTracking.m[0][3];
				hmdTransform.m[1][3] = hmdPose.mDeviceToAbsoluteTracking.m[1][3];
				hmdTransform.m[2][3] = hmdPose.mDeviceToAbsoluteTracking.m[2][3];

				// Copy HMD orientation
				hmdTransform.m[0][0] = hmdPose.mDeviceToAbsoluteTracking.m[0][0];
				hmdTransform.m[0][1] = hmdPose.mDeviceToAbsoluteTracking.m[0][1];
				hmdTransform.m[0][2] = hmdPose.mDeviceToAbsoluteTracking.m[0][2];
				hmdTransform.m[1][0] = hmdPose.mDeviceToAbsoluteTracking.m[1][0];
				hmdTransform.m[1][1] = hmdPose.mDeviceToAbsoluteTracking.m[1][1];
				hmdTransform.m[1][2] = hmdPose.mDeviceToAbsoluteTracking.m[1][2];
				hmdTransform.m[2][0] = hmdPose.mDeviceToAbsoluteTracking.m[2][0];
				hmdTransform.m[2][1] = hmdPose.mDeviceToAbsoluteTracking.m[2][1];
				hmdTransform.m[2][2] = hmdPose.mDeviceToAbsoluteTracking.m[2][2];

				// Apply HMD offset positions directly (in HMD local space)
				hmdTransform.m[0][3] += hmdTransform.m[0][0] * offsetX + hmdTransform.m[0][1] * offsetY + hmdTransform.m[0][2] * offsetZ;
				hmdTransform.m[1][3] += hmdTransform.m[1][0] * offsetX + hmdTransform.m[1][1] * offsetY + hmdTransform.m[1][2] * offsetZ;
				hmdTransform.m[2][3] += hmdTransform.m[2][0] * offsetX + hmdTransform.m[2][1] * offsetY + hmdTransform.m[2][2] * offsetZ;

				// Move up by height (Y axis in HMD space)
				hmdTransform.m[0][3] += hmdTransform.m[0][1] * height;
				hmdTransform.m[1][3] += hmdTransform.m[1][1] * height;
				hmdTransform.m[2][3] += hmdTransform.m[2][1] * height;

				// Scale the overlay based on width/height
				hmdTransform.m[0][0] *= overlayWidth;
				hmdTransform.m[1][1] *= overlayHeight;

				SetOverlayInputFlags(overlay, menuOverlayHandle);
				overlay->SetOverlayTransformAbsolute(menuOverlayHandle, vr::TrackingUniverseStanding, &hmdTransform);
				overlay->SetOverlayWidthInMeters(menuOverlayHandle, baseWidth * settings.VRMenuScale);

			} else {
				logger::debug("HMD pose invalid, falling back to fixed positioning");
				settings.VRMenuPositioningMethod = 1;  // Fall back to fixed positioning
			}
		}

		if (settings.VRMenuPositioningMethod == 1) {
			// Fixed World Position
			logger::debug("Using fixed world positioning");
			if (justSwitchedToFixed) {
				SetFixedOverlayToCurrentHMD();
			}

			vr::HmdMatrix34_t fixedTransform = Util::MatrixToHmdMatrix34(fixedWorldOverlayPosition.m);

			// Scale the overlay based on width/height (same as relative HMD mode)
			fixedTransform.m[0][0] *= overlayWidth;
			fixedTransform.m[1][1] *= overlayHeight;

			SetOverlayInputFlags(overlay, menuOverlayHandle);
			overlay->SetOverlayTransformAbsolute(menuOverlayHandle, vr::TrackingUniverseStanding, &fixedTransform);
			overlay->SetOverlayWidthInMeters(menuOverlayHandle, baseWidth * settings.VRMenuScale);
		}
	}

	// Handle controller positioning separately (can be shown alongside HMD)
	if (showOnController) {
		// Get the VR controller overlay handle from Menu.cpp
		if (menuControllerOverlayHandle == vr::k_ulOverlayHandleInvalid) {
			return;
		}

		// Attach to controller
		vr::TrackedDeviceIndex_t controllerIndex = GetControllerIndexForDevice(settings.VRMenuAttachController);

		if (controllerIndex != vr::k_unTrackedDeviceIndexInvalid) {
			// Position relative to controller using offset settings
			vr::HmdMatrix34_t transform;
			transform.m[0][0] = overlayWidth;
			transform.m[0][1] = 0.0f;
			transform.m[0][2] = 0.0f;
			transform.m[0][3] = settings.VRMenuControllerOffsetX;
			transform.m[1][0] = 0.0f;
			transform.m[1][1] = overlayHeight;
			transform.m[1][2] = 0.0f;
			transform.m[1][3] = settings.VRMenuControllerOffsetY;
			transform.m[2][0] = 0.0f;
			transform.m[2][1] = 0.0f;
			transform.m[2][2] = 1.0f;
			transform.m[2][3] = settings.VRMenuControllerOffsetZ;

			SetOverlayInputFlags(overlay, menuControllerOverlayHandle);
			overlay->SetOverlayTransformTrackedDeviceRelative(menuControllerOverlayHandle, controllerIndex, &transform);

			// Update the overlay width to match the calculated size
			overlay->SetOverlayWidthInMeters(menuControllerOverlayHandle, overlayWidth);

			// Update controller overlay flags for input interaction
			SetOverlayInputFlags(overlay, menuControllerOverlayHandle);
		}
	}

	// Update overlay flags for input interaction
	SetOverlayInputFlags(overlay, menuOverlayHandle);
}

void VR::UpdateVROverlayControllerPosition()
{
	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	auto* system = openvr ? openvr->vrSystem : nullptr;
	if (!system)
		return;

	// Get the VR controller overlay handle from Menu.cpp
	if (menuControllerOverlayHandle == vr::k_ulOverlayHandleInvalid) {
		return;
	}

	auto* overlay = openvr ? RE::BSOpenVR::GetIVROverlayFromContext(&openvr->vrContext) : nullptr;
	if (!overlay)
		return;

	// Texture size based on preset
	float aspect = static_cast<float>(kOverlayHeight) / kOverlayWidth;
	float baseWidth = 1.0f;
	float overlayWidth = baseWidth * settings.VRMenuScale;
	float overlayHeight = overlayWidth * aspect;

	// Find the appropriate controller for the controller overlay
	vr::TrackedDeviceIndex_t controllerIndex = GetControllerIndexForDevice(settings.VRMenuAttachController);
	if (controllerIndex == vr::k_unTrackedDeviceIndexInvalid) {
		overlay->HideOverlay(menuControllerOverlayHandle);
		return;
	}

	// Position relative to controller using offset settings
	vr::HmdMatrix34_t transform;
	float offsetX = settings.VRMenuControllerOffsetX;
	float offsetY = settings.VRMenuControllerOffsetY;
	transform.m[0][0] = overlayWidth;
	transform.m[0][1] = 0.0f;
	transform.m[0][2] = 0.0f;
	transform.m[0][3] = offsetX;
	transform.m[1][0] = 0.0f;
	transform.m[1][1] = overlayHeight;
	transform.m[1][2] = 0.0f;
	transform.m[1][3] = offsetY;
	transform.m[2][0] = 0.0f;
	transform.m[2][1] = 0.0f;
	transform.m[2][2] = 1.0f;
	transform.m[2][3] = settings.VRMenuControllerOffsetZ;

	SetOverlayInputFlags(overlay, menuControllerOverlayHandle);
	overlay->SetOverlayTransformTrackedDeviceRelative(menuControllerOverlayHandle, controllerIndex, &transform);

	// Update the overlay width to match the calculated size
	overlay->SetOverlayWidthInMeters(menuControllerOverlayHandle, overlayWidth);

	// Update controller overlay flags for input interaction
	SetOverlayInputFlags(overlay, menuControllerOverlayHandle);
}

// Add overlay management methods for VR menu overlays
void VR::EnsureOverlayInitialized()
{
	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	logger::info("BSOpenVR: 0x{:X}", reinterpret_cast<uintptr_t>(openvr));
	if (!openvr) {
		logger::error("BSOpenVR::GetSingleton() returned nullptr");
		return;
	}
	auto* vrSystem = openvr->vrSystem;
	auto* overlay = openvr ? RE::BSOpenVR::GetIVROverlayFromContext(&openvr->vrContext) : nullptr;
	logger::info("openVR->vrSystem: 0x{:X}", reinterpret_cast<uintptr_t>(vrSystem));
	logger::info("openVR->vrContext: 0x{:X}", reinterpret_cast<uintptr_t>(&openvr->vrContext));
	logger::info("openVR->vrContext.vrOverlay: 0x{:X}", reinterpret_cast<uintptr_t>(openvr->vrContext.vrOverlay));
	logger::info("openVR->hmdDeviceType: {} ({})", static_cast<int>(openvr->hmdDeviceType), magic_enum::enum_name(openvr->hmdDeviceType));
	for (int i = 0; i < RE::BSVRInterface::Hand::kTotal; ++i) {
		logger::info("openVR->controllerNodes[{}]: 0x{:X}", i, reinterpret_cast<uintptr_t>(openvr->controllerNodes[i].get()));
		if (openvr->controllerNodes[i] && reinterpret_cast<uintptr_t>(openvr->controllerNodes[i].get()) < 0x1000) {
			logger::warn("controllerNodes[{}] is suspiciously low (0x{:X})", i, reinterpret_cast<uintptr_t>(openvr->controllerNodes[i].get()));
		}
	}
	logger::info("menuOverlayHandle: 0x{:X}", menuOverlayHandle);
	logger::info("menuControllerOverlayHandle: 0x{:X}", menuControllerOverlayHandle);
	if (!overlay) {
		logger::error("IVROverlay is nullptr after GetIVROverlay");
		return;
	}
	Util::CreateOverlayTextureAndRTV(globals::d3d::device, kOverlayWidth, kOverlayHeight, &menuTexture, &menuRTV);
	std::string key = "communityshaders.menu";
	std::string name = "Community Shaders Menu";
	vr::EVROverlayError err = overlay->CreateOverlay(key.c_str(), name.c_str(), &menuOverlayHandle);
	if (err == vr::VROverlayError_None) {
		logger::info("CreateOverlay succeeded for menuOverlayHandle: 0x{:X}", menuOverlayHandle);
		SetOverlayInputFlags(overlay, menuOverlayHandle);
		overlay->SetOverlayWidthInMeters(menuOverlayHandle, 1.0f);
	} else {
		logger::error("CreateOverlay failed: {} ({})", static_cast<int>(err), magic_enum::enum_name(err));
	}
	// Controller overlay
	std::string controllerKey = "communityshaders.menu.controller";
	std::string controllerName = "Community Shaders Menu (Controller)";
	err = overlay->CreateOverlay(controllerKey.c_str(), controllerName.c_str(), &menuControllerOverlayHandle);
	if (err == vr::VROverlayError_None) {
		logger::info("CreateOverlay succeeded for menuControllerOverlayHandle: 0x{:X}", menuControllerOverlayHandle);
		Util::CreateOverlayTextureAndRTV(globals::d3d::device, kOverlayWidth, kOverlayHeight, &menuControllerTexture, &menuControllerRTV);
		SetOverlayInputFlags(overlay, menuControllerOverlayHandle);
		overlay->SetOverlayWidthInMeters(menuControllerOverlayHandle, 1.0f);
	} else {
		logger::error("CreateOverlay failed: {} ({})", static_cast<int>(err), magic_enum::enum_name(err));
	}
}

void VR::CleanupOverlayTextures()
{
	if (menuRTV) {
		menuRTV->Release();
		menuRTV = nullptr;
	}
	if (menuTexture) {
		menuTexture->Release();
		menuTexture = nullptr;
	}
	if (menuControllerRTV) {
		menuControllerRTV->Release();
		menuControllerRTV = nullptr;
	}
	if (menuControllerTexture) {
		menuControllerTexture->Release();
		menuControllerTexture = nullptr;
	}
}

void VR::DestroyOverlay()
{
	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	auto* overlay = openvr ? RE::BSOpenVR::GetIVROverlayFromContext(&openvr->vrContext) : nullptr;
	if (!overlay) {
		logger::error("DestroyOverlay: IVROverlay is nullptr");
		return;
	}
	if (menuOverlayHandle != vr::k_ulOverlayHandleInvalid) {
		overlay->DestroyOverlay(menuOverlayHandle);
		menuOverlayHandle = vr::k_ulOverlayHandleInvalid;
	}
	if (menuControllerOverlayHandle != vr::k_ulOverlayHandleInvalid) {
		overlay->DestroyOverlay(menuControllerOverlayHandle);
		menuControllerOverlayHandle = vr::k_ulOverlayHandleInvalid;
	}
	CleanupOverlayTextures();
}

void VR::RecreateOverlayTexturesIfNeeded()
{
	CleanupOverlayTextures();
	Util::CreateOverlayTextureAndRTV(globals::d3d::device, kOverlayWidth, kOverlayHeight, &menuTexture, &menuRTV);
	Util::CreateOverlayTextureAndRTV(globals::d3d::device, kOverlayWidth, kOverlayHeight, &menuControllerTexture, &menuControllerRTV);
}

void VR::SubmitOverlayFrame()
{
	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	auto* gameOverlay = openvr ? RE::BSOpenVR::GetIVROverlayFromContext(&openvr->vrContext) : nullptr;
	auto* cleanOverlay = RE::BSOpenVR::GetCleanIVROverlay();

	static bool cleanOverlayLogged = false;
	if (!cleanOverlayLogged) {
		if (cleanOverlay) {
			logger::info("VR: Successfully acquired clean IVROverlay interface via CommonLib: 0x{:X}", reinterpret_cast<uintptr_t>(cleanOverlay));
		} else {
			logger::error("VR: Failed to get clean IVROverlay interface via CommonLib");
		}
		cleanOverlayLogged = true;
	}

	if (!gameOverlay || !cleanOverlay) {
		return;
	}

	if (!openvr || !openvr->vrSystem) {
		logger::error("SubmitOverlayFrame: BSOpenVR or vrSystem is nullptr");
		return;
	}

	// Update drag logic for all modes
	UpdateOverlayDrag();
	auto& enabled = globals::menu->IsEnabled;
	auto& overlayVisible = globals::menu->overlayVisible;
	if ((enabled || overlayVisible || settings.ShowHowToUseMessage) && menuOverlayHandle != vr::k_ulOverlayHandleInvalid && menuTexture && menuRTV) {
		// Copy ImGui output to overlay texture
		ID3D11RenderTargetView* oldRTV = nullptr;
		globals::d3d::context->OMGetRenderTargets(1, &oldRTV, nullptr);
		globals::d3d::context->OMSetRenderTargets(1, &menuRTV, nullptr);
		float clearColor[4] = { 0, 0, 0, 0 };
		globals::d3d::context->ClearRenderTargetView(menuRTV, clearColor);
		// Re-render ImGui for HMD overlay
		ImGui::Render();
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		globals::d3d::context->OMSetRenderTargets(1, &oldRTV, nullptr);
		if (oldRTV)
			oldRTV->Release();

		// Apply highlight tint to HMD overlay if it's being dragged
		bool hmdBeingDragged = settings.EnableDragToReposition && overlayDragState.dragging &&
		                       (overlayDragState.mode == OverlayDragState::DragMode::HMD ||
								   overlayDragState.mode == OverlayDragState::DragMode::FixedWorld);
		Util::ApplyHighlightTintToTexture(menuTexture, hmdBeingDragged, settings.dragHighlightColor);

		// Update overlay position and submit to SteamVR
		UpdateVROverlayPosition();
		vr::Texture_t tex = { menuTexture, vr::TextureType_DirectX, vr::ColorSpace_Auto };
		if (settings.attachMode == AttachMode::HMDOnly || settings.attachMode == AttachMode::Both) {
			SetOverlayInputFlags(gameOverlay, menuOverlayHandle);
			vr::EVROverlayError err = cleanOverlay->SetOverlayTexture(menuOverlayHandle, &tex);
			if (err != vr::VROverlayError_None) {
				logger::error("SetOverlayTexture failed for menu overlay: {} ({})", static_cast<int>(err), magic_enum::enum_name(err));
			}
			err = gameOverlay->ShowOverlay(menuOverlayHandle);
			if (err != vr::VROverlayError_None) {
				logger::error("ShowOverlay failed for menu overlay: {} ({})", static_cast<int>(err), magic_enum::enum_name(err));
			}
		} else if (menuOverlayHandle != vr::k_ulOverlayHandleInvalid) {
			gameOverlay->HideOverlay(menuOverlayHandle);
		}
		// Controller overlay
		if (settings.attachMode == AttachMode::ControllerOnly || settings.attachMode == AttachMode::Both) {
			// Copy the same ImGui output to controller overlay texture
			globals::d3d::context->OMSetRenderTargets(1, &menuControllerRTV, nullptr);
			globals::d3d::context->ClearRenderTargetView(menuControllerRTV, clearColor);
			// Re-render ImGui for controller overlay
			ImGui::Render();
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
			globals::d3d::context->OMSetRenderTargets(1, &oldRTV, nullptr);

			// Apply highlight tint to controller overlay if it's being dragged
			bool controllerBeingDragged = overlayDragState.dragging &&
			                              overlayDragState.mode == OverlayDragState::DragMode::Controller;
			Util::ApplyHighlightTintToTexture(menuControllerTexture, controllerBeingDragged, settings.dragHighlightColor);

			// Position controller overlay and submit
			UpdateVROverlayControllerPosition();

			vr::Texture_t controllerTex = { menuControllerTexture, vr::TextureType_DirectX, vr::ColorSpace_Auto };
			SetOverlayInputFlags(gameOverlay, menuControllerOverlayHandle);
			vr::EVROverlayError err = cleanOverlay->SetOverlayTexture(menuControllerOverlayHandle, &controllerTex);
			if (err != vr::VROverlayError_None) {
				logger::error("SetOverlayTexture failed for controller overlay: {} ({})", static_cast<int>(err), magic_enum::enum_name(err));
			}
			err = gameOverlay->ShowOverlay(menuControllerOverlayHandle);
			if (err != vr::VROverlayError_None) {
				logger::error("ShowOverlay failed for controller overlay: {} ({})", static_cast<int>(err), magic_enum::enum_name(err));
			}
		} else if (menuControllerOverlayHandle != vr::k_ulOverlayHandleInvalid) {
			gameOverlay->HideOverlay(menuControllerOverlayHandle);
		}
	} else {
		if (menuOverlayHandle != vr::k_ulOverlayHandleInvalid) {
			gameOverlay->HideOverlay(menuOverlayHandle);
		}
		if (menuControllerOverlayHandle != vr::k_ulOverlayHandleInvalid) {
			gameOverlay->HideOverlay(menuControllerOverlayHandle);
		}
	}
}

// Handles overlay/menu open/close logic based on controller input state
void VR::UpdateOverlayMenuStateFromInput()
{
	// Disable menu interactions during combo recording
	if (this->isCapturingCombo) {
		return;
	}

	bool& isEnabled = globals::menu->IsEnabled;
	bool& overlayEnabled = globals::menu->overlayVisible;
	bool& testMode = settings.VRMenuControllerDiagnosticsTestMode;

	// Auto-disable test mode if user leaves VR section or closes menu
	if (testMode) {
		// Check if we're still in the VR section or if menu is closed
		if (!isEnabled) {
			settings.VRMenuControllerDiagnosticsTestMode = false;
			return;
		}
		// In test mode, only allow basic input processing
		return;
	}

	// Check if we're in a valid menu state for input
	bool inValidMenuState = globals::game::ui &&
	                        (globals::game::ui->IsMenuOpen(RE::MainMenu::MENU_NAME) || globals::game::ui->IsMenuOpen(RE::TweenMenu::MENU_NAME));

	if (!inValidMenuState)
		return;

	// Define menu state mappings
	struct MenuStateMapping
	{
		std::function<bool()> condition;
		std::function<void()> action;
	};

	// Generic combo checking function - makes the system truly extensible
	auto CheckCombo = [&](const std::vector<ButtonCombo>& combos) -> bool {
		if (combos.empty())
			return false;

		// Check all configured buttons in the combo
		for (size_t i = 0; i < combos.size(); ++i) {
			const auto& combo = combos[i];

			bool buttonPressed = false;

			switch (combo.GetDevice()) {
			case ControllerDevice::Both:
				// Check if this button is pressed on BOTH controllers
				buttonPressed = primaryControllerState[combo.GetKey()].isPressed &&
				                secondaryControllerState[combo.GetKey()].isPressed;
				break;
			case ControllerDevice::Primary:
				// Check if this button is pressed on PRIMARY controller only
				buttonPressed = primaryControllerState[combo.GetKey()].isPressed;
				break;
			case ControllerDevice::Secondary:
				// Check if this button is pressed on SECONDARY controller only
				buttonPressed = secondaryControllerState[combo.GetKey()].isPressed;
				break;
			}

			if (!buttonPressed) {
				return false;  // Any button not pressed means combo fails
			}
		}

		// All configured buttons are pressed according to requirements
		return true;
	};

	// Define the menu state mappings with extensible lambda array
	std::vector<MenuStateMapping> mappings = {
		// Open Community Shaders menu when closed
		{ [&]() {
			 return CheckCombo(settings.VRMenuOpenKeys) && !isEnabled;
		 },
			[&]() { isEnabled = true; } },

		// Close Community Shaders menu when open
		{ [&]() {
			 return CheckCombo(settings.VRMenuCloseKeys) && isEnabled;
		 },
			[&]() { isEnabled = false; } },

		// Open VR overlay when closed
		{ [&]() {
			 return CheckCombo(settings.VROverlayOpenKeys) && !overlayEnabled;
		 },
			[&]() { overlayEnabled = true; } },

		// Close VR overlay when open
		{ [&]() {
			 return CheckCombo(settings.VROverlayCloseKeys) && overlayEnabled;
		 },
			[&]() { overlayEnabled = false; } }
	};

	// Process mappings in order
	for (const auto& mapping : mappings) {
		if (mapping.condition()) {
			mapping.action();
			break;  // Only execute one action per frame
		}
	}
}

void VR::ProcessVREvents(std::vector<Menu::KeyEvent>& vrEvents)
{
	// Check for handedness changes and reset controller states if needed
	bool currentLeftHandedMode = RE::BSOpenVRControllerDevice::IsLeftHandedMode();
	static bool firstCall = true;
	if (firstCall || currentLeftHandedMode != lastKnownLeftHandedMode) {
		if (!firstCall) {
			logger::info("VR handedness changed: {} -> {}", lastKnownLeftHandedMode ? "Left" : "Right", currentLeftHandedMode ? "Left" : "Right");
		}
		firstCall = false;
		lastKnownLeftHandedMode = currentLeftHandedMode;
		// Reset controller states so they get repopulated with correct roles
		primaryControllerState = {};
		secondaryControllerState = {};
	}

	double nowSecs = Util::GetNowSecs();
	for (auto& event : vrEvents) {
		bool isPrimary = RE::BSOpenVRControllerDevice::IsPrimaryController(event.device);
		bool isSecondary = RE::BSOpenVRControllerDevice::IsSecondaryController(event.device);
		struct VRButtonDescriptor
		{
			const char* name;
			bool (*isButton)(std::uint32_t);
			std::uint32_t keyCode;
		};
		static const VRButtonDescriptor kVRButtons[] = {
			{ "Grip", RE::BSOpenVRControllerDevice::IsGripButton, RE::BSOpenVRControllerDevice::Keys::kGrip },
			{ "GripAlt", RE::BSOpenVRControllerDevice::IsGripButton, RE::BSOpenVRControllerDevice::Keys::kGripAlt },
			{ "Trigger", RE::BSOpenVRControllerDevice::IsTriggerButton, RE::BSOpenVRControllerDevice::Keys::kTrigger },
			{ "Stick Click", RE::BSOpenVRControllerDevice::IsStickClick, RE::BSOpenVRControllerDevice::Keys::kJoystickTrigger },
			{ "Touchpad Click", RE::BSOpenVRControllerDevice::IsTouchpadClick, RE::BSOpenVRControllerDevice::Keys::kTouchpadClick },
			{ "Touchpad Alt", RE::BSOpenVRControllerDevice::IsTouchpadClick, RE::BSOpenVRControllerDevice::Keys::kTouchpadAlt },
			{ "A/X", RE::BSOpenVRControllerDevice::IsAButton, RE::BSOpenVRControllerDevice::Keys::kXA },
			{ "B/Y", RE::BSOpenVRControllerDevice::IsBButton, RE::BSOpenVRControllerDevice::Keys::kBY },
		};
		for (const auto& desc : kVRButtons) {
			if (desc.isButton(event.keyCode)) {
				RE::ButtonState* state = isPrimary ? &primaryControllerState[desc.keyCode] : isSecondary ? &secondaryControllerState[desc.keyCode] :
				                                                                                           nullptr;
				if (state) {
					state->OnEvent(event.IsPressed(), nowSecs);
				}
				break;
			}
		}
		// Do not log events here; logging is handled in the event-specific handler
		switch (event.eventType) {
		case RE::INPUT_EVENT_TYPE::kButton:
			ProcessVRButtonEvent(event);
			break;
		case RE::INPUT_EVENT_TYPE::kThumbstick:
			UpdateControllerState(event);
			break;
		default:
			break;
		}
	}
}

void VR::ProcessVRButtonEvent(const Menu::KeyEvent& event)
{
	// Disable menu interactions during combo recording
	if (this->isCapturingCombo) {
		return;
	}

	ImGuiIO& io = ImGui::GetIO();
	(void)event;
	auto menu = globals::menu;
	bool isPrimary = RE::BSOpenVRControllerDevice::IsPrimaryController(event.device);
	bool isSecondary = RE::BSOpenVRControllerDevice::IsSecondaryController(event.device);
	bool& testMode = settings.VRMenuControllerDiagnosticsTestMode;
	constexpr size_t kNumTriggerMappings = 1;

	// Process button mappings for the current controller
	if (isPrimary || isSecondary) {
		// Define mappings for both controllers (only B/Y differs)
		constexpr size_t kNumMappings = 6;
		RE::ButtonMapping mappings[kNumMappings] = {
			{ RE::BSOpenVRControllerDevice::Keys::kTrigger, ImGuiMouseButton_Left, false, ImGuiKey_None, false },
			{ RE::BSOpenVRControllerDevice::Keys::kGrip, ImGuiMouseButton_Right, false, ImGuiKey_None, false },
			{ RE::BSOpenVRControllerDevice::Keys::kTouchpadClick, ImGuiMouseButton_Middle, false, ImGuiKey_None, false },
			{ RE::BSOpenVRControllerDevice::Keys::kJoystickTrigger, ImGuiMouseButton_Middle, false, ImGuiKey_None, false },
			{ RE::BSOpenVRControllerDevice::Keys::kBY, -1, true, menu->VirtualKeyToImGuiKey(VK_TAB), isSecondary },  // Shift+Tab for secondary
			{ RE::BSOpenVRControllerDevice::Keys::kXA, -1, true, menu->VirtualKeyToImGuiKey(VK_RETURN), false },
		};

		// Use separate state arrays for each controller
		static bool prevPrimaryStates[kNumMappings] = {};
		static bool prevSecondaryStates[kNumMappings] = {};
		bool* prevStates = isPrimary ? prevPrimaryStates : prevSecondaryStates;

		// Get the appropriate controller state
		RE::InputDeviceState& controllerState = isPrimary ? primaryControllerState : secondaryControllerState;

		size_t limit = testMode ? kNumTriggerMappings : kNumMappings;  // Only trigger mappings in test mode

		for (size_t i = 0; i < limit; ++i) {
			RE::ButtonState* state = &controllerState[mappings[i].keyCode];
			bool curr = state ? state->isPressed : false;
			if (curr != prevStates[i]) {
				if (mappings[i].isKeyEvent) {
					if (mappings[i].isShift)
						io.AddKeyEvent(ImGuiMod_Shift, curr);
					io.AddKeyEvent(static_cast<ImGuiKey>(mappings[i].key), curr);
				} else {
					io.AddMouseButtonEvent(mappings[i].logicalButton, curr);
				}
				prevStates[i] = curr;
			}
		}
	}
	// Log the button event after state is updated
	VRControllerEventLog logEntry;
	logEntry.device = static_cast<int>(event.device);
	logEntry.keyCode = event.keyCode;
	logEntry.value = static_cast<int>(event.value);
	logEntry.pressed = event.IsPressed();
	logEntry.heldTime = 0.0;
	logEntry.heldSource = "button";
	logEntry.thumbstickX = 0.0f;
	logEntry.thumbstickY = 0.0f;
	logEntry.controllerRole = isPrimary ? "Primary" : isSecondary ? "Secondary" :
	                                                                "Unknown";
	vrControllerEventLog.push_back(logEntry);
	if (vrControllerEventLog.size() > 32) {
		vrControllerEventLog.erase(vrControllerEventLog.begin());
	}
}

void VR::UpdateControllerState(const Menu::KeyEvent& event)
{
	bool isPrimary = RE::BSOpenVRControllerDevice::IsPrimaryController(event.device);
	bool isSecondary = RE::BSOpenVRControllerDevice::IsSecondaryController(event.device);

	// Update thumbstick state for diagnostics display and later input processing
	if (isPrimary) {
		primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x = event.thumbstickX;
		primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y = event.thumbstickY;
	} else if (isSecondary) {
		secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].x = event.thumbstickX;
		secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Secondary)].y = event.thumbstickY;
	}

	// Log the thumbstick event
	VRControllerEventLog logEntry;
	logEntry.device = static_cast<int>(event.device);
	logEntry.keyCode = event.keyCode;
	logEntry.value = static_cast<int>(event.value);
	logEntry.pressed = event.IsPressed();
	logEntry.heldTime = 0.0;
	logEntry.heldSource = "thumbstick";
	logEntry.thumbstickX = event.thumbstickX;
	logEntry.thumbstickY = event.thumbstickY;
	logEntry.controllerRole = isPrimary ? "Primary" : "Secondary";
	vrControllerEventLog.push_back(logEntry);
	if (vrControllerEventLog.size() > 32) {
		vrControllerEventLog.erase(vrControllerEventLog.begin());
	}
}

// Converts VR controller thumbstick input to ImGui mouse and scroll events for the overlay UI
void VR::ProcessControllerInputForImGui()
{
	if (!globals::menu->IsEnabled)
		return;
	bool testMode = settings.VRMenuControllerDiagnosticsTestMode;
	float mouseDeadzone = settings.mouseDeadzone;
	float mouseSpeed = settings.mouseSpeed;
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
	io.WantSetMousePos = false;
	if (!testMode) {
		// Determine which controller should handle cursor vs scrolling based on attachment mode
		bool useAttachedControllerForCursor = (settings.attachMode == VR::Settings::OverlayAttachMode::ControllerOnly ||
											   settings.attachMode == VR::Settings::OverlayAttachMode::Both);

		// Assign cursor and scroll controllers based on settings
		RE::VRControllerState* cursorController = nullptr;
		RE::VRControllerState* scrollController = nullptr;

		if (useAttachedControllerForCursor) {
			// When attached to controller: attached controller = cursor, other controller = scrolling
			if (settings.VRMenuAttachController == ControllerDevice::Primary) {
				cursorController = &primaryControllerState;
				scrollController = &secondaryControllerState;
			} else {
				cursorController = &secondaryControllerState;
				scrollController = &primaryControllerState;
			}
		} else {
			// HMD mode: primary = cursor, secondary = scroll (traditional)
			cursorController = &primaryControllerState;
			scrollController = &secondaryControllerState;
		}

		// Cursor movement (from determined cursor controller)
		if (cursorController) {
			// Determine the correct thumbstick index based on which controller we're using
			size_t thumbstickIndex = (cursorController == &primaryControllerState) ?
			                             static_cast<size_t>(RE::ControllerRole::Primary) :
			                             static_cast<size_t>(RE::ControllerRole::Secondary);

			float thumbstickX = cursorController->thumbsticks[thumbstickIndex].x;
			float thumbstickY = cursorController->thumbsticks[thumbstickIndex].y;
			bool usingCursorStick = (std::abs(thumbstickX) > mouseDeadzone || std::abs(thumbstickY) > mouseDeadzone);
			if (usingCursorStick) {
				ImVec2 mousePos = io.MousePos;
				mousePos.x += thumbstickX * mouseSpeed;
				mousePos.y -= thumbstickY * mouseSpeed;
				if (mousePos.x < 0)
					mousePos.x = 0;
				if (mousePos.y < 0)
					mousePos.y = 0;
				if (mousePos.x > io.DisplaySize.x)
					mousePos.x = io.DisplaySize.x;
				if (mousePos.y > io.DisplaySize.y)
					mousePos.y = io.DisplaySize.y;
				io.MousePos = mousePos;
				io.AddMousePosEvent(mousePos.x, mousePos.y);
				io.MouseDrawCursor = true;
				io.WantSetMousePos = true;
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
			}
		}

		// Scrolling (from determined scroll controller) - both horizontal and vertical
		if (scrollController) {
			// Determine the correct thumbstick index based on which controller we're using
			size_t thumbstickIndex = (scrollController == &primaryControllerState) ?
			                             static_cast<size_t>(RE::ControllerRole::Primary) :
			                             static_cast<size_t>(RE::ControllerRole::Secondary);

			bool usingScrollStickX = (std::abs(scrollController->thumbsticks[thumbstickIndex].x) > mouseDeadzone);
			bool usingScrollStickY = (std::abs(scrollController->thumbsticks[thumbstickIndex].y) > mouseDeadzone);

			if (usingScrollStickX || usingScrollStickY) {
				static float scrollAccumX = 0.0f;
				static float scrollAccumY = 0.0f;

				// Accumulate scroll input with sensitivity scaling
				scrollAccumX += scrollController->thumbsticks[thumbstickIndex].x * 0.1f;
				scrollAccumY += scrollController->thumbsticks[thumbstickIndex].y * 0.1f;

				// Send scroll events when accumulated enough input
				float scrollEventX = 0.0f;
				float scrollEventY = 0.0f;

				if (std::abs(scrollAccumX) > 0.3f) {
					scrollEventX = scrollAccumX > 0 ? 1.0f : -1.0f;
					scrollAccumX = 0.0f;
				}
				if (std::abs(scrollAccumY) > 0.3f) {
					scrollEventY = scrollAccumY > 0 ? 1.0f : -1.0f;
					scrollAccumY = 0.0f;
				}

				// Send both horizontal and vertical scroll events if needed
				if (scrollEventX != 0.0f || scrollEventY != 0.0f) {
					io.AddMouseWheelEvent(scrollEventX, scrollEventY);
				}
			}
		}
	}
}

// Helper: Get controller world matrix from OpenVR pose
bool GetControllerWorldMatrix(vr::TrackedDeviceIndex_t index, float out[3][4])
{
	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	auto* system = openvr ? openvr->vrSystem : nullptr;
	if (!system)
		return false;
	vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount];
	system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0, poses, vr::k_unMaxTrackedDeviceCount);
	if (!poses[index].bPoseIsValid)
		return false;
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 4; ++j)
			out[i][j] = poses[index].mDeviceToAbsoluteTracking.m[i][j];
	return true;
}

// --- File-scope static helpers for drag logic ---
static bool CanStartAny(vr::ETrackedControllerRole role)
{
	return role != vr::TrackedControllerRole_Invalid;
}

void VR::UpdateOverlayDrag()
{
	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	auto* system = openvr ? openvr->vrSystem : nullptr;
	if (!system)
		return;

	// Check if test mode is active - disable all dragging
	if (settings.VRMenuControllerDiagnosticsTestMode) {
		return;
	}

	if (!settings.EnableDragToReposition)
		return;

	// Helper to get grip state for a controller based on actual hand position
	auto getGripPressed = [&](bool isLeft, bool isRight) {
		bool isLeftHanded = RE::BSOpenVRControllerDevice::IsLeftHandedMode();

		if (isLeft) {
			// Left hand: primary if left-handed, secondary if right-handed
			if (isLeftHanded) {
				return primaryControllerState[RE::BSOpenVRControllerDevice::Keys::kGrip].isPressed;
			} else {
				return secondaryControllerState[RE::BSOpenVRControllerDevice::Keys::kGrip].isPressed;
			}
		}
		if (isRight) {
			// Right hand: secondary if left-handed, primary if right-handed
			if (isLeftHanded) {
				return secondaryControllerState[RE::BSOpenVRControllerDevice::Keys::kGrip].isPressed;
			} else {
				return primaryControllerState[RE::BSOpenVRControllerDevice::Keys::kGrip].isPressed;
			}
		}
		return false;
	};

	// Helper to reset drag state
	auto resetDragState = [&]() {
		overlayDragState.dragging = false;
		overlayDragState.controllerIndex = vr::k_unTrackedDeviceIndexInvalid;
		overlayDragState.isPrimary = false;
		overlayDragState.isSecondary = false;
	};

	// --- Strict mutually exclusive drag mode selection ---
	struct DragMode
	{
		OverlayDragState::DragMode mode;
		bool isActive;
		std::function<bool(vr::ETrackedControllerRole)> canStart;
		std::function<void(Matrix)> onUpdate;
		std::function<void()> onInit;
	};

	std::vector<DragMode> dragModes;

	// Controller mode - only for opposite hand (highest priority)
	if (settings.attachMode == AttachMode::ControllerOnly || settings.attachMode == AttachMode::Both) {
		// Controller drag - only opposite hand can drag the controller overlay
		dragModes.push_back({ OverlayDragState::DragMode::Controller,
			true,
			[&](vr::ETrackedControllerRole role) {
				// Get the attached controller index
				vr::TrackedDeviceIndex_t attachedControllerIndex = GetControllerIndexForDevice(settings.VRMenuAttachController);
				if (attachedControllerIndex == vr::k_unTrackedDeviceIndexInvalid) {
					return false;  // No attached controller found
				}

				// Get the opposite controller index
				ControllerDevice oppositeDevice = (settings.VRMenuAttachController == ControllerDevice::Primary) ?
			                                          ControllerDevice::Secondary :
			                                          ControllerDevice::Primary;
				vr::TrackedDeviceIndex_t oppositeControllerIndex = GetControllerIndexForDevice(oppositeDevice);
				if (oppositeControllerIndex == vr::k_unTrackedDeviceIndexInvalid) {
					return false;  // No opposite controller found
				}

				// Check if the current controller (the one doing the dragging) is the opposite controller
				for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i) {
					if (system->GetTrackedDeviceClass(i) == vr::TrackedDeviceClass_Controller) {
						vr::ETrackedControllerRole deviceRole = system->GetControllerRoleForTrackedDeviceIndex(i);
						if (deviceRole == role && i == oppositeControllerIndex) {
							return true;  // This is the opposite controller, can drag
						}
					}
				}
				return false;  // Not the opposite controller
			},
			[&](Matrix controllerMatrix) {
				// Get current attached controller transform to convert world deltas to local space
				vr::TrackedDeviceIndex_t attachedControllerIndex = GetControllerIndexForDevice(settings.VRMenuAttachController);

				if (attachedControllerIndex != vr::k_unTrackedDeviceIndexInvalid) {
					vr::TrackedDevicePose_t controllerPose;
					system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0, &controllerPose, 1);
					if (controllerPose.bPoseIsValid) {
						Matrix attachedControllerMatrix = Util::HmdMatrix34ToMatrix(controllerPose.mDeviceToAbsoluteTracking);

						// Calculate world-space delta
						Vector3 worldDelta(
							controllerMatrix._14 - overlayDragState.initialControllerMatrix._14,
							controllerMatrix._24 - overlayDragState.initialControllerMatrix._24,
							controllerMatrix._34 - overlayDragState.initialControllerMatrix._34);

						// Transform world delta to attached controller local space (use transpose for correct direction)
						Vector3 localDelta = Vector3::Transform(worldDelta, attachedControllerMatrix);

						// Apply local delta to offsets
						settings.VRMenuControllerOffsetX = overlayDragState.initialControllerOffset.x + localDelta.x;
						settings.VRMenuControllerOffsetY = overlayDragState.initialControllerOffset.y + localDelta.y;
						settings.VRMenuControllerOffsetZ = overlayDragState.initialControllerOffset.z + localDelta.z;
						UpdateVROverlayPosition();
					}
				}
			},
			[&]() {
				overlayDragState.initialControllerOffset.x = settings.VRMenuControllerOffsetX;
				overlayDragState.initialControllerOffset.y = settings.VRMenuControllerOffsetY;
				overlayDragState.initialControllerOffset.z = settings.VRMenuControllerOffsetZ;
				overlayDragState.initialControllerMatrix = overlayDragState.startControllerMatrix;
			} });
	}

	// Fixed world mode - only for attached controller when in "Both" mode
	if (settings.VRMenuPositioningMethod == 1) {
		// In "Both" mode, only the attached controller can adjust fixed world position
		// In HMD-only mode, any controller can adjust fixed world position
		std::function<bool(vr::ETrackedControllerRole)> fixedWorldCanStart;
		if (settings.attachMode == AttachMode::Both) {
			// In "Both" mode, only the attached controller can adjust fixed world position
			fixedWorldCanStart = [&](vr::ETrackedControllerRole role) {
				// Find the actual attached controller using helper function
				vr::TrackedDeviceIndex_t attachedControllerIndex = GetControllerIndexForDevice(settings.VRMenuAttachController);

				if (attachedControllerIndex != vr::k_unTrackedDeviceIndexInvalid) {
					vr::ETrackedControllerRole actualAttachedRole = system->GetControllerRoleForTrackedDeviceIndex(attachedControllerIndex);
					// Only allow the attached controller to drag fixed world
					return role == actualAttachedRole;
				}
				return false;
			};
		} else {
			// In HMD-only mode, any controller can adjust fixed world position
			fixedWorldCanStart = CanStartAny;
		}

		dragModes.push_back({ OverlayDragState::DragMode::FixedWorld,
			true,
			fixedWorldCanStart,
			[&](Matrix controllerMatrix) {
				Matrix delta = controllerMatrix * overlayDragState.initialControllerMatrix.Invert();
				fixedWorldOverlayPosition.m = delta * overlayDragState.initialOverlayMatrix;
			},
			[&]() {
				overlayDragState.initialControllerMatrix = overlayDragState.startControllerMatrix;
				overlayDragState.initialOverlayMatrix = fixedWorldOverlayPosition.m;
			} });
	}

	// HMD mode - for attached controller when both modes active, or any controller otherwise
	if (settings.attachMode == AttachMode::HMDOnly || settings.attachMode == AttachMode::Both) {
		// In "Both" mode, only the attached controller can adjust HMD position
		// In HMD-only mode, any controller can adjust HMD position
		std::function<bool(vr::ETrackedControllerRole)> hmdCanStart;
		if (settings.attachMode == AttachMode::Both) {
			// In "Both" mode, only the attached controller can adjust HMD position
			hmdCanStart = [&](vr::ETrackedControllerRole role) {
				// Find the actual attached controller using helper function
				vr::TrackedDeviceIndex_t attachedControllerIndex = GetControllerIndexForDevice(settings.VRMenuAttachController);

				if (attachedControllerIndex != vr::k_unTrackedDeviceIndexInvalid) {
					vr::ETrackedControllerRole actualAttachedRole = system->GetControllerRoleForTrackedDeviceIndex(attachedControllerIndex);
					// Only allow the attached controller to drag HMD
					return role == actualAttachedRole;
				}
				return false;
			};
		} else {
			// In HMD-only mode, any controller can adjust HMD
			hmdCanStart = CanStartAny;
		}

		dragModes.push_back({ OverlayDragState::DragMode::HMD,
			true,
			hmdCanStart,
			[&](Matrix controllerMatrix) {
				// Get current HMD transform to convert world deltas to local space
				vr::TrackedDevicePose_t hmdPose;
				system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0, &hmdPose, 1);
				if (hmdPose.bPoseIsValid) {
					Matrix hmdMatrix = Util::HmdMatrix34ToMatrix(hmdPose.mDeviceToAbsoluteTracking);

					// Calculate world-space delta
					Vector3 worldDelta(
						controllerMatrix._14 - overlayDragState.initialControllerMatrix._14,
						controllerMatrix._24 - overlayDragState.initialControllerMatrix._24,
						controllerMatrix._34 - overlayDragState.initialControllerMatrix._34);

					// Transform world delta to HMD local space (use transpose for correct direction)
					Vector3 localDelta = Vector3::Transform(worldDelta, hmdMatrix);

					// Apply local delta to offsets
					settings.VRMenuOffsetX = overlayDragState.initialHMDOffset.x + localDelta.x;
					settings.VRMenuOffsetY = overlayDragState.initialHMDOffset.y + localDelta.y;
					settings.VRMenuOffsetZ = overlayDragState.initialHMDOffset.z + localDelta.z;
					UpdateVROverlayPosition();
				}
			},
			[&]() {
				overlayDragState.initialHMDOffset.x = settings.VRMenuOffsetX;
				overlayDragState.initialHMDOffset.y = settings.VRMenuOffsetY;
				overlayDragState.initialHMDOffset.z = settings.VRMenuOffsetZ;
				overlayDragState.initialControllerMatrix = overlayDragState.startControllerMatrix;
			} });
	}

	// Drag update (if dragging)
	if (overlayDragState.dragging) {
		float rawMatrix[3][4];
		if (GetControllerWorldMatrix(overlayDragState.controllerIndex, rawMatrix)) {
			vr::HmdMatrix34_t mat = Util::Float3x4ToHmdMatrix34(rawMatrix);
			Matrix controllerMatrix = Util::HmdMatrix34ToMatrix(mat);

			// Find the active drag mode and update it
			for (const auto& mode : dragModes) {
				if (mode.mode == overlayDragState.mode) {
					mode.onUpdate(controllerMatrix);
					break;
				}
			}
		}
		bool gripPressed = getGripPressed(overlayDragState.isPrimary, overlayDragState.isSecondary);
		if (!gripPressed) {
			resetDragState();
		}
		return;
	}

	// Try to start a new drag - use first available mode
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
			bool gripPressed = getGripPressed(isLeft, isRight);
			if (!gripPressed)
				continue;
			float rawMatrix[3][4];
			if (!GetControllerWorldMatrix(i, rawMatrix))
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

			// Send haptic pulse to the controller that started the drag (only if overlay is visible)
			if (system && globals::menu->IsEnabled) {
				// Find the controller device index for the hand that started the drag
				for (vr::TrackedDeviceIndex_t deviceIdx = 0; deviceIdx < vr::k_unMaxTrackedDeviceCount; ++deviceIdx) {
					if (system->GetTrackedDeviceClass(deviceIdx) == vr::TrackedDeviceClass_Controller) {
						vr::ETrackedControllerRole deviceRole = system->GetControllerRoleForTrackedDeviceIndex(deviceIdx);
						bool isRightController = (deviceRole == vr::ETrackedControllerRole::TrackedControllerRole_RightHand);
						if (isRightController == isRight) {
							// Trigger haptic pulse (100ms = 100,000 microseconds)
							system->TriggerHapticPulse(deviceIdx, 0, static_cast<unsigned short>(100000));
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
	vr::HmdMatrix34_t transform = ComputeOverlayTransformFromHMD();
	fixedWorldOverlayPosition.m = Util::HmdMatrix34ToMatrix(transform);
}
