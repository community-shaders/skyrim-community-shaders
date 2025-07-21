#include "VR.h"
#include "Menu.h"
#include "RE/B/BSOpenVR.h"
#include <openvr.h>

#include "DX12SwapChain.h"
#include "State.h"
#include "Utils/UI.h"
#include <DirectXMath.h>
#include <SimpleMath.h>
#include <cmath>
#include <d3d11.h>
#include <imgui_impl_dx11.h>
#include <magic_enum.hpp>
#include <processthreadsapi.h>  // For GetCurrentProcessId on Windows
#include <unordered_map>
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
	dragHighlightColor,
	VRMenuOpenKeys,
	VRMenuCloseKeys,
	VROverlayOpenKeys,
	VROverlayCloseKeys,
	comboTimeout,
	EnableDragToReposition,
	ShowHowToUseMessage)

// A "clean" IVROverlay interface, loaded separately from the game's context.
static vr::IVROverlay* g_cleanOverlay = nullptr;

// Helper to acquire the clean interface
void InitCleanOverlay()
{
	// Define the function pointer type locally, as it seems to be missing from the project's openvr.h
	typedef void* (*pfnVRGetGenericInterface)(const char* pchInterfaceVersion, vr::EVRInitError* peError);

	HMODULE openvr_handle = GetModuleHandleA("openvr_api.dll");
	if (!openvr_handle) {
		logger::error("VR: Failed to get openvr_api.dll handle");
		return;
	}

	auto VR_GetGenericInterface = (pfnVRGetGenericInterface)GetProcAddress(openvr_handle, "VR_GetGenericInterface");
	if (!VR_GetGenericInterface) {
		logger::error("VR: Failed to get address of VR_GetGenericInterface");
		return;
	}

	vr::EVRInitError eError = vr::VRInitError_None;
	g_cleanOverlay = (vr::IVROverlay*)VR_GetGenericInterface(vr::IVROverlay_Version, &eError);

	if (eError != vr::VRInitError_None) {
		g_cleanOverlay = nullptr;
		logger::error("VR: Failed to get clean IVROverlay interface: {} ({})", static_cast<int>(eError), magic_enum::enum_name(eError));
		return;
	}
	logger::info("VR: Successfully acquired clean IVROverlay interface: 0x{:X}", reinterpret_cast<uintptr_t>(g_cleanOverlay));
}

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
	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	if (openvr) {
		auto* system = openvr->vrSystem;
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
	}
	return transform;
}

void DrawButtonCombo(const std::vector<ButtonCombo>& combo, bool showControllerLabels = false)
{
	bool anyDrawn = false;
	for (size_t i = 0; i < combo.size(); ++i) {
		if (combo[i].GetKey() == 0)
			continue;
		if (i > 0) {
			ImGui::SameLine();
			ImGui::Text("+");
			ImGui::SameLine();
		}
		ImVec4 color;
		switch (combo[i].GetDevice()) {
		case ControllerDevice::Primary:
			color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
			break;
		case ControllerDevice::Secondary:
			color = ImVec4(0.0f, 0.6f, 1.0f, 1.0f);
			break;
		case ControllerDevice::Both:
			color = ImVec4(0.5f, 0.0f, 0.5f, 1.0f);
			break;
		default:
			color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
			break;
		}
		ImGui::PushStyleColor(ImGuiCol_Text, color);
		ImGui::Text("%s", RE::GetOpenVRButtonName(combo[i].GetKey()));
		ImGui::PopStyleColor();
		anyDrawn = true;
		if (showControllerLabels) {
			ImGui::SameLine();
			const char* label = "";
			switch (combo[i].GetDevice()) {
			case ControllerDevice::Primary:
				label = "(Primary)";
				break;
			case ControllerDevice::Secondary:
				label = "(Secondary)";
				break;
			case ControllerDevice::Both:
				label = "(Both)";
				break;
			default:
				break;
			}
			ImGui::TextDisabled("%s", label);
			if (i < combo.size() - 1)
				ImGui::SameLine();
		}
	}
	if (anyDrawn) {
		if (auto _tt = Util::HoverTooltipWrapper()) {
			Util::DrawColoredMultiLineTooltip({ { "Color coding:", ImVec4(1, 1, 1, 1) },
				{ "Green = Primary controller", ImVec4(0.0f, 1.0f, 0.0f, 1.0f) },
				{ "Blue = Secondary controller", ImVec4(0.0f, 0.6f, 1.0f, 1.0f) },
				{ "Purple = Both controllers", ImVec4(0.5f, 0.0f, 0.5f, 1.0f) } });
		}
	}
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
	DrawButtonCombo(settings.VRMenuOpenKeys, true);
	ImGui::Text("\nClose Menu: ");
	DrawButtonCombo(settings.VRMenuCloseKeys, true);
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

	// Combo recording popup (moved outside tabs)
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
			double remainingTime = this->comboTimeout - (GetNowSecs() - this->comboStartTime);
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

				DrawButtonCombo(sortedRecordedCombos, false);
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
			for (const auto& [keyCode, buttonState] : primaryControllerState.buttons) {
				if (buttonState.isPressed) {
					pressedKey = keyCode;
					buttonPressed = true;
					pressedDevice = ControllerDevice::Primary;
					break;
				}
			}

			// Check secondary controller buttons if primary didn't have any
			if (!buttonPressed) {
				for (const auto& [keyCode, buttonState] : secondaryControllerState.buttons) {
					if (buttonState.isPressed) {
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
			ImGui::BulletText("Secondary Controller Thumbstick: Mouse movement");
			ImGui::BulletText("Primary Controller Thumbstick: Scroll");
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
			ImGui::SliderFloat("Menu Distance", &settings.VRMenuDistance, 0.5f, 5.0f, "%.1f");
			ImGui::SliderFloat("Menu Scale", &settings.VRMenuScale, 0.5f, 2.0f, "%.2f");
			const char* positioningMethods[] = { "HMD Relative", "Fixed World Position" };
			ImGui::Combo("Menu Positioning Method", &settings.VRMenuPositioningMethod, positioningMethods, IM_ARRAYSIZE(positioningMethods));
			const char* attachModes[] = { "HMD Only", "Controller Only", "Both" };
			int attachModeInt = static_cast<int>(settings.attachMode);
			if (ImGui::Combo("Attach Mode", &attachModeInt, attachModes, IM_ARRAYSIZE(attachModes))) {
				settings.attachMode = static_cast<VR::Settings::OverlayAttachMode>(attachModeInt);
			}
			const char* controllerHands[] = { "Left", "Right" };
			int controllerHandInt = static_cast<int>(settings.VRMenuControllerHand);
			if (ImGui::Combo("Menu Controller Hand", &controllerHandInt, controllerHands, IM_ARRAYSIZE(controllerHands))) {
				settings.VRMenuControllerHand = static_cast<vr::ETrackedControllerRole>(controllerHandInt);
			}
			ImGui::SliderFloat("Menu Offset X", &settings.VRMenuOffsetX, -2.0f, 2.0f, "%.2f");
			ImGui::SliderFloat("Menu Offset Y", &settings.VRMenuOffsetY, -2.0f, 2.0f, "%.2f");
			ImGui::SliderFloat("Menu Offset Z", &settings.VRMenuOffsetZ, -2.0f, 2.0f, "%.2f");
			ImGui::SliderFloat("Controller Offset X", &settings.VRMenuControllerOffsetX, -0.5f, 0.5f, "%.2f");
			ImGui::SliderFloat("Controller Offset Y", &settings.VRMenuControllerOffsetY, -0.5f, 0.5f, "%.2f");
			ImGui::SliderFloat("Controller Offset Z", &settings.VRMenuControllerOffsetZ, -0.5f, 0.5f, "%.2f");
		}
	}
	void DrawMouseSettings(VR::Settings& settings)
	{
		if (ImGui::CollapsingHeader("Mouse Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::SliderFloat("Mouse Deadzone", &settings.mouseDeadzone, 0.0f, 0.5f, "%.2f");
			ImGui::SliderFloat("Mouse Speed", &settings.mouseSpeed, 0.1f, 5.0f, "%.2f");
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
			vr->comboStartTime = GetNowSecs();
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
				DrawButtonCombo(config.combos, false);
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
				printRow("Trigger", vr->primaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kTrigger], vr->secondaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kTrigger]);
				printRow("Grip", vr->primaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kGrip], vr->secondaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kGrip]);
				printRow("GripAlt", vr->primaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kGripAlt], vr->secondaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kGripAlt]);
				printRow("Stick Click", vr->primaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kJoystickTrigger], vr->secondaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kJoystickTrigger]);
				printRow("Touchpad Click", vr->primaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kTouchpadClick], vr->secondaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kTouchpadClick]);
				printRow("Touchpad Alt", vr->primaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kTouchpadAlt], vr->secondaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kTouchpadAlt]);
				printRow("B/Y", vr->primaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kBY], vr->secondaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kBY]);
				printRow("A/X", vr->primaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kXA], vr->secondaryControllerState.buttons[RE::BSOpenVRControllerDevice::Keys::kXA]);
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
				ImGui::TableHeadersRow();
				// Primary controller cell
				ImGui::TableSetColumnIndex(0);
				ImGui::BeginGroup();
				ImVec2 padSizeL = DrawThumbstickPad(vr->primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x, vr->primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y, highlightCol);
				ImGui::Dummy(padSizeL);
				ImGui::SetNextItemWidth(160.0f);
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight());
				ImGui::Text("X: %+1.3f  Y: %+1.3f  [%s]", vr->primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x, vr->primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y, RE::GetQuadrantName(vr->primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x, vr->primaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y));
				ImGui::EndGroup();
				// Secondary controller cell
				ImGui::TableSetColumnIndex(1);
				ImGui::BeginGroup();
				ImVec2 padSizeR = DrawThumbstickPad(vr->secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x, vr->secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y, highlightCol);
				ImGui::Dummy(padSizeR);
				ImGui::SetNextItemWidth(160.0f);
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight());
				ImGui::Text("X: %+1.3f  Y: %+1.3f  [%s]", vr->secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x, vr->secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y, RE::GetQuadrantName(vr->secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].x, vr->secondaryControllerState.thumbsticks[static_cast<size_t>(RE::ControllerRole::Primary)].y));
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
	// One-time initialization of the clean overlay interface
	if (!g_cleanOverlay) {
		InitCleanOverlay();
	}

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
	logger::info("Current PID: {}", GetCurrentProcessId());
	if (!overlay) {
		logger::error("IVROverlay is nullptr after GetIVROverlay");
		return;
	}
	CreateOverlayTextureAndRTV(globals::d3d::device, kOverlayWidth, kOverlayHeight, &menuTexture, &menuRTV);
	std::string key = "communityshaders.menu";
	std::string name = "Community Shaders Menu";
	vr::EVROverlayError err = overlay->CreateOverlay(key.c_str(), name.c_str(), &menuOverlayHandle);
	if (err == vr::VROverlayError_None) {
		logger::info("CreateOverlay succeeded for menuOverlayHandle: 0x{:X}", menuOverlayHandle);
		SetOverlayInputFlags(overlay, menuOverlayHandle);
		overlay->SetOverlayWidthInMeters(menuOverlayHandle, 1.0f);
		overlay->SetOverlayRenderingPid(menuOverlayHandle, GetCurrentProcessId());
		uint32_t pid = overlay->GetOverlayRenderingPid(menuOverlayHandle);
		logger::info("menuOverlayHandle rendering PID: {}", pid);
	} else {
		logger::error("CreateOverlay failed: {} ({})", static_cast<int>(err), magic_enum::enum_name(err));
	}
	// Controller overlay
	std::string controllerKey = "communityshaders.menu.controller";
	std::string controllerName = "Community Shaders Menu (Controller)";
	err = overlay->CreateOverlay(controllerKey.c_str(), controllerName.c_str(), &menuControllerOverlayHandle);
	if (err == vr::VROverlayError_None) {
		logger::info("CreateOverlay succeeded for menuControllerOverlayHandle: 0x{:X}", menuControllerOverlayHandle);
		CreateOverlayTextureAndRTV(globals::d3d::device, kOverlayWidth, kOverlayHeight, &menuControllerTexture, &menuControllerRTV);
		SetOverlayInputFlags(overlay, menuControllerOverlayHandle);
		overlay->SetOverlayWidthInMeters(menuControllerOverlayHandle, 1.0f);
		overlay->SetOverlayRenderingPid(menuControllerOverlayHandle, GetCurrentProcessId());
		uint32_t pid = overlay->GetOverlayRenderingPid(menuControllerOverlayHandle);
		logger::info("menuControllerOverlayHandle rendering PID: {}", pid);
	} else {
		logger::error("CreateOverlay failed: {} ({})", static_cast<int>(err), magic_enum::enum_name(err));
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
	RE::BSOpenVR* openvr = RE::BSOpenVR::GetSingleton();
	auto* gameOverlay = openvr ? RE::BSOpenVR::GetIVROverlayFromContext(&openvr->vrContext) : nullptr;

	if (!gameOverlay || !g_cleanOverlay) {
		return;  // Not ready yet
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
		ApplyHighlightTintToTexture(menuTexture, hmdBeingDragged);

		// Update overlay position and submit to SteamVR
		UpdateVROverlayPosition();
		vr::Texture_t tex = { menuTexture, vr::TextureType_DirectX, vr::ColorSpace_Auto };
		if (settings.attachMode == AttachMode::HMDOnly || settings.attachMode == AttachMode::Both) {
			SetOverlayInputFlags(gameOverlay, menuOverlayHandle);
			vr::EVROverlayError err = g_cleanOverlay->SetOverlayTexture(menuOverlayHandle, &tex);
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
			ApplyHighlightTintToTexture(menuControllerTexture, controllerBeingDragged);

			// Position controller overlay and submit
			UpdateVROverlayControllerPosition();

			vr::Texture_t controllerTex = { menuControllerTexture, vr::TextureType_DirectX, vr::ColorSpace_Auto };
			SetOverlayInputFlags(gameOverlay, menuControllerOverlayHandle);
			vr::EVROverlayError err = g_cleanOverlay->SetOverlayTexture(menuControllerOverlayHandle, &controllerTex);
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
				buttonPressed = primaryControllerState.buttons[combo.GetKey()].isPressed &&
				                secondaryControllerState.buttons[combo.GetKey()].isPressed;
				break;
			case ControllerDevice::Primary:
				// Check if this button is pressed on PRIMARY controller only
				buttonPressed = primaryControllerState.buttons[combo.GetKey()].isPressed;
				break;
			case ControllerDevice::Secondary:
				// Check if this button is pressed on SECONDARY controller only
				buttonPressed = secondaryControllerState.buttons[combo.GetKey()].isPressed;
				break;
			}

			if (!buttonPressed) {
				return false;  // Any button not pressed means combo fails
			}
		}

		// All configured buttons are pressed according to requirements
		return true;
	};

	// Define the mappings - restore original 4 distinct events with extensible lambda array
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
	// Disable menu interactions during combo recording
	if (this->isCapturingCombo) {
		return;
	}

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
static bool CanStartAny(vr::ETrackedControllerRole, bool isLeft, bool isRight)
{
	return isLeft || isRight;
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
