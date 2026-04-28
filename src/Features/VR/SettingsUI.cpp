#include "FeatureConstraints.h"
#include "Features/DynamicCubemaps.h"
#include "Features/ScreenSpaceGI.h"
#include "Features/ScreenSpaceShadows.h"
#include "Features/VR.h"
#include "Menu.h"
#include "Menu/Fonts.h"
#include "RE/B/BSOpenVR.h"
#include "RE/P/PlayerCharacter.h"
#include "State.h"
#include "Utils/PerfUtils.h"
#include "Utils/UI.h"
#include "Utils/VRUtils.h"

#include <openvr.h>

using AttachMode = VR::Settings::OverlayAttachMode;

namespace
{
	bool BeginTabItemWithFont(const char* label, Menu::FontRole role, ImGuiTabItemFlags flags = ImGuiTabItemFlags_None)
	{
		return MenuFonts::BeginTabItemWithFont(label, role, flags);
	}
}

//=============================================================================
// COMBO RECORDING HELPERS
//=============================================================================

void VR::ResetComboRecording()
{
	isCapturingCombo = false;
	currentComboType = ComboType::None;
	currentComboName = nullptr;
	recordedCombo.clear();
	comboStartTime = 0.0;
	recordingButtonControllers.clear();
}

void VR::ApplyRecordedCombo()
{
	if (recordedCombo.empty())
		return;

	switch (currentComboType) {
	case ComboType::MenuOpen:
		settings.VRMenuOpenKeys = recordedCombo;
		break;
	case ComboType::MenuClose:
		settings.VRMenuCloseKeys = recordedCombo;
		break;
	case ComboType::OverlayOpen:
		settings.VROverlayOpenKeys = recordedCombo;
		break;
	case ComboType::OverlayClose:
		settings.VROverlayCloseKeys = recordedCombo;
		break;
	default:
		break;
	}
}

//=============================================================================
// OVERLAY (WELCOME MESSAGE)
//=============================================================================

void VR::DrawOverlay()
{
	auto& vr = globals::features::vr;
	if (!vr.IsOpenVRCompatible())
		return;
	static LARGE_INTEGER overlayShowStart = { 0 };
	static LARGE_INTEGER freq = { 0 };

	bool shouldShow = IsWelcomeOverlayVisible();

	if (!shouldShow) {
		overlayShowStart.QuadPart = 0;
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
	const double autoHideSeconds = static_cast<double>(settings.kAutoHideSeconds);
	if (elapsed >= autoHideSeconds) {
		return;
	}
	int secondsLeft = int(std::ceil(autoHideSeconds - elapsed));

	ImGuiIO& io = ImGui::GetIO();
	const float scale = Util::GetUIScale();
	ImVec2 overlaySize(520 * scale, 0);
	ImVec2 overlayPos = ImVec2((io.DisplaySize.x - overlaySize.x) * 0.5f, (io.DisplaySize.y * 0.35f));
	ImGui::SetNextWindowPos(overlayPos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(overlaySize, ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.95f);

	ImGui::Begin("HowToUseOverlay", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);

	ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 500.0f * scale);
	ImGui::TextWrapped("How to Use VR Community Shaders Menu:");
	ImGui::Separator();
	ImGui::TextWrapped("You must open the Main Menu or Tween Menu before VR controls work.");
	ImGui::Spacing();
	ImGui::PopTextWrapPos();

	ImGui::Text("Open Menu: ");
	ImGui::SameLine();
	Util::DrawButtonCombo(settings.VRMenuOpenKeys, true);

	ImGui::Text("Close Menu: ");
	ImGui::SameLine();
	Util::DrawButtonCombo(settings.VRMenuCloseKeys, true);

	ImGui::Spacing();
	ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 500.0f * scale);
	ImGui::TextWrapped("Grip + Thumbstick: Adjust overlay depth (closer/farther)");
	ImGui::Spacing();
	ImGui::TextWrapped("Tip: Disable this VR overlay by setting Attach Mode to 'None' in VR settings.");
	ImGui::Spacing();
	ImGui::TextWrapped("(This welcome message will auto-hide in %d seconds)", secondsLeft);
	ImGui::TextWrapped("(Disable in: VR settings > Controller Input Instructions)");
	ImGui::PopTextWrapPos();

	ImGui::End();
}

//=============================================================================
// ANONYMOUS NAMESPACE: SETTINGS PANEL DRAW FUNCTIONS
//=============================================================================

namespace
{
	void DrawControllerInputInstructions()
	{
		auto& vr = globals::features::vr;
		auto& settings = vr.settings;
		if (!vr.IsOpenVRCompatible())
			return;
		if (ImGui::CollapsingHeader("Controller Input Instructions", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::SliderInt("Auto-hide Welcome overlay timeout", &settings.kAutoHideSeconds, 0, VR::Config::kMaxAutoHideSeconds,
				settings.kAutoHideSeconds <= 0 ? "Hidden" : "%d seconds");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Set to 0 to hide the overlay, or a positive value to show it for that many seconds");
			}
			ImGui::TextWrapped("Menu (while in the main menu or tween menu):");
			if (ImGui::BeginTable("MenuInstructionsTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Open Community Shaders Menu:");
				ImGui::TableSetColumnIndex(1);
				Util::DrawButtonCombo(settings.VRMenuOpenKeys, true);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Close Community Shaders Menu:");
				ImGui::TableSetColumnIndex(1);
				Util::DrawButtonCombo(settings.VRMenuCloseKeys, true);
				ImGui::EndTable();
			}
			ImGui::TextWrapped("Overlay (while in the main menu or tween menu):");
			if (ImGui::BeginTable("OverlayInstructionsTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Open Overlay:");
				ImGui::TableSetColumnIndex(1);
				Util::DrawButtonCombo(settings.VROverlayOpenKeys, true);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Close Overlay:");
				ImGui::TableSetColumnIndex(1);
				Util::DrawButtonCombo(settings.VROverlayCloseKeys, true);
				ImGui::EndTable();
			}
			ImGui::TextWrapped("Menu Controller Input:");
			if (ImGui::BeginTable("ControllerInputTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextColored(Util::GetControllerBothColor(), "Trigger (Both Controllers)");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("Left mouse button");
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextColored(Util::GetControllerBothColor(), "Grip (Both Controllers)");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("Right mouse button");
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextColored(Util::GetControllerBothColor(), "Touchpad Click (Both Controllers)");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("Middle mouse button");
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextColored(Util::GetControllerBothColor(), "Stick Click (Both Controllers)");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("Middle mouse button");
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextColored(Util::GetControllerBothColor(), "A/X (Both Controllers)");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("Enter");
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextColored(Util::GetControllerPrimaryColor(), "B/Y (Primary Controller)");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("Tab");
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextColored(Util::GetControllerSecondaryColor(), "B/Y (Secondary Controller)");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("Shift+Tab");
				ImGui::EndTable();
			}
			bool useAttachedControllerForCursor = (settings.attachMode == AttachMode::ControllerOnly || settings.attachMode == AttachMode::Both);
			if (ImGui::BeginTable("ThumbstickInstructionsTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
				if (useAttachedControllerForCursor) {
					if (settings.VRMenuAttachController == ControllerDevice::Primary) {
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextColored(Util::GetControllerPrimaryColor(), "Primary Controller Thumbstick");
						ImGui::TableSetColumnIndex(1);
						ImGui::Text("Mouse movement (attached controller)");
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextColored(Util::GetControllerSecondaryColor(), "Secondary Controller Thumbstick");
						ImGui::TableSetColumnIndex(1);
						ImGui::Text("Scroll");
					} else {
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextColored(Util::GetControllerPrimaryColor(), "Primary Controller Thumbstick");
						ImGui::TableSetColumnIndex(1);
						ImGui::Text("Scroll");
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextColored(Util::GetControllerSecondaryColor(), "Secondary Controller Thumbstick");
						ImGui::TableSetColumnIndex(1);
						ImGui::Text("Mouse movement (attached controller)");
					}
				} else {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextColored(Util::GetControllerPrimaryColor(), "Primary Controller Thumbstick");
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("Mouse movement (HMD mode)");
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextColored(Util::GetControllerSecondaryColor(), "Secondary Controller Thumbstick");
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("Scroll");
				}
				ImGui::EndTable();
			}
		}
	}

	void DrawStereoSettings()
	{
		auto& vr = globals::features::vr;
		VR::Settings& settings = vr.settings;

		if (ImGui::CollapsingHeader("Stereo Reprojection", ImGuiTreeNodeFlags_DefaultOpen))
			vr.stereoOpt.DrawSettings();

		bool hasEffects = VR::AnyScreenSpaceEffectLoaded();
		bool isDev = globals::state && globals::state->IsDeveloperMode();

		if (ImGui::CollapsingHeader("Stereo Blend", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (!hasEffects && !isDev) {
				ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Requires an active screen-space effect (SSGI, SS Shadows, SSR).");
			} else {
				if (!hasEffects)
					ImGui::TextColored(ImVec4(0.6f, 0.6f, 1.0f, 1.0f), "Developer mode: no screen-space effects active.");

				ImGui::Checkbox("Enable Stereo Blend", &settings.EnableStereoBlend);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text(
						"Post-composite depth-aware bilateral blend between eyes.\n"
						"Reduces stereo inconsistencies from screen-space effects (SSGI, SSR, etc.).\n"
						"Each pixel is reprojected to the other eye; blending is applied only where\n"
						"depth agrees (same surface). Full-screen pass in VR.");
				}

				ImGui::BeginDisabled(!settings.EnableStereoBlend);

				ImGui::SliderFloat("Depth Sigma", &settings.StereoBlendDepthSigma, 0.001f, 0.1f, "%.4f");
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text(
						"Depth sensitivity for the bilateral weight.\n"
						"Lower values are stricter -- only blend when depths match very closely.\n"
						"Higher values allow blending across slight depth differences.\n"
						"Default: 0.01");
				}

				ImGui::SliderFloat("Max Blend Factor", &settings.StereoBlendMaxFactor, 0.0f, 0.5f, "%.2f");
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text(
						"Maximum blend strength between the two eyes.\n"
						"Higher values reduce screen-space effect flicker but destroy stereo depth.\n"
						"Keep below ~0.15 to preserve 3D parallax.\n"
						"Default: 0.1");
				}

				ImGui::SliderFloat("Color Difference Threshold", &settings.StereoBlendColorThreshold, 0.0f, 0.2f, "%.3f");
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text(
						"Minimum luminance difference between eyes to trigger blending.\n"
						"Set to 0 to blend everywhere. Higher = more selective.\n"
						"Default: 0.02");
				}

				ImGui::EndDisabled();
			}
		}

		if (hasEffects || isDev) {
			ImGui::Separator();

			// Auto-enable required feature when a debug mode is selected; restore on Off.
			// Tracks what we toggled so user-initiated changes aren't clobbered.
			static bool s_weEnabledStereoBlend = false;
			static bool s_weEnabledReproj = false;

			const char* debugModes[] = { "Off", "Back-Check", "Blend Weight", "Edge Detection", "Overwrite", "Overwrite Eye1" };
			if (ImGui::Combo("Debug View", &settings.StereoBlendDebugMode, debugModes, IM_ARRAYSIZE(debugModes))) {
				int newMode = settings.StereoBlendDebugMode;
				bool needsBlend = (newMode >= 1 && newMode <= 3);
				bool needsReproj = (newMode == 4 || newMode == 5);

				// Auto-enable Stereo Blend for modes 1-3 (runtime-toggleable)
				if (needsBlend && !settings.EnableStereoBlend) {
					settings.EnableStereoBlend = true;
					s_weEnabledStereoBlend = true;
				} else if (!needsBlend && s_weEnabledStereoBlend) {
					settings.EnableStereoBlend = false;
					s_weEnabledStereoBlend = false;
				}

				// Auto-enable Reprojection for modes 4-5 (note: takes effect after restart)
				auto& sm = vr.stereoOpt.settings.stereoMode;
				if (needsReproj && sm == VRStereoOptimizations::StereoMode::Off) {
					sm = VRStereoOptimizations::StereoMode::Enable;
					s_weEnabledReproj = true;
				} else if (!needsReproj && s_weEnabledReproj) {
					sm = VRStereoOptimizations::StereoMode::Off;
					s_weEnabledReproj = false;
				}
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Selecting a debug mode auto-enables the required feature; setting back to Off restores it.\n\n"
					"Off: Normal rendering\n"
					"Back-Check: Round-trip reprojection validation (auto-enables Stereo Blend)\n"
					"Blend Weight: Heatmap of bilateral blend intensity (auto-enables Stereo Blend)\n"
					"Edge Detection: Highlights depth discontinuities (auto-enables Stereo Blend)\n"
					"Overwrite: Mode texture classification (auto-enables Reprojection -- restart required)\n"
					"  Green=edge  Pink=edge neighbour  Blue=disoccluded  Orange=full blend\n"
					"Overwrite Eye1: POM depth heatmap for Eye 1 (auto-enables Reprojection -- restart required)");
			}
		}
	}

	void DrawGeneralVRSettings()
	{
		auto& vr = globals::features::vr;
		VR::Settings& settings = vr.settings;
		if (ImGui::CollapsingHeader("General Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			bool exteriorChanged = ImGui::Checkbox("Enable Depth Buffer Culling in Exteriors", &settings.EnableDepthBufferCullingExterior);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Improves performance in exteriors, recommended ON.");
			}

			bool interiorChanged = ImGui::Checkbox("Enable Depth Buffer Culling in Interiors", &settings.EnableDepthBufferCullingInterior);
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Improves performance in interiors, recommended ON.");
			}

			if (exteriorChanged || interiorChanged) {
				vr.UpdateDepthBufferCulling();
			}

			if (ImGui::SliderFloat("Min Occludee Box Extent", &settings.MinOccludeeBoxExtent, 0.0f, 1000.0f, "%.1f")) {
				if (vr.gMinOccludeeBoxExtent) {
					*vr.gMinOccludeeBoxExtent = settings.MinOccludeeBoxExtent;
				}
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Minimum bounding box dimensions for object occlusion culling. Lower values improve performance but may result in visual artifacts.");
			}
		}
	}

	void DrawMenuSettings()
	{
		auto& vr = globals::features::vr;
		auto& settings = vr.settings;
		if (!vr.IsOpenVRCompatible())
			return;
		if (ImGui::CollapsingHeader("Menu Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			float maxScale = VR::Config::kMaxMenuScale;
			ImGui::SliderFloat("Menu Scale", &settings.VRMenuScale, VR::Config::kMinMenuScale, maxScale, "%.2f");
			const char* positioningMethods[] = { "HMD Relative", "Fixed World Position" };
			int prevMethod = settings.VRMenuPositioningMethod;
			if (ImGui::Combo("Menu Positioning Method", &settings.VRMenuPositioningMethod, positioningMethods, IM_ARRAYSIZE(positioningMethods))) {
				if (prevMethod != 1 && settings.VRMenuPositioningMethod == 1) {
					vr.SetFixedOverlayToCurrentHMD();
					auto player = RE::PlayerCharacter::GetSingleton();
					if (player)
						vr.savedPlayerWorldPos = player->GetPosition();
				}
			}
			const char* attachModes[] = { "HMD Only", "Controller Only", "Both", "None (Disabled)" };
			int attachModeInt = static_cast<int>(settings.attachMode);
			if (ImGui::Combo("Attach Mode", &attachModeInt, attachModes, IM_ARRAYSIZE(attachModes))) {
				settings.attachMode = static_cast<AttachMode>(attachModeInt);
			}

			if (settings.attachMode == AttachMode::ControllerOnly ||
				settings.attachMode == AttachMode::Both) {
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

			if (settings.attachMode == AttachMode::HMDOnly ||
				settings.attachMode == AttachMode::Both) {
				ImGui::Separator();
				ImGui::Text("HMD Offset Settings");
				ImGui::SliderFloat("HMD Offset X", &settings.VRMenuOffsetX, -2.0f, 2.0f, "%.2f");
				ImGui::SliderFloat("HMD Offset Y", &settings.VRMenuOffsetY, -2.0f, 2.0f, "%.2f");
				ImGui::SliderFloat("HMD Offset Z", &settings.VRMenuOffsetZ, -4.0f, 1.0f, "%.2f");
			}

			if (settings.VRMenuPositioningMethod == 1) {
				ImGui::Separator();
				ImGui::Text("Fixed World Position Settings");
				ImGui::SliderFloat("Auto Reset Distance (game units)", &settings.VRMenuAutoResetDistance, 100.0f, 5000.0f, "%.0f");
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("If you move farther than this distance from the menu, it will automatically reset to your HMD position. %s", Util::Units::FormatDistance(settings.VRMenuAutoResetDistance).c_str());
				}
				if (ImGui::Button("Reset Menu to HMD Position")) {
					vr.SetFixedOverlayToCurrentHMD();
				}
			}
		}
	}

	void DrawMouseSettings()
	{
		auto& vr = globals::features::vr;
		if (!vr.IsOpenVRCompatible())
			return;
		VR::Settings& settings = vr.settings;
		if (ImGui::CollapsingHeader("Input Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (ImGui::Checkbox("Enable Wand Pointing", &settings.EnableWandPointing)) {
				vr.wandState.isIntersecting = false;
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Use controller ray-casting to point at UI elements");
			}
			ImGui::Separator();
			ImGui::Text("Joystick Settings");
			ImGui::SliderFloat("Mouse Deadzone", &settings.mouseDeadzone, 0.0f, 1.0f, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Thumbstick deadzone for joystick cursor movement");
			}
			ImGui::SliderFloat("Mouse Speed", &settings.mouseSpeed, 0.1f, 50.0f, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Speed multiplier for joystick cursor movement");
			}
		}
	}

	void DrawDragSettings()
	{
		auto& vr = globals::features::vr;
		if (!vr.IsOpenVRCompatible())
			return;
		VR::Settings& settings = vr.settings;
		if (ImGui::CollapsingHeader("Drag Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (ImGui::CollapsingHeader("Drag Instructions", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::TextWrapped("Overlay Positioning (Grip + Drag):");
				ImGui::BulletText("Fixed World Position: Any controller can drag (HMD-only mode) or attached controller only (Both modes)");
				ImGui::BulletText("HMD Relative: Any controller can drag (HMD-only mode) or attached controller only (Both modes)");
				ImGui::BulletText("Controller Attached: Only the opposite hand can drag the controller overlay");
				ImGui::Spacing();
				ImGui::TextWrapped("Depth Adjustment (Grip + Thumbstick):");
				ImGui::BulletText("While gripping to drag, use the thumbstick on the same hand to adjust depth");
				ImGui::BulletText("Thumbstick forward: Push overlay farther away");
				ImGui::BulletText("Thumbstick back: Pull overlay closer");
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

	void DrawKeyBindings()
	{
		auto& vr = globals::features::vr;
		auto& settings = vr.settings;

		if (ImGui::CollapsingHeader("Combo Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::SliderFloat("Combo Timeout", &settings.comboTimeout, 1.0f, 10.0f, "%.1f seconds");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Time limit for recording button combinations.");
			}
		}
		ImGui::Separator();
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
			vr.isCapturingCombo = false;
			vr.currentComboType = VR::ComboType::None;
			vr.recordedCombo.clear();
		}
		if (ImGui::Button("Record Selected Combo")) {
			vr.isCapturingCombo = true;
			vr.currentComboType = static_cast<VR::ComboType>(selectedComboIndex + 1);
			vr.currentComboName = comboTypes[selectedComboIndex];
			vr.recordedCombo.clear();
			vr.comboStartTime = Util::GetNowSecs();
			vr.recordingButtonControllers.clear();
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Clear")) {
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
		if (ImGui::BeginTable("##VRBindingsTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn("Action");
			ImGui::TableSetupColumn("Current Binding");
			ImGui::TableSetupColumn("Description");
			ImGui::TableHeadersRow();
			struct VRKeyBindingConfig
			{
				const char* label;
				std::vector<InputCombo>& combos;
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
				if (row == static_cast<size_t>(selectedComboIndex)) {
					ImU32 highlight = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 0.0f, 0.15f));
					ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, highlight);
				}
				ImGui::TableSetColumnIndex(0);
				char selectableId[64];
				snprintf(selectableId, sizeof(selectableId), "##combo_row_%zu", row);
				bool rowSelected = (row == static_cast<size_t>(selectedComboIndex));
				if (ImGui::Selectable(selectableId, rowSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, ImVec2(0, 0))) {
					selectedComboIndex = static_cast<int>(row);
				}
				ImGui::SameLine(0, 0);
				ImGui::Text("%s", config.label);
				ImGui::TableSetColumnIndex(1);
				Util::DrawButtonCombo(config.combos, false);
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%s", config.description);
			}
			ImGui::EndTable();
		}
		ImGui::Spacing();
		if (ImGui::Button("Reset to Defaults")) {
			VR::Settings defaults;
			settings.VRMenuOpenKeys = defaults.VRMenuOpenKeys;
			settings.VRMenuCloseKeys = defaults.VRMenuCloseKeys;
			settings.VROverlayOpenKeys = defaults.VROverlayOpenKeys;
			settings.VROverlayCloseKeys = defaults.VROverlayCloseKeys;
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Reset all VR key bindings to their default values.");
		}
	}

	void DrawThumbstickColumn(VR& vr, bool showPrimary, ImU32 highlightCol)
	{
		auto& state = showPrimary ? vr.primaryControllerState : vr.secondaryControllerState;
		auto role = showPrimary ? RE::ControllerRole::Primary : RE::ControllerRole::Secondary;
		float x = state.thumbsticks[static_cast<size_t>(role)].x;
		float y = state.thumbsticks[static_cast<size_t>(role)].y;

		ImVec2 padSize = ImVec2(80, 80);
		ImVec2 cursor = ImGui::GetCursorScreenPos();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		ImVec2 center = ImVec2(cursor.x + padSize.x / 2, cursor.y + padSize.y / 2);
		float radius = padSize.x / 2 - 4;
		ImU32 borderCol = ImGui::GetColorU32(ImGuiCol_Border);
		ImU32 axisCol = ImGui::GetColorU32(ImGuiCol_TextDisabled);
		ImU32 dotCol = ImGui::GetColorU32(ImGuiCol_Text);

		drawList->AddRectFilled(cursor, ImVec2(cursor.x + padSize.x, cursor.y + padSize.y), ImGui::GetColorU32(ImGuiCol_FrameBg));
		drawList->AddRect(cursor, ImVec2(cursor.x + padSize.x, cursor.y + padSize.y), borderCol, 4.0f, 0, 2.0f);
		drawList->AddLine(ImVec2(center.x, cursor.y + 4), ImVec2(center.x, cursor.y + padSize.y - 4), axisCol, 1.0f);
		drawList->AddLine(ImVec2(cursor.x + 4, center.y), ImVec2(cursor.x + padSize.x - 4, center.y), axisCol, 1.0f);

		int quad = 0;
		if (x > 0 && y > 0)
			quad = 1;
		else if (x < 0 && y > 0)
			quad = 2;
		else if (x < 0 && y < 0)
			quad = 3;
		else if (x > 0 && y < 0)
			quad = 4;

		if (quad != 0) {
			ImVec2 q0 = center, q1 = center, q2 = center, q3 = center;
			if (quad == 1) {
				q1 = { center.x + radius, center.y - radius };
				q2 = { center.x + radius, center.y };
				q3 = { center.x, center.y - radius };
			} else if (quad == 2) {
				q1 = { center.x - radius, center.y - radius };
				q2 = { center.x - radius, center.y };
				q3 = { center.x, center.y - radius };
			} else if (quad == 3) {
				q1 = { center.x - radius, center.y + radius };
				q2 = { center.x - radius, center.y };
				q3 = { center.x, center.y + radius };
			} else if (quad == 4) {
				q1 = { center.x + radius, center.y + radius };
				q2 = { center.x + radius, center.y };
				q3 = { center.x, center.y + radius };
			}
			ImVec2 poly[4] = { center, q1, q2, q3 };
			drawList->AddConvexPolyFilled(poly, 4, highlightCol);
		}

		ImVec2 dot = ImVec2(center.x + x * radius, center.y - y * radius);
		drawList->AddCircleFilled(dot, 5.0f, dotCol);

		ImGui::Dummy(padSize);
		ImGui::SetNextItemWidth(160.0f);
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight());
		ImGui::Text("X: %+1.3f  Y: %+1.3f  [%s]", x, y, RE::GetQuadrantName(x, y));
	}

	void DrawDebugSection()
	{
		auto& vr = globals::features::vr;
		auto& settings = vr.settings;
		auto menu = globals::menu;

		if (ImGui::CollapsingHeader("OpenVR Information", ImGuiTreeNodeFlags_DefaultOpen)) {
			auto& info = vr.openVRInfo;
			if (info.isAvailable) {
				if (vr.IsOpenVRCompatible()) {
					ImGui::Text("OpenVR System: Active & Compatible");
				} else {
					ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "OpenVR System: Active but INCOMPATIBLE");
					ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "VR overlay menus disabled.");
				}

				ImGui::Text("Runtime: %s", VRDetection::RuntimeTypeToString(info.runtimeType));
				ImGui::Text("DLL Path: %s", info.dllPath.c_str());
				ImGui::Text("DLL Version: %s", info.version.c_str());
				ImGui::Text("DLL Size: %llu bytes", info.fileSize);
				ImGui::Text("Modified: %s", info.modificationTime.c_str());

				ImGui::Separator();
				ImGui::Text("Detection Method:");
				ImGui::Text("  Interface Probing: %s", info.probingSucceeded ? "Passed" : "Failed");
				ImGui::Text("    IVROverlay_016: %s", info.hasOverlayInterface ? "OK" : "Missing");
				ImGui::Text("    IVRSystem_017: %s", info.hasSystemInterface ? "OK" : "Missing");
				ImGui::Text("    IVRCompositor_021: %s", info.hasCompositorInterface ? "OK" : "Missing");
				ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "  Rendering: In-scene overlay (submit hook)");

			} else {
				ImGui::Text("OpenVR system not available");
			}
		}

		if (ImGui::CollapsingHeader("Controller Diagnostics", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (ImGui::Checkbox("Test Mode: Disable controller menu input (except scroll controller and triggers)", &settings.VRMenuControllerDiagnosticsTestMode)) {
				ImGui::SetScrollHereY(0.0f);
			}
			ImGui::SeparatorText("Button State");
			double nowSecs = Util::GetNowSecs();
			ImVec4 highlightColor = menu->GetTheme().StatusPalette.InfoColor;
			ImU32 highlightColorU32 = ImGui::ColorConvertFloat4ToU32(highlightColor);

			bool isLeftHanded = vr.lastKnownLeftHandedMode;

			if (ImGui::BeginTable("vr_input_state_table", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
				ImGui::TableSetupColumn("Button");
				if (isLeftHanded) {
					ImGui::TableSetupColumn("Primary State");
					ImGui::TableSetupColumn("Primary Held (s)");
					ImGui::TableSetupColumn("Primary Type");
					ImGui::TableSetupColumn("Secondary State");
					ImGui::TableSetupColumn("Secondary Held (s)");
					ImGui::TableSetupColumn("Secondary Type");
				} else {
					ImGui::TableSetupColumn("Secondary State");
					ImGui::TableSetupColumn("Secondary Held (s)");
					ImGui::TableSetupColumn("Secondary Type");
					ImGui::TableSetupColumn("Primary State");
					ImGui::TableSetupColumn("Primary Held (s)");
					ImGui::TableSetupColumn("Primary Type");
				}
				ImGui::TableHeadersRow();

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

				auto printRowWithHandedness = [&](const char* label, auto key) {
					auto& primary = vr.primaryControllerState[key];
					auto& secondary = vr.secondaryControllerState[key];
					if (isLeftHanded) {
						printRow(label, primary, secondary);
					} else {
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
			ImU32 highlightCol = ImGui::ColorConvertFloat4ToU32(menu->GetTheme().StatusPalette.InfoColor);
			if (ImGui::BeginTable("##VRThumbstickTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
				if (isLeftHanded) {
					ImGui::TableSetupColumn("Primary Controller", ImGuiTableColumnFlags_WidthFixed, 200.0f);
					ImGui::TableSetupColumn("Secondary Controller", ImGuiTableColumnFlags_WidthFixed, 200.0f);
				} else {
					ImGui::TableSetupColumn("Secondary Controller", ImGuiTableColumnFlags_WidthFixed, 200.0f);
					ImGui::TableSetupColumn("Primary Controller", ImGuiTableColumnFlags_WidthFixed, 200.0f);
				}
				ImGui::TableHeadersRow();

				// Left column
				ImGui::TableSetColumnIndex(0);
				ImGui::BeginGroup();
				DrawThumbstickColumn(vr, isLeftHanded, highlightCol);
				ImGui::EndGroup();

				// Right column
				ImGui::TableSetColumnIndex(1);
				ImGui::BeginGroup();
				DrawThumbstickColumn(vr, !isLeftHanded, highlightCol);
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
				for (const auto& e : vr.vrControllerEventLog) {
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

			ImGui::SeparatorText("Wand Pointing State");
			if (ImGui::BeginTable("##WandPointingState", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
				ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 200.0f);
				ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Wand Pointing Enabled");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%s", settings.EnableWandPointing ? "Yes" : "No");

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Intersecting Overlay");
				ImGui::TableSetColumnIndex(1);
				if (vr.wandState.isIntersecting) {
					ImGui::TextColored(menu->GetTheme().StatusPalette.InfoColor, "YES");
				} else {
					ImGui::Text("No");
				}

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("UV Coordinates");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("(%.3f, %.3f)", vr.wandState.uvCoordinates.x, vr.wandState.uvCoordinates.y);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Controller Index");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%u", vr.wandState.controllerIndex);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Ray Origin");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("(%.2f, %.2f, %.2f)", vr.wandState.rayOrigin.x, vr.wandState.rayOrigin.y, vr.wandState.rayOrigin.z);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Ray Direction");
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("(%.2f, %.2f, %.2f)", vr.wandState.rayDirection.x, vr.wandState.rayDirection.y, vr.wandState.rayDirection.z);

				ImGui::EndTable();
			}
		}

		if (ImGui::CollapsingHeader("OpenVR Addresses")) {
			auto openvr = RE::BSOpenVR::GetSingleton();
			auto overlay = openvr ? RE::BSOpenVR::GetIVROverlayFromContext(&openvr->vrContext) : nullptr;
			auto vrSystem = openvr ? openvr->vrSystem : nullptr;
			ADDRESS_NODE(openvr)
			ADDRESS_NODE(overlay)
			ADDRESS_NODE(vrSystem)
		}
	}
}  // namespace

//=============================================================================
// DRAW SETTINGS (main entry point)
//=============================================================================

void VR::DrawSettings()
{
	auto menu = globals::menu;
	if (!menu)
		return;
	if (ImGui::BeginTabBar("##VRTabs", ImGuiTabBarFlags_None)) {
		if (BeginTabItemWithFont("General", Menu::FontRole::Subheading)) {
			if (ImGui::BeginChild("##VRGeneralFrame", { 0, 0 }, true)) {
				DrawGeneralVRSettings();
				DrawControllerInputInstructions();
				DrawMenuSettings();
				DrawMouseSettings();
				DrawDragSettings();
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		if (BeginTabItemWithFont("Stereo", Menu::FontRole::Subheading)) {
			if (ImGui::BeginChild("##VRStereoFrame", { 0, 0 }, true)) {
				DrawStereoSettings();
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		if (IsOpenVRCompatible()) {
			if (BeginTabItemWithFont("Bindings", Menu::FontRole::Subheading)) {
				if (ImGui::BeginChild("##VRBindingsFrame", { 0, 0 }, true)) {
					DrawKeyBindings();
				}
				ImGui::EndChild();
				ImGui::EndTabItem();
			}
		}

		if (BeginTabItemWithFont("Debug", Menu::FontRole::Subheading)) {
			if (ImGui::BeginChild("##VRDebugFrame", { 0, 0 }, true)) {
				DrawDebugSection();
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	// Combo recording popup
	if (this->isCapturingCombo) {
		ImGui::OpenPopup("Record Combo");
		ImGui::SetNextWindowSize(ImVec2(400 * Util::GetUIScale(), 200 * Util::GetUIScale()), ImGuiCond_FirstUseEver);
		if (ImGui::BeginPopupModal("Record Combo", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
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

			ImGui::Text("Recording combo for: %s", this->currentComboName ? this->currentComboName : "Unknown");
			ImGui::Spacing();

			ImGui::TextDisabled("(During recording, any controller's buttons can be used. Requirement is only enforced during use.)");

			ImGui::Spacing();

			double remainingTime = settings.comboTimeout - (Util::GetNowSecs() - this->comboStartTime);
			ImVec4 timerColor = remainingTime > 2.0 ? Util::Colors::GetTimerGood() :
			                    remainingTime > 1.0 ? Util::Colors::GetTimerWarning() :
			                                          Util::Colors::GetTimerCritical();
			ImGui::TextColored(timerColor, "Time remaining: %.1f seconds", remainingTime);

			ImGui::Spacing();

			if (this->recordedCombo.empty()) {
				ImGui::Text("Press buttons to record combo...");
			} else {
				ImGui::Text("Recorded buttons:");
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

			ImGui::Text("Press ENTER to accept, ESC to cancel");

			// Handle button recording
			bool buttonPressed = false;
			uint32_t pressedKey = 0;
			ControllerDevice pressedDevice = ControllerDevice::Both;

			for (const auto& [keyCode, buttonState] : primaryControllerState.GetActiveButtons()) {
				if (buttonState->isPressed) {
					pressedKey = keyCode;
					buttonPressed = true;
					pressedDevice = ControllerDevice::Primary;
					break;
				}
			}

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

			if (buttonPressed) {
				auto it = recordingButtonControllers.find(pressedKey);
				if (it == recordingButtonControllers.end()) {
					recordingButtonControllers[pressedKey] = pressedDevice;
				} else {
					if (it->second != pressedDevice && it->second != ControllerDevice::Both) {
						it->second = ControllerDevice::Both;
					}
				}
				this->recordedCombo.clear();
				for (const auto& [key, device] : recordingButtonControllers) {
					this->recordedCombo.push_back(ButtonCombo(device, key));
				}
			}

			if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
				ApplyRecordedCombo();
				ResetComboRecording();
				ImGui::CloseCurrentPopup();
			}

			if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
				ResetComboRecording();
				ImGui::CloseCurrentPopup();
			}

			if (remainingTime <= 0.0) {
				ApplyRecordedCombo();
				ResetComboRecording();
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
	}
}
