#include "VR.h"
#ifdef ENABLE_SKYRIM_VR
#	include "Menu.h"
#	include "RE/B/BSOpenVR.h"

#	include <chrono>
#	include "Utils/UI.h"
extern vr::VROverlayHandle_t g_vrMenuOverlayHandle;
extern vr::VROverlayHandle_t g_vrMenuControllerOverlayHandle;
#endif

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	VR::Settings,
	EnableDepthBufferCulling,
	MinOccludeeBoxExtent,
	VRMenuDistance,
	VRMenuHeight,
	VRMenuWidth,
	VRMenuPositioningMethod,
	VRMenuAttachToHMD,
	VRMenuAttachToController,
	VRMenuControllerHand,
	VRMenuOffsetX,
	VRMenuOffsetY,
	VRMenuOffsetZ,
	VRMenuControllerOffsetX,
	VRMenuControllerOffsetY,
	VRMenuControllerOffsetZ,
	VRMenuEnableControllerInput)

void VR::DrawSettings()
{
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

			if (ImGui::SliderFloat("Menu Distance", &settings.VRMenuDistance, 0.5f, 3.0f, "%.1f m")) {
				UpdateVROverlayPosition();
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Distance from the player's head to display the menu overlay.");
			}

			if (ImGui::SliderFloat("Menu Height", &settings.VRMenuHeight, -0.5f, 0.5f, "%.2f m")) {
				UpdateVROverlayPosition();
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Vertical offset from eye level (negative = below, positive = above).");
			}

			if (ImGui::SliderFloat("Menu Width", &settings.VRMenuWidth, 0.5f, 2.0f, "%.1f m")) {
				UpdateVROverlayPosition();
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Width of the menu overlay in meters.");
			}
		}

		bool attachPointsExpanded = true;
		Util::DrawSectionHeader("Attach Points", false, true, &attachPointsExpanded);
		if (attachPointsExpanded) {
			if (ImGui::Checkbox("Show on HMD", &settings.VRMenuAttachToHMD)) {
				UpdateVROverlayPosition();
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Display the menu overlay in front of your head.");
			}

			if (ImGui::Checkbox("Show on Controller", &settings.VRMenuAttachToController)) {
				UpdateVROverlayPosition();
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Display the menu overlay attached to your controller.");
			}
		}

		if (settings.VRMenuAttachToHMD) {
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

		if (settings.VRMenuAttachToController) {
			bool controllerOffsetExpanded = true;
			Util::DrawSectionHeader("Controller Offset", false, true, &controllerOffsetExpanded);
			if (controllerOffsetExpanded) {
				const char* hands[] = { "Left Hand", "Right Hand" };
				if (ImGui::Combo("Controller Hand", &settings.VRMenuControllerHand, hands, 2)) {
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

			if (ImGui::TreeNodeEx("Input Instructions", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::TextWrapped("Controller Input Options:");
				ImGui::BulletText("Touchpad: Move cursor and click");
				ImGui::BulletText("Trigger: Left mouse button");
				ImGui::BulletText("Grip: Right mouse button");
				ImGui::BulletText("Menu button: Toggle menu visibility");
				ImGui::BulletText("Thumbstick: Scroll (if supported)");
				ImGui::Spacing();
				ImGui::TextWrapped("HMD Input Options:");
				ImGui::BulletText("Mouse: Standard desktop mouse input");
				ImGui::BulletText("Keyboard: Standard keyboard input");
				ImGui::TreePop();
			}
		}
	}

	// Controller Diagnostics Section
	if (ImGui::CollapsingHeader("Controller Diagnostics", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SeparatorText("Button State");
		auto now = std::chrono::steady_clock::now().time_since_epoch();
		double nowSecs = std::chrono::duration_cast<std::chrono::duration<double>>(now).count();

		// Get highlight color from theme
		ImVec4 highlightColor = Menu::GetSingleton()->GetTheme().StatusPalette.InfoColor;
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
			auto DrawButtonType = [](const RE::BSInputDevice::ButtonState& state) {
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
			auto printRow = [&](const char* label, const RE::BSInputDevice::ButtonState& left, const RE::BSInputDevice::ButtonState& right) {
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

			printRow("Trigger", Menu::GetSingleton()->leftTriggerState, Menu::GetSingleton()->rightTriggerState);
			printRow("Grip", Menu::GetSingleton()->leftGripState, Menu::GetSingleton()->rightGripState);
			printRow("Stick Click", Menu::GetSingleton()->leftStickClickState, Menu::GetSingleton()->rightStickClickState);
			printRow("Touchpad", Menu::GetSingleton()->leftTouchpadState, Menu::GetSingleton()->rightTouchpadState);
			printRow("B/Y", Menu::GetSingleton()->leftBorYState, Menu::GetSingleton()->rightBorYState);
			printRow("A/X", Menu::GetSingleton()->leftAorXState, Menu::GetSingleton()->rightAorXState);

			ImGui::EndTable();
		}
		auto menu = Menu::GetSingleton();

		if (menu) {
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

			// Helper to get quadrant name
			auto GetQuadrantName = [](float x, float y) -> const char* {
				if (x > 0 && y > 0)
					return "Top-Right";
				if (x < 0 && y > 0)
					return "Top-Left";
				if (x < 0 && y < 0)
					return "Bottom-Left";
				if (x > 0 && y < 0)
					return "Bottom-Right";
				if (x == 0 && y == 0)
					return "Center";
				if (y == 0)
					return x > 0 ? "Right" : "Left";
				if (x == 0)
					return y > 0 ? "Top" : "Bottom";
				return "";
			};

			ImU32 highlightCol = ImGui::ColorConvertFloat4ToU32(menu->GetTheme().StatusPalette.InfoColor);

			if (ImGui::BeginTable("##VRThumbstickTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
				ImGui::TableSetupColumn("Left Controller", ImGuiTableColumnFlags_WidthFixed, 200.0f);
				ImGui::TableSetupColumn("Right Controller", ImGuiTableColumnFlags_WidthFixed, 200.0f);
				ImGui::TableNextRow();

				// Left controller cell
				ImGui::TableSetColumnIndex(0);
				ImGui::BeginGroup();
				ImVec2 padSizeL = DrawThumbstickPad(menu->leftThumbstickState.x, menu->leftThumbstickState.y, highlightCol);
				ImGui::Dummy(padSizeL);
				ImGui::SetNextItemWidth(160.0f);
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight());
				ImGui::Text("X: %+1.3f  Y: %+1.3f  [%s]", menu->leftThumbstickState.x, menu->leftThumbstickState.y, GetQuadrantName(menu->leftThumbstickState.x, menu->leftThumbstickState.y));
				ImGui::EndGroup();

				// Right controller cell
				ImGui::TableSetColumnIndex(1);
				ImGui::BeginGroup();
				ImVec2 padSizeR = DrawThumbstickPad(menu->rightThumbstickState.x, menu->rightThumbstickState.y, highlightCol);
				ImGui::Dummy(padSizeR);
				ImGui::SetNextItemWidth(160.0f);
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight());
				ImGui::Text("X: %+1.3f  Y: %+1.3f  [%s]", menu->rightThumbstickState.x, menu->rightThumbstickState.y, GetQuadrantName(menu->rightThumbstickState.x, menu->rightThumbstickState.y));
				ImGui::EndGroup();

				ImGui::EndTable();
			}
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
			for (const auto& e : Menu::GetSingleton()->vrControllerEventLog) {
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
					ImGui::TextUnformatted("Thumbstick");
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
#ifdef ENABLE_SKYRIM_VR
	if (!REL::Module::IsVR()) {
		return;
	}

	if (g_vrMenuOverlayHandle == vr::k_ulOverlayHandleInvalid) {
		return;
	}

	vr::IVROverlay* overlay = vr::VROverlay();
	vr::IVRSystem* system = vr::VRSystem();
	if (!overlay || !system) {
		return;
	}

	// Remove pose debug logging

	// Determine positioning strategy based on settings
	bool showOnController = settings.VRMenuAttachToController;
	bool showOnHMD = settings.VRMenuAttachToHMD;

	// Handle HMD positioning
	if (showOnHMD) {
		if (settings.VRMenuPositioningMethod == 0) {
			// HMD Relative positioning
			vr::TrackedDevicePose_t hmdPose;
			system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0, &hmdPose, 1);

			if (hmdPose.bPoseIsValid) {
				// Calculate position in front of HMD
				float distance = settings.VRMenuDistance;
				float height = settings.VRMenuHeight;
				float width = settings.VRMenuWidth;
				float offsetX = settings.VRMenuOffsetX;
				float offsetY = settings.VRMenuOffsetY;
				float offsetZ = settings.VRMenuOffsetZ;

				// Create transform matrix - start with identity
				vr::HmdMatrix34_t transform;
				transform.m[0][0] = 1.0f;
				transform.m[0][1] = 0.0f;
				transform.m[0][2] = 0.0f;
				transform.m[0][3] = 0.0f;
				transform.m[1][0] = 0.0f;
				transform.m[1][1] = 1.0f;
				transform.m[1][2] = 0.0f;
				transform.m[1][3] = 0.0f;
				transform.m[2][0] = 0.0f;
				transform.m[2][1] = 0.0f;
				transform.m[2][2] = 1.0f;
				transform.m[2][3] = 0.0f;

				// Copy HMD position
				transform.m[0][3] = hmdPose.mDeviceToAbsoluteTracking.m[0][3];
				transform.m[1][3] = hmdPose.mDeviceToAbsoluteTracking.m[1][3];
				transform.m[2][3] = hmdPose.mDeviceToAbsoluteTracking.m[2][3];

				// Copy HMD orientation
				transform.m[0][0] = hmdPose.mDeviceToAbsoluteTracking.m[0][0];
				transform.m[0][1] = hmdPose.mDeviceToAbsoluteTracking.m[0][1];
				transform.m[0][2] = hmdPose.mDeviceToAbsoluteTracking.m[0][2];
				transform.m[1][0] = hmdPose.mDeviceToAbsoluteTracking.m[1][0];
				transform.m[1][1] = hmdPose.mDeviceToAbsoluteTracking.m[1][1];
				transform.m[1][2] = hmdPose.mDeviceToAbsoluteTracking.m[1][2];
				transform.m[2][0] = hmdPose.mDeviceToAbsoluteTracking.m[2][0];
				transform.m[2][1] = hmdPose.mDeviceToAbsoluteTracking.m[2][1];
				transform.m[2][2] = hmdPose.mDeviceToAbsoluteTracking.m[2][2];

				// Move forward by distance (Z axis in HMD space) - ensure it's in front
				// Use negative distance to move in the direction the HMD is facing
				transform.m[0][3] += transform.m[0][2] * (-distance);
				transform.m[1][3] += transform.m[1][2] * (-distance);
				transform.m[2][3] += transform.m[2][2] * (-distance);

				// Move up by height (Y axis in HMD space)
				transform.m[0][3] += transform.m[0][1] * height;
				transform.m[1][3] += transform.m[1][1] * height;
				transform.m[2][3] += transform.m[2][1] * height;

				// Apply HMD overlay offsets (in HMD local space)
				transform.m[0][3] += transform.m[0][0] * offsetX + transform.m[0][1] * offsetY + transform.m[0][2] * offsetZ;
				transform.m[1][3] += transform.m[1][0] * offsetX + transform.m[1][1] * offsetY + transform.m[1][2] * offsetZ;
				transform.m[2][3] += transform.m[2][0] * offsetX + transform.m[2][1] * offsetY + transform.m[2][2] * offsetZ;

				// Scale the overlay based on width
				transform.m[0][0] *= width;
				transform.m[1][1] *= width * 0.75f;  // Maintain aspect ratio

				overlay->SetOverlayTransformAbsolute(g_vrMenuOverlayHandle, vr::TrackingUniverseStanding, &transform);
			} else {
				logger::debug("HMD pose invalid, falling back to fixed positioning");
				settings.VRMenuPositioningMethod = 1;  // Fall back to fixed positioning
			}
		}

		if (settings.VRMenuPositioningMethod == 1) {
			// Fixed World Position
			logger::debug("Using fixed world positioning");

			vr::HmdMatrix34_t transform;
			transform.m[0][0] = 1.0f;
			transform.m[0][1] = 0.0f;
			transform.m[0][2] = 0.0f;
			transform.m[0][3] = 0.0f;
			transform.m[1][0] = 0.0f;
			transform.m[1][1] = 1.0f;
			transform.m[1][2] = 0.0f;
			transform.m[1][3] = settings.VRMenuHeight;
			transform.m[2][0] = 0.0f;
			transform.m[2][1] = 0.0f;
			transform.m[2][2] = 1.0f;
			transform.m[2][3] = settings.VRMenuDistance;

			// Scale the overlay
			transform.m[0][0] *= settings.VRMenuWidth;
			transform.m[1][1] *= settings.VRMenuWidth * 0.75f;

			overlay->SetOverlayTransformAbsolute(g_vrMenuOverlayHandle, vr::TrackingUniverseStanding, &transform);
		}
	}

	// Handle controller positioning separately (can be shown alongside HMD)
	if (showOnController) {
		// Get the VR controller overlay handle from Menu.cpp
		if (g_vrMenuControllerOverlayHandle == vr::k_ulOverlayHandleInvalid) {
			return;
		}

		// Attach to controller
		vr::TrackedDeviceIndex_t controllerIndex = vr::k_unTrackedDeviceIndexInvalid;

		// Find the appropriate controller
		for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; i++) {
			if (system->GetTrackedDeviceClass(i) == vr::TrackedDeviceClass_Controller) {
				vr::ETrackedControllerRole role = system->GetControllerRoleForTrackedDeviceIndex(i);
				if ((settings.VRMenuControllerHand == 0 && role == vr::TrackedControllerRole_LeftHand) ||
					(settings.VRMenuControllerHand == 1 && role == vr::TrackedControllerRole_RightHand)) {
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

			overlay->SetOverlayTransformTrackedDeviceRelative(g_vrMenuControllerOverlayHandle, controllerIndex, &transform);

			// Update controller overlay flags for input interaction
			if (settings.VRMenuEnableControllerInput) {
				overlay->SetOverlayFlag(g_vrMenuControllerOverlayHandle, vr::VROverlayFlags_SendVRScrollEvents, true);
				overlay->SetOverlayFlag(g_vrMenuControllerOverlayHandle, vr::VROverlayFlags_SendVRTouchpadEvents, true);
				overlay->SetOverlayFlag(g_vrMenuControllerOverlayHandle, vr::VROverlayFlags_AcceptsGamepadEvents, true);
			} else {
				overlay->SetOverlayFlag(g_vrMenuControllerOverlayHandle, vr::VROverlayFlags_SendVRScrollEvents, false);
				overlay->SetOverlayFlag(g_vrMenuControllerOverlayHandle, vr::VROverlayFlags_SendVRTouchpadEvents, false);
				overlay->SetOverlayFlag(g_vrMenuControllerOverlayHandle, vr::VROverlayFlags_AcceptsGamepadEvents, false);
			}

			// Ensure controller overlay is visible in the world
			overlay->SetOverlayFlag(g_vrMenuControllerOverlayHandle, vr::VROverlayFlags_VisibleInDashboard, true);
		}
	}

	// Update overlay flags for input interaction
	if (settings.VRMenuEnableControllerInput) {
		overlay->SetOverlayFlag(g_vrMenuOverlayHandle, vr::VROverlayFlags_SendVRScrollEvents, true);
		overlay->SetOverlayFlag(g_vrMenuOverlayHandle, vr::VROverlayFlags_SendVRTouchpadEvents, true);
		overlay->SetOverlayFlag(g_vrMenuOverlayHandle, vr::VROverlayFlags_AcceptsGamepadEvents, true);
	} else {
		overlay->SetOverlayFlag(g_vrMenuOverlayHandle, vr::VROverlayFlags_SendVRScrollEvents, false);
		overlay->SetOverlayFlag(g_vrMenuOverlayHandle, vr::VROverlayFlags_SendVRTouchpadEvents, false);
		overlay->SetOverlayFlag(g_vrMenuOverlayHandle, vr::VROverlayFlags_AcceptsGamepadEvents, false);
	}

	// Ensure overlay is visible in the world
	overlay->SetOverlayFlag(g_vrMenuOverlayHandle, vr::VROverlayFlags_VisibleInDashboard, true);
#endif
}

void VR::UpdateVROverlayControllerPosition()
{
#ifdef ENABLE_SKYRIM_VR
	if (!REL::Module::IsVR()) {
		return;
	}

	// Get the VR controller overlay handle from Menu.cpp
	if (g_vrMenuControllerOverlayHandle == vr::k_ulOverlayHandleInvalid) {
		return;
	}

	vr::IVROverlay* overlay = vr::VROverlay();
	vr::IVRSystem* system = vr::VRSystem();
	if (!overlay || !system) {
		return;
	}

	// Note: Skyrim VR interface integration would require additional reverse engineering
	// For now, we'll use the standard OpenVR API

	// Find the appropriate controller for the controller overlay
	vr::TrackedDeviceIndex_t controllerIndex = vr::k_unTrackedDeviceIndexInvalid;

	for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; i++) {
		if (system->GetTrackedDeviceClass(i) == vr::TrackedDeviceClass_Controller) {
			vr::ETrackedControllerRole role = system->GetControllerRoleForTrackedDeviceIndex(i);
			if ((settings.VRMenuControllerHand == 0 && role == vr::TrackedControllerRole_LeftHand) ||
				(settings.VRMenuControllerHand == 1 && role == vr::TrackedControllerRole_RightHand)) {
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

		overlay->SetOverlayTransformTrackedDeviceRelative(g_vrMenuControllerOverlayHandle, controllerIndex, &transform);

		// Update controller overlay flags for input interaction
		if (settings.VRMenuEnableControllerInput) {
			overlay->SetOverlayFlag(g_vrMenuControllerOverlayHandle, vr::VROverlayFlags_SendVRScrollEvents, true);
			overlay->SetOverlayFlag(g_vrMenuControllerOverlayHandle, vr::VROverlayFlags_SendVRTouchpadEvents, true);
			overlay->SetOverlayFlag(g_vrMenuControllerOverlayHandle, vr::VROverlayFlags_AcceptsGamepadEvents, true);
		} else {
			overlay->SetOverlayFlag(g_vrMenuControllerOverlayHandle, vr::VROverlayFlags_SendVRScrollEvents, false);
			overlay->SetOverlayFlag(g_vrMenuControllerOverlayHandle, vr::VROverlayFlags_SendVRTouchpadEvents, false);
			overlay->SetOverlayFlag(g_vrMenuControllerOverlayHandle, vr::VROverlayFlags_AcceptsGamepadEvents, false);
		}

		// Ensure controller overlay is visible in the world
		overlay->SetOverlayFlag(g_vrMenuControllerOverlayHandle, vr::VROverlayFlags_VisibleInDashboard, true);
	}
#endif
}

#ifdef ENABLE_SKYRIM_VR
namespace VRInputBridge
{
	void ProcessVRInputEvent(const Menu::KeyEvent& event)
	{
		auto* menu = Menu::GetSingleton();
		auto now = std::chrono::steady_clock::now().time_since_epoch();
		double nowSecs = std::chrono::duration_cast<std::chrono::duration<double>>(now).count();
		Menu::VRControllerEventLog logEntry;
		logEntry.device = static_cast<int>(event.device);
		logEntry.keyCode = event.keyCode;
		logEntry.value = static_cast<int>(event.value);
		logEntry.pressed = event.IsPressed();
		logEntry.heldTime = 0.0;
		logEntry.heldSource = "chrono";
		bool isLeft = (event.device == RE::INPUT_DEVICE::kVivePrimary || event.device == RE::INPUT_DEVICE::kOculusPrimary || event.device == RE::INPUT_DEVICE::kWMRPrimary);
		bool isRight = (event.device == RE::INPUT_DEVICE::kViveSecondary || event.device == RE::INPUT_DEVICE::kOculusSecondary || event.device == RE::INPUT_DEVICE::kWMRSecondary);
		if (RE::BSOpenVRControllerDevice::IsGripButton(event.keyCode)) {
			if (isLeft) {
				menu->leftGripState.OnEvent(event.IsPressed(), nowSecs);
				logEntry.heldTime = menu->leftGripState.isPressed ? (nowSecs - menu->leftGripState.lastPressTime) : menu->leftGripState.holdDuration;
			} else if (isRight) {
				menu->rightGripState.OnEvent(event.IsPressed(), nowSecs);
				logEntry.heldTime = menu->rightGripState.isPressed ? (nowSecs - menu->rightGripState.lastPressTime) : menu->rightGripState.holdDuration;
			}
		}
		if (RE::BSOpenVRControllerDevice::IsTriggerButton(event.keyCode)) {
			if (isLeft) {
				menu->leftTriggerState.OnEvent(event.IsPressed(), nowSecs);
				logEntry.heldTime = menu->leftTriggerState.isPressed ? (nowSecs - menu->leftTriggerState.lastPressTime) : menu->leftTriggerState.holdDuration;
			} else if (isRight) {
				menu->rightTriggerState.OnEvent(event.IsPressed(), nowSecs);
				logEntry.heldTime = menu->rightTriggerState.isPressed ? (nowSecs - menu->rightTriggerState.lastPressTime) : menu->rightTriggerState.holdDuration;
			}
		}
		if (RE::BSOpenVRControllerDevice::IsTouchpadClick(event.keyCode)) {
			if (isLeft) {
				menu->leftTouchpadState.OnEvent(event.IsPressed(), nowSecs);
				logEntry.heldTime = menu->leftTouchpadState.isPressed ? (nowSecs - menu->leftTouchpadState.lastPressTime) : menu->leftTouchpadState.holdDuration;
			} else if (isRight) {
				menu->rightTouchpadState.OnEvent(event.IsPressed(), nowSecs);
				logEntry.heldTime = menu->rightTouchpadState.isPressed ? (nowSecs - menu->rightTouchpadState.lastPressTime) : menu->rightTouchpadState.holdDuration;
			}
		}
		if (RE::BSOpenVRControllerDevice::IsAButton(event.keyCode)) {
			if (isLeft) {
				menu->leftAorXState.OnEvent(event.IsPressed(), nowSecs);
				logEntry.heldTime = menu->leftAorXState.isPressed ? (nowSecs - menu->leftAorXState.lastPressTime) : menu->leftAorXState.holdDuration;
			} else if (isRight) {
				menu->rightAorXState.OnEvent(event.IsPressed(), nowSecs);
				logEntry.heldTime = menu->rightAorXState.isPressed ? (nowSecs - menu->rightAorXState.lastPressTime) : menu->rightAorXState.holdDuration;
			}
		}
		if (RE::BSOpenVRControllerDevice::IsBButton(event.keyCode)) {
			if (isLeft) {
				menu->leftBorYState.OnEvent(event.IsPressed(), nowSecs);
				logEntry.heldTime = menu->leftBorYState.isPressed ? (nowSecs - menu->leftBorYState.lastPressTime) : menu->leftBorYState.holdDuration;
			} else if (isRight) {
				menu->rightBorYState.OnEvent(event.IsPressed(), nowSecs);
				logEntry.heldTime = menu->rightBorYState.isPressed ? (nowSecs - menu->rightBorYState.lastPressTime) : menu->rightBorYState.holdDuration;
			}
		}
		menu->vrControllerEventLog.push_back(logEntry);
		if (menu->vrControllerEventLog.size() > 32) {
			menu->vrControllerEventLog.erase(menu->vrControllerEventLog.begin());
		}
	}
}  // namespace VRInputBridge
#endif