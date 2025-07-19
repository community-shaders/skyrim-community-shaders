#include "VR.h"
#include "Menu.h"
#include "RE/B/BSOpenVR.h"

#include "DX12SwapChain.h"
#include "State.h"
#include "Utils/UI.h"
#include <chrono>
#include <cmath>
#include <d3d11.h>
#include <imgui_impl_dx11.h>
#include <magic_enum.hpp>
#include <openvr.h>
#include <windows.h>

using AttachMode = VR::Settings::OverlayAttachMode;

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	VR::Settings,
	EnableDepthBufferCulling,
	MinOccludeeBoxExtent,
	VRMenuDistance,
	VRMenuSizePreset,
	VRMenuScale,
	VRMenuPositioningMethod,
	attachMode,
	VRMenuControllerHand,
	VRMenuOffsetX,
	VRMenuOffsetY,
	VRMenuOffsetZ,
	VRMenuControllerOffsetX,
	VRMenuControllerOffsetY,
	VRMenuControllerOffsetZ,
	VRMenuEnableControllerInput,
	VRMenuControllerDiagnosticsTestMode,
	mouseDeadzone,
	mouseSpeed)

// Add a high-resolution timer helper using QueryPerformanceCounter
inline double GetNowSecs()
{
	static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	return static_cast<double>(now.QuadPart) / static_cast<double>(freq.QuadPart);
}

vr::HmdMatrix34_t VR::ComputeOverlayTransformFromHMD()
{
	vr::HmdMatrix34_t transform = {};
	vr::IVRSystem* system = vr::VRSystem();
	if (system) {
		vr::TrackedDevicePose_t hmdPose;
		system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0, &hmdPose, 1);
		if (hmdPose.bPoseIsValid) {
			float distance = settings.VRMenuDistance;
			int texWidth = 1920, texHeight = 1080;
			if (settings.VRMenuSizePreset == 0) {
				texWidth = 1280;
				texHeight = 720;
			} else if (settings.VRMenuSizePreset == 2) {
				texWidth = 2560;
				texHeight = 1440;
			}
			float aspect = static_cast<float>(texHeight) / texWidth;
			float baseWidth = 1.0f;
			float overlayWidth = baseWidth * settings.VRMenuScale;
			float overlayHeight = overlayWidth * aspect;
			float centerOffsetX = -0.5f * (overlayWidth - baseWidth);
			float centerOffsetY = -0.5f * (overlayHeight - (baseWidth * aspect));
			float offsetX = settings.VRMenuOffsetX + centerOffsetX;
			float offsetY = settings.VRMenuOffsetY + centerOffsetY;
			float offsetZ = settings.VRMenuOffsetZ;
			transform = hmdPose.mDeviceToAbsoluteTracking;
			// Move forward by distance (Z axis in HMD space)
			transform.m[0][3] += transform.m[0][2] * (-distance);
			transform.m[1][3] += transform.m[1][2] * (-distance);
			transform.m[2][3] += transform.m[2][2] * (-distance);
			// Apply HMD overlay offsets (in HMD local space)
			transform.m[0][3] += transform.m[0][0] * offsetX + transform.m[0][1] * offsetY + transform.m[0][2] * offsetZ;
			transform.m[1][3] += transform.m[1][0] * offsetX + transform.m[1][1] * offsetY + transform.m[1][2] * offsetZ;
			transform.m[2][3] += transform.m[2][0] * offsetX + transform.m[2][1] * offsetY + transform.m[2][2] * offsetZ;
		}
	}
	return transform;
}

void VR::DrawSettings()
{
	auto menu = globals::menu;
	if (!menu)
		return;

	if (ImGui::CollapsingHeader("Controller Input Instructions", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::TextWrapped("Controller Input Options:");
		ImGui::BulletText("Trigger (Both Controllers): Left mouse button");
		ImGui::BulletText("Grip (Both Controllers): Right mouse button");
		ImGui::BulletText("Touchpad Click (Both Controllers): Middle mouse button");
		ImGui::BulletText("Stick Click (Both Controllers): Middle mouse button");
		ImGui::BulletText("A/X (Both Controllers): Enter");
		ImGui::BulletText("B/Y (Primary Controller): Tab");
		ImGui::BulletText("B/Y (Secondary Controller): Shift+Tab");
		ImGui::BulletText("Secondary Controller Thumbstick: Mouse movement");
		ImGui::BulletText("Primary Controller Thumbstick: Scroll");
		ImGui::Spacing();
		ImGui::TextWrapped("Menu Overlay:");
		ImGui::BulletText("Open: Hold both A/X and B/Y (Primary Controller) while in the main menu or tween menu");
		ImGui::BulletText("Close: Hold the Grip button on both controllers at the same time");
		ImGui::Spacing();
		ImGui::TextWrapped("HMD Input Options:");
		ImGui::BulletText("Mouse: Standard desktop mouse input");
		ImGui::BulletText("Keyboard: Standard keyboard input");
	}

	if (ImGui::Checkbox("Enable Depth Buffer Culling", &settings.EnableDepthBufferCulling))
		*gDepthBufferCulling = settings.EnableDepthBufferCulling;
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text(
			"Enables a depth buffer culling solution that checks object bounds against the depth buffer before rendering. "
			"Provides a significant performance boost and includes fixes for game engine bugs. ");
	}

	if (settings.EnableDepthBufferCulling) {
		if (ImGui::SliderFloat("Min Occludee Box Extent", &settings.MinOccludeeBoxExtent, 0.1f, 500.0f, "%.1f"))
			*gMinOccludeeBoxExtent = settings.MinOccludeeBoxExtent;
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Sets the minimum bounding box size to use for objects when testing them against the depth buffer. "
				"Helps prevent small objects from flickering due to precision issues. "
				"Lower values will give better performance. ");
		}
	}

	// Overlay Settings Section
	if (ImGui::CollapsingHeader("Overlay Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
		bool vrMenuOverlayExpanded = true;
		Util::DrawSectionHeader("VR Menu Overlay", false, true, &vrMenuOverlayExpanded);
		if (vrMenuOverlayExpanded) {
			const char* positioningMethods[] = { "HMD Relative", "Fixed World Position" };
			if (ImGui::Combo("Positioning Method", &settings.VRMenuPositioningMethod, positioningMethods, 2)) {
				UpdateVROverlayPosition();
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("HMD Relative: Menu follows your head movement. Fixed World Position: Menu stays at a fixed location in the world.");
			}
			// If in Fixed World Position mode, show reset button
			if (settings.VRMenuPositioningMethod == 1) {
				if (ImGui::Button("Reset Fixed Position to HMD")) {
					SetFixedOverlayToCurrentHMD();
				}
			}

			if (ImGui::Combo("Menu Size", &settings.VRMenuSizePreset, "Small (1280x720)\0Medium (1920x1080)\0Large (2560x1440)\0")) {
				UpdateVROverlayPosition();
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Controls the sharpness and clarity of the VR menu overlay.");
			}

			if (ImGui::SliderFloat("Menu Scale", &settings.VRMenuScale, 0.5f, 2.0f, "%.2fx")) {
				UpdateVROverlayPosition();
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Scales the menu overlay size in VR space.");
			}

			if (ImGui::SliderFloat("Menu Distance", &settings.VRMenuDistance, 0.5f, 3.0f, "%.1f m")) {
				UpdateVROverlayPosition();
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Distance from the player's head to display the menu overlay.");
			}
		}

		bool attachPointsExpanded = true;
		Util::DrawSectionHeader("Attach Points", false, true, &attachPointsExpanded);
		if (attachPointsExpanded) {
			constexpr auto attachEnums = magic_enum::enum_values<AttachMode>();
			constexpr auto attachNames = magic_enum::enum_names<AttachMode>();
			std::vector<const char*> attachLabels;
			for (size_t i = 0; i < attachEnums.size(); ++i) {
				attachLabels.push_back(attachNames[i].data());
			}
			int attachModeInt = static_cast<int>(settings.attachMode);
			if (ImGui::Combo("Overlay Attach Mode", &attachModeInt, attachLabels.data(), static_cast<int>(attachLabels.size()))) {
				settings.attachMode = static_cast<AttachMode>(attachModeInt);
				UpdateVROverlayPosition();
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Display the menu overlay in front of your head.");
			}
		}

		if (AttachMode::HMDOnly == settings.attachMode || AttachMode::Both == settings.attachMode) {
			bool hmdOffsetExpanded = true;
			Util::DrawSectionHeader("HMD Overlay Offset", false, true, &hmdOffsetExpanded);
			if (hmdOffsetExpanded) {
				if (ImGui::SliderFloat("X Offset (Left/Right)##HMD", &settings.VRMenuOffsetX, -0.5f, 0.5f, "%.2f m")) {
					UpdateVROverlayPosition();
				}
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("Horizontal offset from HMD center (negative = left, positive = right).");
				}
				if (ImGui::SliderFloat("Y Offset (Up/Down)##HMD", &settings.VRMenuOffsetY, -0.5f, 0.5f, "%.2f m")) {
					UpdateVROverlayPosition();
				}
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("Vertical offset from HMD center (negative = down, positive = up).");
				}
				if (ImGui::SliderFloat("Z Offset (Forward/Back)##HMD", &settings.VRMenuOffsetZ, -0.5f, 0.5f, "%.2f m")) {
					UpdateVROverlayPosition();
				}
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("Depth offset from HMD center (negative = forward, positive = backward).");
				}
			}
		}

		if (AttachMode::ControllerOnly == settings.attachMode || AttachMode::Both == settings.attachMode) {
			bool controllerOffsetExpanded = true;
			Util::DrawSectionHeader("Controller Offset", false, true, &controllerOffsetExpanded);
			if (controllerOffsetExpanded) {
				// Use magic_enum to get names and filter out 'Invalid'
				constexpr auto handEnums = magic_enum::enum_values<vr::ETrackedControllerRole>();
				constexpr auto handNames = magic_enum::enum_names<vr::ETrackedControllerRole>();
				std::vector<const char*> handLabels;
				std::vector<int> handIndices;
				for (size_t i = 0; i < handEnums.size(); ++i) {
					if (handEnums[i] == vr::ETrackedControllerRole::TrackedControllerRole_LeftHand || handEnums[i] == vr::ETrackedControllerRole::TrackedControllerRole_RightHand) {
						handLabels.push_back(handNames[i].data());
						handIndices.push_back(static_cast<int>(handEnums[i]));
					}
				}
				int handInt = static_cast<int>(settings.VRMenuControllerHand);
				int currentIndex = (handInt == vr::ETrackedControllerRole::TrackedControllerRole_RightHand) ? 1 : 0;
				if (ImGui::Combo("Controller Hand", &currentIndex, handLabels.data(), static_cast<int>(handLabels.size()))) {
					settings.VRMenuControllerHand = static_cast<vr::ETrackedControllerRole>(handIndices[currentIndex]);
					UpdateVROverlayPosition();
				}
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("Which controller to attach the menu to.");
				}

				if (ImGui::SliderFloat("X Offset (Left/Right)##Controller", &settings.VRMenuControllerOffsetX, -0.5f, 0.5f, "%.2f m")) {
					UpdateVROverlayPosition();
				}
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("Horizontal offset from controller center (negative = left, positive = right).");
				}

				if (ImGui::SliderFloat("Y Offset (Up/Down)##Controller", &settings.VRMenuControllerOffsetY, -0.5f, 0.5f, "%.2f m")) {
					UpdateVROverlayPosition();
				}
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("Vertical offset from controller center (negative = down, positive = up).");
				}

				if (ImGui::SliderFloat("Z Offset (Forward/Back)##Controller", &settings.VRMenuControllerOffsetZ, -0.5f, 0.5f, "%.2f m")) {
					UpdateVROverlayPosition();
				}
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("Depth offset from controller center (negative = forward, positive = backward).");
				}
			}
		}
	}

	// Input Options Section
	if (ImGui::CollapsingHeader("Input Options", ImGuiTreeNodeFlags_DefaultOpen)) {
		bool inputOptionsExpanded = true;
		if (inputOptionsExpanded) {
			if (ImGui::Checkbox("Enable Controller Input", &settings.VRMenuEnableControllerInput)) {
				UpdateVROverlayPosition();
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Enable interaction with the menu using controller input (touchpad, buttons, etc.).");
			}

			if (ImGui::SliderFloat("Mouse Deadzone", &settings.mouseDeadzone, 0.0f, 1.0f, "%.2f")) {
				// No extra action needed, value is used in menu input
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Minimum thumbstick deflection required to move the mouse or scroll. Higher values require more movement to register.");
			}

			if (ImGui::SliderFloat("Mouse Speed", &settings.mouseSpeed, 1.0f, 100.0f, "%.1f")) {
				// No extra action needed, value is used in menu input
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Mouse speed in pixels per frame per full thumbstick deflection.");
			}
		}
	}

	// Controller Diagnostics Section
	if (ImGui::CollapsingHeader("Controller Diagnostics", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (ImGui::Checkbox("Test Mode: Disable controller menu input (except right thumbstick and triggers)", &settings.VRMenuControllerDiagnosticsTestMode)) {
			ImGui::SetScrollHereY(0.0f);  // Scroll to top of the window when toggled
		}
		ImGui::SeparatorText("Button State");
		double nowSecs = GetNowSecs();

		// Get highlight color from theme
		ImVec4 highlightColor = menu->GetTheme().StatusPalette.InfoColor;
		ImU32 highlightColorU32 = ImGui::ColorConvertFloat4ToU32(highlightColor);

		if (ImGui::BeginTable("vr_input_state_table", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
			ImGui::TableSetupColumn("Button");
			ImGui::TableSetupColumn("Left State");
			ImGui::TableSetupColumn("Left Held (s)");
			ImGui::TableSetupColumn("Left Type");
			ImGui::TableSetupColumn("Right State");
			ImGui::TableSetupColumn("Right Held (s)");
			ImGui::TableSetupColumn("Right Type");
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

			printRow("Trigger", primaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kTrigger], secondaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kTrigger]);
			printRow("Grip", primaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kGrip], secondaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kGrip]);
			printRow("GripAlt", primaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kGripAlt], secondaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kGripAlt]);
			printRow("Stick Click", primaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kJoystickTrigger], secondaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kJoystickTrigger]);
			printRow("Touchpad Click", primaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kTouchpadClick], secondaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kTouchpadClick]);
			printRow("Touchpad Alt", primaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kTouchpadAlt], secondaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kTouchpadAlt]);
			printRow("B/Y", primaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kBY], secondaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kBY]);
			printRow("A/X", primaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kXA], secondaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kXA]);

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
			ImGui::TableSetupColumn("Primary Controller", ImGuiTableColumnFlags_WidthFixed, 200.0f);
			ImGui::TableSetupColumn("Secondary Controller", ImGuiTableColumnFlags_WidthFixed, 200.0f);
			ImGui::TableNextRow();

			// Primary controller cell
			ImGui::TableSetColumnIndex(0);
			ImGui::BeginGroup();
			ImVec2 padSizeL = DrawThumbstickPad(primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x, primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y, highlightCol);
			ImGui::Dummy(padSizeL);
			ImGui::SetNextItemWidth(160.0f);
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight());
			ImGui::Text("X: %+1.3f  Y: %+1.3f  [%s]", primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x, primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y, RE::GetQuadrantName(primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x, primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y));
			ImGui::EndGroup();

			// Secondary controller cell
			ImGui::TableSetColumnIndex(1);
			ImGui::BeginGroup();
			ImVec2 padSizeR = DrawThumbstickPad(secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x, secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y, highlightCol);
			ImGui::Dummy(padSizeR);
			ImGui::SetNextItemWidth(160.0f);
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight());
			ImGui::Text("X: %+1.3f  Y: %+1.3f  [%s]", secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x, secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y, RE::GetQuadrantName(secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x, secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y));
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
			for (const auto& e : vrControllerEventLog) {
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

void VR::UpdateVROverlayPosition()
{
	if (!REL::Module::IsVR()) {
		return;
	}

	if (menuOverlayHandle == vr::k_ulOverlayHandleInvalid) {
		return;
	}

	vr::IVROverlay* overlay = vr::VROverlay();
	vr::IVRSystem* system = vr::VRSystem();
	if (!overlay || !system) {
		return;
	}

	// Remove pose debug logging

	// Determine positioning strategy based on settings
	bool showOnController = (settings.attachMode == AttachMode::ControllerOnly || settings.attachMode == AttachMode::Both);
	bool showOnHMD = (settings.attachMode == AttachMode::HMDOnly || settings.attachMode == AttachMode::Both);

	// Texture size based on preset
	int texWidth = 1920, texHeight = 1080;
	if (settings.VRMenuSizePreset == 0) {
		texWidth = 1280;
		texHeight = 720;
	} else if (settings.VRMenuSizePreset == 2) {
		texWidth = 2560;
		texHeight = 1440;
	}
	float aspect = static_cast<float>(texHeight) / texWidth;
	float baseWidth = 1.0f;
	float overlayWidth = baseWidth * settings.VRMenuScale;
	float overlayHeight = overlayWidth * aspect;
	float centerOffsetX = -0.5f * (overlayWidth - baseWidth);
	float centerOffsetY = -0.5f * (overlayHeight - (baseWidth * aspect));

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
				// Calculate position in front of HMD
				float distance = settings.VRMenuDistance;
				float height = 0.0f;  // No longer using settings.VRMenuHeight
				float offsetX = settings.VRMenuOffsetX + centerOffsetX;
				float offsetY = settings.VRMenuOffsetY + centerOffsetY;
				float offsetZ = settings.VRMenuOffsetZ;

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

				// Move forward by distance (Z axis in HMD space)
				hmdTransform.m[0][3] += hmdTransform.m[0][2] * (-distance);
				hmdTransform.m[1][3] += hmdTransform.m[1][2] * (-distance);
				hmdTransform.m[2][3] += hmdTransform.m[2][2] * (-distance);

				// Move up by height (Y axis in HMD space)
				hmdTransform.m[0][3] += hmdTransform.m[0][1] * height;
				hmdTransform.m[1][3] += hmdTransform.m[1][1] * height;
				hmdTransform.m[2][3] += hmdTransform.m[2][1] * height;

				// Apply HMD overlay offsets (in HMD local space)
				hmdTransform.m[0][3] += hmdTransform.m[0][0] * offsetX + hmdTransform.m[0][1] * offsetY + hmdTransform.m[0][2] * offsetZ;
				hmdTransform.m[1][3] += hmdTransform.m[1][0] * offsetX + hmdTransform.m[1][1] * offsetY + hmdTransform.m[1][2] * offsetZ;
				hmdTransform.m[2][3] += hmdTransform.m[2][0] * offsetX + hmdTransform.m[2][1] * offsetY + hmdTransform.m[2][2] * offsetZ;

				// Scale the overlay based on width/height
				hmdTransform.m[0][0] *= overlayWidth;
				hmdTransform.m[1][1] *= overlayHeight;

				overlay->SetOverlayTransformAbsolute(menuOverlayHandle, vr::TrackingUniverseStanding, &hmdTransform);

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

			vr::HmdMatrix34_t fixedTransform;
			for (int i = 0; i < 3; ++i)
				for (int j = 0; j < 4; ++j)
					fixedTransform.m[i][j] = fixedWorldOverlayPosition.m[i][j];
			overlay->SetOverlayTransformAbsolute(menuOverlayHandle, vr::TrackingUniverseStanding, &fixedTransform);
		}
	}

	// Handle controller positioning separately (can be shown alongside HMD)
	if (showOnController) {
		// Get the VR controller overlay handle from Menu.cpp
		if (menuControllerOverlayHandle == vr::k_ulOverlayHandleInvalid) {
			return;
		}

		// Attach to controller
		vr::TrackedDeviceIndex_t controllerIndex = vr::k_unTrackedDeviceIndexInvalid;

		// Find the appropriate controller
		for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; i++) {
			if (system->GetTrackedDeviceClass(i) == vr::TrackedDeviceClass_Controller) {
				vr::ETrackedControllerRole role = system->GetControllerRoleForTrackedDeviceIndex(i);
				if (role == settings.VRMenuControllerHand) {
					controllerIndex = i;
					break;
				}
			}
		}

		if (controllerIndex != vr::k_unTrackedDeviceIndexInvalid) {
			// Position relative to controller using offset settings
			vr::HmdMatrix34_t transform;
			transform.m[0][0] = 1.0f;
			transform.m[0][1] = 0.0f;
			transform.m[0][2] = 0.0f;
			transform.m[0][3] = settings.VRMenuControllerOffsetX;
			transform.m[1][0] = 0.0f;
			transform.m[1][1] = 1.0f;
			transform.m[1][2] = 0.0f;
			transform.m[1][3] = settings.VRMenuControllerOffsetY;
			transform.m[2][0] = 0.0f;
			transform.m[2][1] = 0.0f;
			transform.m[2][2] = 1.0f;
			transform.m[2][3] = settings.VRMenuControllerOffsetZ;

			overlay->SetOverlayTransformTrackedDeviceRelative(menuControllerOverlayHandle, controllerIndex, &transform);

			// Update controller overlay flags for input interaction
			if (settings.VRMenuEnableControllerInput) {
				overlay->SetOverlayFlag(menuControllerOverlayHandle, vr::VROverlayFlags_SendVRScrollEvents, true);
				overlay->SetOverlayFlag(menuControllerOverlayHandle, vr::VROverlayFlags_SendVRTouchpadEvents, true);
				overlay->SetOverlayFlag(menuControllerOverlayHandle, vr::VROverlayFlags_AcceptsGamepadEvents, true);
			} else {
				overlay->SetOverlayFlag(menuControllerOverlayHandle, vr::VROverlayFlags_SendVRScrollEvents, false);
				overlay->SetOverlayFlag(menuControllerOverlayHandle, vr::VROverlayFlags_SendVRTouchpadEvents, false);
				overlay->SetOverlayFlag(menuControllerOverlayHandle, vr::VROverlayFlags_AcceptsGamepadEvents, false);
			}

			// Ensure controller overlay is visible in the world
			overlay->SetOverlayFlag(menuControllerOverlayHandle, vr::VROverlayFlags_VisibleInDashboard, true);
		}
	}

	// Update overlay flags for input interaction
	if (settings.VRMenuEnableControllerInput) {
		overlay->SetOverlayFlag(menuOverlayHandle, vr::VROverlayFlags_SendVRScrollEvents, true);
		overlay->SetOverlayFlag(menuOverlayHandle, vr::VROverlayFlags_SendVRTouchpadEvents, true);
		overlay->SetOverlayFlag(menuOverlayHandle, vr::VROverlayFlags_AcceptsGamepadEvents, true);
	} else {
		overlay->SetOverlayFlag(menuOverlayHandle, vr::VROverlayFlags_SendVRScrollEvents, false);
		overlay->SetOverlayFlag(menuOverlayHandle, vr::VROverlayFlags_SendVRTouchpadEvents, false);
		overlay->SetOverlayFlag(menuOverlayHandle, vr::VROverlayFlags_AcceptsGamepadEvents, false);
	}

	// Ensure overlay is visible in the world
	overlay->SetOverlayFlag(menuOverlayHandle, vr::VROverlayFlags_VisibleInDashboard, true);
}

void VR::UpdateVROverlayControllerPosition()
{
	if (!REL::Module::IsVR()) {
		return;
	}

	// Get the VR controller overlay handle from Menu.cpp
	if (menuControllerOverlayHandle == vr::k_ulOverlayHandleInvalid) {
		return;
	}

	vr::IVROverlay* overlay = vr::VROverlay();
	vr::IVRSystem* system = vr::VRSystem();
	if (!overlay || !system) {
		return;
	}

	// Texture size based on preset
	int texWidth = 1920, texHeight = 1080;
	if (settings.VRMenuSizePreset == 0) {
		texWidth = 1280;
		texHeight = 720;
	} else if (settings.VRMenuSizePreset == 2) {
		texWidth = 2560;
		texHeight = 1440;
	}
	float aspect = static_cast<float>(texHeight) / texWidth;
	float baseWidth = 1.0f;
	float overlayWidth = baseWidth * settings.VRMenuScale;
	float overlayHeight = overlayWidth * aspect;
	float centerOffsetX = -0.5f * (overlayWidth - baseWidth);
	float centerOffsetY = -0.5f * (overlayHeight - (baseWidth * aspect));

	// Note: Skyrim VR interface integration would require additional reverse engineering
	// For now, we'll use the standard OpenVR API

	// Find the appropriate controller for the controller overlay
	vr::TrackedDeviceIndex_t controllerIndex = vr::k_unTrackedDeviceIndexInvalid;

	for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; i++) {
		if (system->GetTrackedDeviceClass(i) == vr::TrackedDeviceClass_Controller) {
			vr::ETrackedControllerRole role = system->GetControllerRoleForTrackedDeviceIndex(i);
			if (role == settings.VRMenuControllerHand) {
				controllerIndex = i;
				break;
			}
		}
	}

	if (controllerIndex != vr::k_unTrackedDeviceIndexInvalid) {
		// Position relative to controller using offset settings
		vr::HmdMatrix34_t transform;
		float offsetX = settings.VRMenuControllerOffsetX + centerOffsetX;
		float offsetY = settings.VRMenuControllerOffsetY + centerOffsetY;
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

		overlay->SetOverlayTransformTrackedDeviceRelative(menuControllerOverlayHandle, controllerIndex, &transform);

		// Update controller overlay flags for input interaction
		if (settings.VRMenuEnableControllerInput) {
			overlay->SetOverlayFlag(menuControllerOverlayHandle, vr::VROverlayFlags_SendVRScrollEvents, true);
			overlay->SetOverlayFlag(menuControllerOverlayHandle, vr::VROverlayFlags_SendVRTouchpadEvents, true);
			overlay->SetOverlayFlag(menuControllerOverlayHandle, vr::VROverlayFlags_AcceptsGamepadEvents, true);
		} else {
			overlay->SetOverlayFlag(menuControllerOverlayHandle, vr::VROverlayFlags_SendVRScrollEvents, false);
			overlay->SetOverlayFlag(menuControllerOverlayHandle, vr::VROverlayFlags_SendVRTouchpadEvents, false);
			overlay->SetOverlayFlag(menuControllerOverlayHandle, vr::VROverlayFlags_AcceptsGamepadEvents, false);
		}

		// Ensure controller overlay is visible in the world
		overlay->SetOverlayFlag(menuControllerOverlayHandle, vr::VROverlayFlags_VisibleInDashboard, true);
	}
}

// Add overlay management methods for VR menu overlays
void VR::EnsureOverlayInitialized()
{
	if (menuOverlayHandle != vr::k_ulOverlayHandleInvalid)
		return;
	vr::IVROverlay* overlay = vr::VROverlay();
	if (!overlay)
		return;
	D3D11_TEXTURE2D_DESC vrDesc = {};
	int preset = settings.VRMenuSizePreset;
	if (preset == 0) {
		vrDesc.Width = 1280;
		vrDesc.Height = 720;
	} else if (preset == 1) {
		vrDesc.Width = 1920;
		vrDesc.Height = 1080;
	} else {
		vrDesc.Width = 2560;
		vrDesc.Height = 1440;
	}
	vrDesc.MipLevels = 1;
	vrDesc.ArraySize = 1;
	vrDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	vrDesc.SampleDesc.Count = 1;
	vrDesc.Usage = D3D11_USAGE_DEFAULT;
	vrDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	globals::d3d::device->CreateTexture2D(&vrDesc, nullptr, &menuTexture);
	if (menuTexture) {
		globals::d3d::device->CreateRenderTargetView(menuTexture, nullptr, &menuRTV);
	}
	std::string key = "communityshaders.menu";
	std::string name = "Community Shaders Menu";
	vr::EVROverlayError err = overlay->CreateOverlay(key.c_str(), name.c_str(), &menuOverlayHandle);
	if (err == vr::VROverlayError_None) {
		overlay->SetOverlayFlag(menuOverlayHandle, vr::VROverlayFlags_SendVRScrollEvents, true);
		overlay->SetOverlayFlag(menuOverlayHandle, vr::VROverlayFlags_SendVRTouchpadEvents, true);
		overlay->SetOverlayFlag(menuOverlayHandle, vr::VROverlayFlags_AcceptsGamepadEvents, true);
		overlay->SetOverlayWidthInMeters(menuOverlayHandle, 1.0f);
		overlay->SetOverlayFlag(menuOverlayHandle, vr::VROverlayFlags_VisibleInDashboard, true);
	}
	// Controller overlay
	std::string controllerKey = "communityshaders.menu.controller";
	std::string controllerName = "Community Shaders Menu (Controller)";
	err = overlay->CreateOverlay(controllerKey.c_str(), controllerName.c_str(), &menuControllerOverlayHandle);
	if (err == vr::VROverlayError_None) {
		globals::d3d::device->CreateTexture2D(&vrDesc, nullptr, &menuControllerTexture);
		if (menuControllerTexture) {
			globals::d3d::device->CreateRenderTargetView(menuControllerTexture, nullptr, &menuControllerRTV);
		}
		overlay->SetOverlayFlag(menuControllerOverlayHandle, vr::VROverlayFlags_SendVRScrollEvents, true);
		overlay->SetOverlayFlag(menuControllerOverlayHandle, vr::VROverlayFlags_SendVRTouchpadEvents, true);
		overlay->SetOverlayFlag(menuControllerOverlayHandle, vr::VROverlayFlags_AcceptsGamepadEvents, true);
		overlay->SetOverlayWidthInMeters(menuControllerOverlayHandle, 0.5f);
		overlay->SetOverlayFlag(menuControllerOverlayHandle, vr::VROverlayFlags_VisibleInDashboard, true);
	}
}

void VR::DestroyOverlay()
{
	vr::IVROverlay* overlay = vr::VROverlay();
	if (menuOverlayHandle != vr::k_ulOverlayHandleInvalid) {
		overlay->DestroyOverlay(menuOverlayHandle);
		menuOverlayHandle = vr::k_ulOverlayHandleInvalid;
	}
	if (menuControllerOverlayHandle != vr::k_ulOverlayHandleInvalid) {
		overlay->DestroyOverlay(menuControllerOverlayHandle);
		menuControllerOverlayHandle = vr::k_ulOverlayHandleInvalid;
	}
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

void VR::RecreateOverlayTexturesIfNeeded()
{
	static int lastPreset = -1;
	int preset = settings.VRMenuSizePreset;
	if (preset == lastPreset)
		return;
	lastPreset = preset;
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
	D3D11_TEXTURE2D_DESC vrDesc = {};
	if (preset == 0) {
		vrDesc.Width = 1280;
		vrDesc.Height = 720;
	} else if (preset == 1) {
		vrDesc.Width = 1920;
		vrDesc.Height = 1080;
	} else {
		vrDesc.Width = 2560;
		vrDesc.Height = 1440;
	}
	vrDesc.MipLevels = 1;
	vrDesc.ArraySize = 1;
	vrDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	vrDesc.SampleDesc.Count = 1;
	vrDesc.Usage = D3D11_USAGE_DEFAULT;
	vrDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	globals::d3d::device->CreateTexture2D(&vrDesc, nullptr, &menuTexture);
	if (menuTexture) {
		globals::d3d::device->CreateRenderTargetView(menuTexture, nullptr, &menuRTV);
	}
	globals::d3d::device->CreateTexture2D(&vrDesc, nullptr, &menuControllerTexture);
	if (menuControllerTexture) {
		globals::d3d::device->CreateRenderTargetView(menuControllerTexture, nullptr, &menuControllerRTV);
	}
}

void VR::SubmitOverlayFrame()
{
	vr::IVROverlay* overlay = vr::VROverlay();
	if (!overlay)
		return;
	// Only update drag logic in fixed world mode
	if (settings.VRMenuPositioningMethod == 1) {
		UpdateOverlayDrag();
	}
	auto& enabled = globals::menu->IsEnabled;
	if (enabled && menuOverlayHandle != vr::k_ulOverlayHandleInvalid && menuTexture && menuRTV) {
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
		// Update overlay position and submit to SteamVR
		UpdateVROverlayPosition();
		vr::Texture_t tex = { menuTexture, vr::TextureType_DirectX, vr::ColorSpace_Auto };
		if (settings.attachMode == AttachMode::HMDOnly || settings.attachMode == AttachMode::Both) {
			overlay->SetOverlayTexture(menuOverlayHandle, &tex);
			overlay->ShowOverlay(menuOverlayHandle);
		} else if (menuOverlayHandle != vr::k_ulOverlayHandleInvalid) {
			overlay->HideOverlay(menuOverlayHandle);
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
			// Position controller overlay and submit
			UpdateVROverlayControllerPosition();

			vr::Texture_t controllerTex = { menuControllerTexture, vr::TextureType_DirectX, vr::ColorSpace_Auto };
			overlay->SetOverlayTexture(menuControllerOverlayHandle, &controllerTex);
			overlay->ShowOverlay(menuControllerOverlayHandle);
		} else if (menuControllerOverlayHandle != vr::k_ulOverlayHandleInvalid) {
			overlay->HideOverlay(menuControllerOverlayHandle);
		}
	} else {
		if (menuOverlayHandle != vr::k_ulOverlayHandleInvalid) {
			overlay->HideOverlay(menuOverlayHandle);
		}
		if (menuControllerOverlayHandle != vr::k_ulOverlayHandleInvalid) {
			overlay->HideOverlay(menuControllerOverlayHandle);
		}
	}
}

// New function: Handles overlay/menu open/close logic based on controller input state
void VR::UpdateOverlayMenuStateFromInput()
{
	bool& isEnabled = globals::menu->IsEnabled;
	bool& testMode = settings.VRMenuControllerDiagnosticsTestMode;
	// Dual grip to close
	if (isEnabled && !testMode &&
		primaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kGrip].isPressed &&
		secondaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kGrip].isPressed) {
		isEnabled = false;
		primaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kGrip].isPressed = false;
		secondaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kGrip].isPressed = false;
	}
	// A/X + B/Y to open
	if (!isEnabled &&
		primaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kXA].isPressed &&
		primaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kBY].isPressed &&
		globals::game::ui &&
		(globals::game::ui->IsMenuOpen(RE::MainMenu::MENU_NAME) || globals::game::ui->IsMenuOpen(RE::TweenMenu::MENU_NAME))) {
		isEnabled = true;
	}
}

void VR::ProcessVREvents(std::vector<Menu::KeyEvent>& vrEvents)
{
	double nowSecs = GetNowSecs();
	for (auto& event : vrEvents) {
		bool isPrimary = RE::BSOpenVRControllerDevice::IsSecondaryController(event.device);
		bool isSecondary = RE::BSOpenVRControllerDevice::IsPrimaryController(event.device);
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
				RE::ButtonState* state = isPrimary ? &primaryControllerState.buttons[desc.keyCode] : isSecondary ? &secondaryControllerState.buttons[desc.keyCode] :
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
			ProcessVRThumbstickEvent(event);
			break;
		default:
			break;
		}
	}
}

void VR::ProcessVRButtonEvent(const Menu::KeyEvent& event)
{
	ImGuiIO& io = ImGui::GetIO();
	(void)event;
	auto menu = globals::menu;
	bool isPrimary = RE::BSOpenVRControllerDevice::IsSecondaryController(event.device);
	bool isSecondary = RE::BSOpenVRControllerDevice::IsPrimaryController(event.device);
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
			RE::ButtonState* state = &controllerState.buttons[mappings[i].keyCode];
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

void VR::ProcessVRThumbstickEvent(const Menu::KeyEvent& event)
{
	ImGuiIO& io = ImGui::GetIO();
	bool isPrimary = RE::BSOpenVRControllerDevice::IsSecondaryController(event.device);
	bool isSecondary = RE::BSOpenVRControllerDevice::IsPrimaryController(event.device);
	bool& testMode = settings.VRMenuControllerDiagnosticsTestMode;

	// Update thumbstick state and optionally map to ImGui navigation/scroll
	if (isPrimary) {
		primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x = event.thumbstickX;
		primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y = event.thumbstickY;
		// Optionally: map to scroll if not in test mode
		if (!testMode) {
			io.AddMouseWheelEvent(0.0f, primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y);
		}
	} else if (isSecondary) {
		secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x = event.thumbstickX;
		secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y = event.thumbstickY;
		// Map to mouse movement
		io.AddMousePosEvent(io.MousePos.x + secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x, io.MousePos.y + secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y);
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

// Maps VR controller thumbstick input to ImGui mouse and scroll events for the overlay UI
void VR::ProcessVRControllerOverlayInput()
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
		bool usingPrimaryStick = (std::abs(primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y) > mouseDeadzone);
		if (usingPrimaryStick) {
			static float scrollAccum = 0.0f;
			scrollAccum += primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y * 0.1f;
			if (std::abs(scrollAccum) > 0.3f) {
				io.AddMouseWheelEvent(0.0f, scrollAccum > 0 ? 1.0f : -1.0f);
				scrollAccum = 0.0f;
			}
		}
	}
	bool usingSecondaryStick = (std::abs(secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x) > mouseDeadzone || std::abs(secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y) > mouseDeadzone);
	if (usingSecondaryStick) {
		ImVec2 mousePos = io.MousePos;
		mousePos.x += secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x * mouseSpeed;
		mousePos.y -= secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y * mouseSpeed;
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

// Helper: Invert a 3x4 rigid transform matrix (rotation + translation)
void InvertMatrix3x4(const float in[3][4], float out[3][4])
{
	// Transpose rotation
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j)
			out[i][j] = in[j][i];
	// Invert translation
	for (int i = 0; i < 3; ++i) {
		out[i][3] = 0.0f;
		for (int j = 0; j < 3; ++j)
			out[i][3] -= out[i][j] * in[j][3];
	}
}

// Helper: Multiply two 3x4 matrices (rigid transforms)
void MultiplyMatrix3x4(const float a[3][4], const float b[3][4], float out[3][4])
{
	// Rotation
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j)
			out[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
	// Translation
	for (int i = 0; i < 3; ++i)
		out[i][3] = a[i][0] * b[0][3] + a[i][1] * b[1][3] + a[i][2] * b[2][3] + a[i][3];
}

// Helper: Get controller world matrix from OpenVR pose
bool GetControllerWorldMatrix(vr::TrackedDeviceIndex_t index, float out[3][4])
{
	vr::IVRSystem* system = vr::VRSystem();
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

// Helper: Overlay intersection test (controller tip in overlay local space)
bool IsControllerIntersectingOverlay(const float controllerMatrix[3][4], const float overlayMatrix[3][4], float overlayWidth, float overlayHeight, float threshold = 0.05f)
{
	// Invert overlay matrix
	float invOverlay[3][4];
	InvertMatrix3x4(overlayMatrix, invOverlay);
	// Controller tip in world space (origin)
	float tipWorld[3] = { controllerMatrix[0][3], controllerMatrix[1][3], controllerMatrix[2][3] };
	// Transform to overlay local space
	float rel[3];
	for (int i = 0; i < 3; ++i) {
		rel[i] = invOverlay[i][0] * tipWorld[0] + invOverlay[i][1] * tipWorld[1] + invOverlay[i][2] * tipWorld[2] + invOverlay[i][3];
	}
	// Check Z is close to 0 (overlay plane)
	if (std::abs(rel[2]) > threshold)
		return false;
	// Check X/Y bounds
	if (std::abs(rel[0]) > overlayWidth / 2.0f)
		return false;
	if (std::abs(rel[1]) > overlayHeight / 2.0f)
		return false;
	return true;
}

void VR::UpdateOverlayDrag()
{
	if (settings.VRMenuPositioningMethod != 1)
		return;
	vr::IVRSystem* system = vr::VRSystem();
	if (!system)
		return;
	// Allow either grip to start dragging, no intersection required
	for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i) {
		if (system->GetTrackedDeviceClass(i) != vr::TrackedDeviceClass_Controller)
			continue;
		float controllerMatrix[3][4];
		if (!GetControllerWorldMatrix(i, controllerMatrix))
			continue;
		vr::ETrackedControllerRole role = system->GetControllerRoleForTrackedDeviceIndex(i);
		bool isPrimary = (role == vr::TrackedControllerRole_LeftHand);
		bool isSecondary = (role == vr::TrackedControllerRole_RightHand);
		bool gripPressed = false;
		if (isPrimary)
			gripPressed = primaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kGrip].isPressed;
		else if (isSecondary)
			gripPressed = secondaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kGrip].isPressed;
		if (!overlayDragState.dragging && gripPressed) {
			overlayDragState.dragging = true;
			overlayDragState.controllerIndex = i;
			overlayDragState.isPrimary = isPrimary;
			overlayDragState.isSecondary = isSecondary;
			for (int r = 0; r < 3; ++r)
				for (int c = 0; c < 4; ++c) {
					overlayDragState.initialControllerMatrix[r][c] = controllerMatrix[r][c];
					overlayDragState.initialOverlayMatrix[r][c] = fixedWorldOverlayPosition.m[r][c];
				}
			// Compute grabOffset = inverse(overlay) * controller
			float invOverlay[3][4];
			InvertMatrix3x4(fixedWorldOverlayPosition.m, invOverlay);
			MultiplyMatrix3x4(invOverlay, controllerMatrix, overlayDragState.grabOffset);
			break;
		}
	}
	overlayDragState.intersecting = false;  // No intersection logic now
	// If dragging, update overlay position
	if (overlayDragState.dragging) {
		// Find the current device index for the dragging logical controller
		vr::IVRSystem* vrSystem = vr::VRSystem();
		vr::TrackedDeviceIndex_t draggingIndex = vr::k_unTrackedDeviceIndexInvalid;
		for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i) {
			if (vrSystem->GetTrackedDeviceClass(i) != vr::TrackedDeviceClass_Controller)
				continue;
			vr::ETrackedControllerRole role = vrSystem->GetControllerRoleForTrackedDeviceIndex(i);
			if ((overlayDragState.isPrimary && role == vr::TrackedControllerRole_LeftHand) ||
				(overlayDragState.isSecondary && role == vr::TrackedControllerRole_RightHand)) {
				draggingIndex = i;
				break;
			}
		}
		if (draggingIndex != vr::k_unTrackedDeviceIndexInvalid) {
			float controllerMatrix[3][4];
			if (GetControllerWorldMatrix(draggingIndex, controllerMatrix)) {
				float invGrab[3][4];
				InvertMatrix3x4(overlayDragState.grabOffset, invGrab);
				float newOverlay[3][4];
				MultiplyMatrix3x4(controllerMatrix, invGrab, newOverlay);
				for (int r = 0; r < 3; ++r)
					for (int c = 0; c < 4; ++c)
						fixedWorldOverlayPosition.m[r][c] = newOverlay[r][c];
			}
		}
		// If grip released, stop dragging
		bool gripPressed = false;
		if (overlayDragState.isPrimary)
			gripPressed = primaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kGrip].isPressed;
		else if (overlayDragState.isSecondary)
			gripPressed = secondaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kGrip].isPressed;
		if (!gripPressed) {
			overlayDragState.dragging = false;
			overlayDragState.controllerIndex = vr::k_unTrackedDeviceIndexInvalid;
			overlayDragState.isPrimary = false;
			overlayDragState.isSecondary = false;
		}
	}
}

void VR::SetFixedOverlayToCurrentHMD()
{
	vr::HmdMatrix34_t transform = ComputeOverlayTransformFromHMD();
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 4; ++j)
			fixedWorldOverlayPosition.m[i][j] = transform.m[i][j];
}
