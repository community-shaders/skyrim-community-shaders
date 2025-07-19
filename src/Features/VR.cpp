#include "VR.h"
#include "Menu.h"
#include "RE/B/BSOpenVR.h"

#include "DX12SwapChain.h"
#include "State.h"
#include "Utils/UI.h"
#include <DirectXMath.h>
#include <SimpleMath.h>
#include <cmath>
#include <d3d11.h>
#include <imgui_impl_dx11.h>
#include <magic_enum.hpp>
#include <openvr.h>
#include <windows.h>

using AttachMode = VR::Settings::OverlayAttachMode;

constexpr int kOverlayWidth = 1920;
constexpr int kOverlayHeight = 1080;

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	VR::Settings,
	EnableDepthBufferCulling,
	MinOccludeeBoxExtent,
	VRMenuDistance,
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
	mouseDeadzone,
	mouseSpeed,
	dragHighlightColor)

void CreateOverlayTextureAndRTV(ID3D11Device* device, int width, int height, ID3D11Texture2D** outTex, ID3D11RenderTargetView** outRTV)
{
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	device->CreateTexture2D(&desc, nullptr, outTex);
	if (*outTex) {
		device->CreateRenderTargetView(*outTex, nullptr, outRTV);
	}
}

void VR::ApplyHighlightTintToTexture(ID3D11Texture2D* texture, bool isHighlighted)
{
	if (!isHighlighted || !texture)
		return;

	// Create a temporary staging texture to read from
	ID3D11Texture2D* stagingTexture = nullptr;
	D3D11_TEXTURE2D_DESC desc;
	texture->GetDesc(&desc);
	desc.Usage = D3D11_USAGE_STAGING;
	desc.BindFlags = 0;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;

	if (FAILED(globals::d3d::device->CreateTexture2D(&desc, nullptr, &stagingTexture)))
		return;

	// Copy the original texture to staging
	globals::d3d::context->CopyResource(stagingTexture, texture);

	// Map the staging texture to read/write pixels
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (SUCCEEDED(globals::d3d::context->Map(stagingTexture, 0, D3D11_MAP_READ_WRITE, 0, &mapped))) {
		// Apply highlight tint to each pixel
		uint8_t* pixels = static_cast<uint8_t*>(mapped.pData);
		for (UINT y = 0; y < desc.Height; ++y) {
			for (UINT x = 0; x < desc.Width; ++x) {
				uint8_t* pixel = pixels + (y * mapped.RowPitch + x * 4);

				// Only tint non-transparent pixels (alpha > 0)
				if (pixel[3] > 0) {
					// Apply configurable highlight tint
					// Blend the original color with the highlight color
					float originalR = pixel[0] / 255.0f;
					float originalG = pixel[1] / 255.0f;
					float originalB = pixel[2] / 255.0f;

					// Blend: original * (1 - alpha) + highlight * alpha
					float blendAlpha = settings.dragHighlightColor[3];
					pixel[0] = static_cast<uint8_t>((originalR * (1.0f - blendAlpha) + settings.dragHighlightColor[0] * blendAlpha) * 255.0f);
					pixel[1] = static_cast<uint8_t>((originalG * (1.0f - blendAlpha) + settings.dragHighlightColor[1] * blendAlpha) * 255.0f);
					pixel[2] = static_cast<uint8_t>((originalB * (1.0f - blendAlpha) + settings.dragHighlightColor[2] * blendAlpha) * 255.0f);
					// Alpha stays the same
				}
			}
		}

		globals::d3d::context->Unmap(stagingTexture, 0);

		// Copy the modified texture back
		globals::d3d::context->CopyResource(texture, stagingTexture);
	}

	stagingTexture->Release();
}

// Add a high-resolution timer helper using QueryPerformanceCounter
inline double GetNowSecs()
{
	static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	return static_cast<double>(now.QuadPart) / static_cast<double>(freq.QuadPart);
}

// Conversion helpers for OpenVR API boundary
Matrix HmdMatrix34ToMatrix(const vr::HmdMatrix34_t& m)
{
	return Matrix(
		m.m[0][0], m.m[0][1], m.m[0][2], m.m[0][3],
		m.m[1][0], m.m[1][1], m.m[1][2], m.m[1][3],
		m.m[2][0], m.m[2][1], m.m[2][2], m.m[2][3],
		0, 0, 0, 1);
}

vr::HmdMatrix34_t Float3x4ToHmdMatrix34(const float m[3][4])
{
	vr::HmdMatrix34_t mat;
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 4; ++j)
			mat.m[i][j] = m[i][j];
	return mat;
}

vr::HmdMatrix34_t MatrixToHmdMatrix34(const Matrix& mat)
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

vr::HmdMatrix34_t VR::ComputeOverlayTransformFromHMD()
{
	vr::HmdMatrix34_t transform = {};
	vr::IVRSystem* system = vr::VRSystem();
	if (system) {
		vr::TrackedDevicePose_t hmdPose;
		system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0, &hmdPose, 1);
		if (hmdPose.bPoseIsValid) {
			float distance = settings.VRMenuDistance;
			float offsetX = settings.VRMenuOffsetX;
			float offsetY = settings.VRMenuOffsetY;
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
		ImGui::TextWrapped("Overlay Positioning (Grip + Drag):");
		ImGui::BulletText("Fixed World Position: Any controller can drag (HMD-only mode) or attached controller only (Both modes)");
		ImGui::BulletText("HMD Relative: Any controller can drag (HMD-only mode) or attached controller only (Both modes)");
		ImGui::BulletText("Controller Attached: Only the opposite hand can drag the controller overlay");
		ImGui::BulletText("Haptic feedback will confirm when drag starts");
		ImGui::BulletText("The overlay being dragged will be highlighted with a tint color");
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
				if (ImGui::SliderFloat("X Offset (Left/Right)##HMD", &settings.VRMenuOffsetX, -2.0f, 2.0f, "%.2f m")) {
					UpdateVROverlayPosition();
				}
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("Horizontal offset from HMD center (negative = left, positive = right).");
				}
				if (ImGui::SliderFloat("Y Offset (Up/Down)##HMD", &settings.VRMenuOffsetY, -2.0f, 2.0f, "%.2f m")) {
					UpdateVROverlayPosition();
				}
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("Vertical offset from HMD center (negative = down, positive = up).");
				}
				if (ImGui::SliderFloat("Z Offset (Forward/Back)##HMD", &settings.VRMenuOffsetZ, -2.0f, 2.0f, "%.2f m")) {
					UpdateVROverlayPosition();
				}
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("Depth offset from HMD center (negative = forward, positive = backward).");
				}

				// Reset HMD offset button
				if (ImGui::Button("Reset HMD Offset")) {
					settings.VRMenuOffsetX = 0.0f;
					settings.VRMenuOffsetY = 0.0f;
					settings.VRMenuOffsetZ = 0.0f;
					UpdateVROverlayPosition();
				}
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("Reset all HMD offset values to zero.");
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

				if (ImGui::SliderFloat("X Offset (Left/Right)##Controller", &settings.VRMenuControllerOffsetX, -2.0f, 2.0f, "%.2f m")) {
					UpdateVROverlayPosition();
				}
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("Horizontal offset from controller center (negative = left, positive = right).");
				}

				if (ImGui::SliderFloat("Y Offset (Up/Down)##Controller", &settings.VRMenuControllerOffsetY, -2.0f, 2.0f, "%.2f m")) {
					UpdateVROverlayPosition();
				}
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("Vertical offset from controller center (negative = down, positive = up).");
				}

				if (ImGui::SliderFloat("Z Offset (Forward/Back)##Controller", &settings.VRMenuControllerOffsetZ, -2.0f, 2.0f, "%.2f m")) {
					UpdateVROverlayPosition();
				}
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("Depth offset from controller center (negative = forward, positive = backward).");
				}

				// Reset Controller offset button
				if (ImGui::Button("Reset Controller Offset")) {
					settings.VRMenuControllerOffsetX = 0.0f;
					settings.VRMenuControllerOffsetY = 0.0f;
					settings.VRMenuControllerOffsetZ = 0.0f;
					UpdateVROverlayPosition();
				}
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("Reset all controller offset values to zero.");
				}
			}
		}
	}

	// Input Options Section
	if (ImGui::CollapsingHeader("Input Options", ImGuiTreeNodeFlags_DefaultOpen)) {
		bool inputOptionsExpanded = true;
		if (inputOptionsExpanded) {
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

			ImGui::Separator();
			ImGui::Text("Drag Highlight Color");
			if (ImGui::ColorEdit4("##DragHighlightColor", settings.dragHighlightColor.data(), ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
				// Color picker changed - no immediate action needed, will be applied on next drag
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Color and transparency of the highlight tint applied to overlays when dragging. Alpha controls the blend strength.");
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

void SetOverlayInputFlags(vr::IVROverlay* overlay, vr::VROverlayHandle_t handle)
{
	overlay->SetOverlayFlag(handle, vr::VROverlayFlags_SendVRScrollEvents, true);
	overlay->SetOverlayFlag(handle, vr::VROverlayFlags_SendVRTouchpadEvents, true);
	overlay->SetOverlayFlag(handle, vr::VROverlayFlags_AcceptsGamepadEvents, true);
	overlay->SetOverlayFlag(handle, vr::VROverlayFlags_VisibleInDashboard, true);
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
				// Calculate position in front of HMD
				float distance = settings.VRMenuDistance;
				float height = 0.0f;  // No longer using settings.VRMenuHeight

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

			vr::HmdMatrix34_t fixedTransform = MatrixToHmdMatrix34(fixedWorldOverlayPosition.m);
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
	float aspect = static_cast<float>(kOverlayHeight) / kOverlayWidth;
	float baseWidth = 1.0f;
	float overlayWidth = baseWidth * settings.VRMenuScale;
	float overlayHeight = overlayWidth * aspect;

	// Note: Skyrim VR interface integration would require additional reverse engineering
	// For now, we'll use the standard OpenVR API

	// Find the appropriate controller for the controller overlay
	vr::TrackedDeviceIndex_t controllerIndex = vr::k_unTrackedDeviceIndexInvalid;
	bool foundPreferred = false;
	vr::TrackedDeviceIndex_t firstController = vr::k_unTrackedDeviceIndexInvalid;
	for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; i++) {
		if (system->GetTrackedDeviceClass(i) == vr::TrackedDeviceClass_Controller) {
			if (firstController == vr::k_unTrackedDeviceIndexInvalid) {
				firstController = i;
			}
			vr::ETrackedControllerRole role = system->GetControllerRoleForTrackedDeviceIndex(i);
			if (role == settings.VRMenuControllerHand) {
				controllerIndex = i;
				foundPreferred = true;
				break;
			}
		}
	}
	if (!foundPreferred && firstController != vr::k_unTrackedDeviceIndexInvalid) {
		controllerIndex = firstController;
	}
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
	if (menuOverlayHandle != vr::k_ulOverlayHandleInvalid)
		return;
	vr::IVROverlay* overlay = vr::VROverlay();
	if (!overlay)
		return;
	CreateOverlayTextureAndRTV(globals::d3d::device, kOverlayWidth, kOverlayHeight, &menuTexture, &menuRTV);
	std::string key = "communityshaders.menu";
	std::string name = "Community Shaders Menu";
	vr::EVROverlayError err = overlay->CreateOverlay(key.c_str(), name.c_str(), &menuOverlayHandle);
	if (err == vr::VROverlayError_None) {
		SetOverlayInputFlags(overlay, menuOverlayHandle);
		overlay->SetOverlayWidthInMeters(menuOverlayHandle, 1.0f);
	}
	// Controller overlay
	std::string controllerKey = "communityshaders.menu.controller";
	std::string controllerName = "Community Shaders Menu (Controller)";
	err = overlay->CreateOverlay(controllerKey.c_str(), controllerName.c_str(), &menuControllerOverlayHandle);
	if (err == vr::VROverlayError_None) {
		CreateOverlayTextureAndRTV(globals::d3d::device, kOverlayWidth, kOverlayHeight, &menuControllerTexture, &menuControllerRTV);
		SetOverlayInputFlags(overlay, menuControllerOverlayHandle);
		overlay->SetOverlayWidthInMeters(menuControllerOverlayHandle, 1.0f);
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
	CreateOverlayTextureAndRTV(globals::d3d::device, kOverlayWidth, kOverlayHeight, &menuTexture, &menuRTV);
	CreateOverlayTextureAndRTV(globals::d3d::device, kOverlayWidth, kOverlayHeight, &menuControllerTexture, &menuControllerRTV);
}

void VR::SubmitOverlayFrame()
{
	vr::IVROverlay* overlay = vr::VROverlay();
	if (!overlay)
		return;
	// Update drag logic for all modes
	UpdateOverlayDrag();
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

		// Apply highlight tint to HMD overlay if it's being dragged
		bool hmdBeingDragged = overlayDragState.dragging &&
		                       (overlayDragState.mode == OverlayDragState::DragMode::HMD ||
								   overlayDragState.mode == OverlayDragState::DragMode::FixedWorld);
		ApplyHighlightTintToTexture(menuTexture, hmdBeingDragged);

		// Update overlay position and submit to SteamVR
		UpdateVROverlayPosition();
		vr::Texture_t tex = { menuTexture, vr::TextureType_DirectX, vr::ColorSpace_Auto };
		if (settings.attachMode == AttachMode::HMDOnly || settings.attachMode == AttachMode::Both) {
			SetOverlayInputFlags(overlay, menuOverlayHandle);
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

			// Apply highlight tint to controller overlay if it's being dragged
			bool controllerBeingDragged = overlayDragState.dragging &&
			                              overlayDragState.mode == OverlayDragState::DragMode::Controller;
			ApplyHighlightTintToTexture(menuControllerTexture, controllerBeingDragged);

			// Position controller overlay and submit
			UpdateVROverlayControllerPosition();

			vr::Texture_t controllerTex = { menuControllerTexture, vr::TextureType_DirectX, vr::ColorSpace_Auto };
			SetOverlayInputFlags(overlay, menuControllerOverlayHandle);
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

// --- File-scope static helpers for drag logic ---
static bool CanStartAny(vr::ETrackedControllerRole, bool isLeft, bool isRight)
{
	return isLeft || isRight;
}

void VR::UpdateOverlayDrag()
{
	vr::IVRSystem* system = vr::VRSystem();
	if (!system)
		return;

	// Check if test mode is active - disable all dragging
	if (settings.VRMenuControllerDiagnosticsTestMode) {
		return;
	}

	// Helper to get grip state for a controller
	auto getGripPressed = [&](bool isLeft, bool isRight) {
		if (isLeft)
			return primaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kGrip].isPressed;
		if (isRight)
			return secondaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kGrip].isPressed;
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
		std::function<bool(vr::ETrackedControllerRole, bool, bool)> canStart;
		std::function<void(Matrix)> onUpdate;
		std::function<void()> onInit;
	};

	std::vector<DragMode> dragModes;

	// Controller mode - only for opposite hand (highest priority)
	if (settings.attachMode == AttachMode::ControllerOnly || settings.attachMode == AttachMode::Both) {
		// Controller drag - only opposite hand can drag the controller overlay
		dragModes.push_back({ OverlayDragState::DragMode::Controller,
			true,
			[&](vr::ETrackedControllerRole role, bool isLeft, bool isRight) {
				// Find the actual attached controller (same logic as UpdateVROverlayControllerPosition)
				vr::TrackedDeviceIndex_t attachedControllerIndex = vr::k_unTrackedDeviceIndexInvalid;
				vr::TrackedDeviceIndex_t firstController = vr::k_unTrackedDeviceIndexInvalid;
				vr::ETrackedControllerRole actualAttachedRole = settings.VRMenuControllerHand;

				for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i) {
					if (system->GetTrackedDeviceClass(i) == vr::TrackedDeviceClass_Controller) {
						if (firstController == vr::k_unTrackedDeviceIndexInvalid) {
							firstController = i;
						}
						vr::ETrackedControllerRole deviceRole = system->GetControllerRoleForTrackedDeviceIndex(i);
						if (deviceRole == settings.VRMenuControllerHand) {
							attachedControllerIndex = i;
							break;
						}
					}
				}

				// If preferred controller not found, use first available (same fallback logic)
				if (attachedControllerIndex == vr::k_unTrackedDeviceIndexInvalid && firstController != vr::k_unTrackedDeviceIndexInvalid) {
					attachedControllerIndex = firstController;
					actualAttachedRole = system->GetControllerRoleForTrackedDeviceIndex(firstController);
				}

				// Find the opposite of the actual attached controller
				vr::ETrackedControllerRole oppositeRole = (actualAttachedRole == vr::ETrackedControllerRole::TrackedControllerRole_LeftHand) ?
			                                                  vr::ETrackedControllerRole::TrackedControllerRole_RightHand :
			                                                  vr::ETrackedControllerRole::TrackedControllerRole_LeftHand;

				// Only allow the opposite controller to drag
				return (attachedControllerIndex != vr::k_unTrackedDeviceIndexInvalid) &&
			           ((isLeft && role == oppositeRole) ||
						   (isRight && role == oppositeRole));
			},
			[&](Matrix controllerMatrix) {
				// Get current attached controller transform to convert world deltas to local space (same logic as above)
				vr::TrackedDeviceIndex_t attachedControllerIndex = vr::k_unTrackedDeviceIndexInvalid;
				vr::TrackedDeviceIndex_t firstController = vr::k_unTrackedDeviceIndexInvalid;

				for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i) {
					if (system->GetTrackedDeviceClass(i) == vr::TrackedDeviceClass_Controller) {
						if (firstController == vr::k_unTrackedDeviceIndexInvalid) {
							firstController = i;
						}
						vr::ETrackedControllerRole role = system->GetControllerRoleForTrackedDeviceIndex(i);
						if (role == settings.VRMenuControllerHand) {
							attachedControllerIndex = i;
							break;
						}
					}
				}

				// If preferred controller not found, use first available (same fallback logic)
				if (attachedControllerIndex == vr::k_unTrackedDeviceIndexInvalid && firstController != vr::k_unTrackedDeviceIndexInvalid) {
					attachedControllerIndex = firstController;
				}

				if (attachedControllerIndex != vr::k_unTrackedDeviceIndexInvalid) {
					vr::TrackedDevicePose_t controllerPose;
					system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0, &controllerPose, 1);
					if (controllerPose.bPoseIsValid) {
						Matrix attachedControllerMatrix = HmdMatrix34ToMatrix(controllerPose.mDeviceToAbsoluteTracking);

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
		std::function<bool(vr::ETrackedControllerRole, bool, bool)> fixedWorldCanStart;
		if (settings.attachMode == AttachMode::Both) {
			// In "Both" mode, only the attached controller can adjust fixed world position
			fixedWorldCanStart = [&](vr::ETrackedControllerRole role, bool isLeft, bool isRight) {
				// Find the actual attached controller (same logic as controller drag)
				vr::TrackedDeviceIndex_t attachedControllerIndex = vr::k_unTrackedDeviceIndexInvalid;
				vr::TrackedDeviceIndex_t firstController = vr::k_unTrackedDeviceIndexInvalid;
				vr::ETrackedControllerRole actualAttachedRole = settings.VRMenuControllerHand;

				for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i) {
					if (system->GetTrackedDeviceClass(i) == vr::TrackedDeviceClass_Controller) {
						if (firstController == vr::k_unTrackedDeviceIndexInvalid) {
							firstController = i;
						}
						vr::ETrackedControllerRole deviceRole = system->GetControllerRoleForTrackedDeviceIndex(i);
						if (deviceRole == settings.VRMenuControllerHand) {
							attachedControllerIndex = i;
							break;
						}
					}
				}

				// If preferred controller not found, use first available (same fallback logic)
				if (attachedControllerIndex == vr::k_unTrackedDeviceIndexInvalid && firstController != vr::k_unTrackedDeviceIndexInvalid) {
					attachedControllerIndex = firstController;
					actualAttachedRole = system->GetControllerRoleForTrackedDeviceIndex(firstController);
				}

				// Only allow the attached controller to drag fixed world
				return (attachedControllerIndex != vr::k_unTrackedDeviceIndexInvalid) &&
				       ((isLeft && role == actualAttachedRole) ||
						   (isRight && role == actualAttachedRole));
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
		std::function<bool(vr::ETrackedControllerRole, bool, bool)> hmdCanStart;
		if (settings.attachMode == AttachMode::Both) {
			// In "Both" mode, only the attached controller can adjust HMD position
			hmdCanStart = [&](vr::ETrackedControllerRole role, bool isLeft, bool isRight) {
				// Find the actual attached controller (same logic as controller drag)
				vr::TrackedDeviceIndex_t attachedControllerIndex = vr::k_unTrackedDeviceIndexInvalid;
				vr::TrackedDeviceIndex_t firstController = vr::k_unTrackedDeviceIndexInvalid;
				vr::ETrackedControllerRole actualAttachedRole = settings.VRMenuControllerHand;

				for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i) {
					if (system->GetTrackedDeviceClass(i) == vr::TrackedDeviceClass_Controller) {
						if (firstController == vr::k_unTrackedDeviceIndexInvalid) {
							firstController = i;
						}
						vr::ETrackedControllerRole deviceRole = system->GetControllerRoleForTrackedDeviceIndex(i);
						if (deviceRole == settings.VRMenuControllerHand) {
							attachedControllerIndex = i;
							break;
						}
					}
				}

				// If preferred controller not found, use first available (same fallback logic)
				if (attachedControllerIndex == vr::k_unTrackedDeviceIndexInvalid && firstController != vr::k_unTrackedDeviceIndexInvalid) {
					attachedControllerIndex = firstController;
					actualAttachedRole = system->GetControllerRoleForTrackedDeviceIndex(firstController);
				}

				// Only allow the attached controller to drag HMD
				return (attachedControllerIndex != vr::k_unTrackedDeviceIndexInvalid) &&
				       ((isLeft && role == actualAttachedRole) ||
						   (isRight && role == actualAttachedRole));
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
					Matrix hmdMatrix = HmdMatrix34ToMatrix(hmdPose.mDeviceToAbsoluteTracking);

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
			vr::HmdMatrix34_t mat = Float3x4ToHmdMatrix34(rawMatrix);
			Matrix controllerMatrix = HmdMatrix34ToMatrix(mat);

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
			if (!mode.canStart(role, isLeft, isRight))
				continue;
			bool gripPressed = getGripPressed(isLeft, isRight);
			if (!gripPressed)
				continue;
			float rawMatrix[3][4];
			if (!GetControllerWorldMatrix(i, rawMatrix))
				continue;
			vr::HmdMatrix34_t mat = Float3x4ToHmdMatrix34(rawMatrix);
			Matrix controllerMatrix = HmdMatrix34ToMatrix(mat);
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
	fixedWorldOverlayPosition.m = HmdMatrix34ToMatrix(transform);
}
