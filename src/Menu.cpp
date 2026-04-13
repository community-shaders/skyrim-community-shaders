#include "Menu.h"

#ifndef DIRECTINPUT_VERSION
#	define DIRECTINPUT_VERSION 0x0800
#endif
#include <algorithm>
#include <dinput.h>
#include <dxgi.h>
#include <format>
#include <functional>
#include <imgui.h>
#include <ranges>
#include <variant>
#include <imgui_impl_dx11.h>
#include <imgui_internal.h>

#include "Feature.h"
#include "FeatureIssues.h"
#include "FeatureVersions.h"
#include "Globals.h"
#include "ShaderCache.h"
#include "State.h"
#include "TruePBR.h"
#include "Util.h"
#include "Utils/UI.h"

#include "Features/PerformanceOverlay.h"
#include "Features/Upscaling.h"
#include "Features/PerformanceOverlay/ABTesting/ABTestAggregator.h"
#include "Features/PerformanceOverlay/ABTesting/ABTesting.h"
#include "Features/RenderDoc.h"
#include "Features/VR.h"
#include "Features/WeatherEditor.h"
#include "WeatherEditor/EditorWindow.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Menu::Settings,
	ToggleKey,
	SkipCompilationKey,
	EffectToggleKey,
	OverlayToggleKey,
	ShaderBlockPrevKey,
	ShaderBlockNextKey,
	WeatherEditorToggleKey,
	EnableShaderBlocking,
	UIScale)

bool IsEnabled = false;

Menu::~Menu()
{
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

void Menu::Load(json& o_json)
{
	settings = o_json;

	// Migration: Convert legacy uint32_t keys to InputCombo vectors if needed
	auto migrateKey = [](json& j, const char* keyName, std::vector<InputCombo>& target) {
		if (j.contains(keyName) && j[keyName].is_number_integer()) {
			uint32_t legacyKey = j[keyName].get<uint32_t>();
			target.clear();
			if (legacyKey != 0) {
				target.push_back(InputCombo::Keyboard(legacyKey));
			}
		}
	};

	migrateKey(o_json, "ToggleKey", settings.ToggleKey);
	migrateKey(o_json, "SkipCompilationKey", settings.SkipCompilationKey);
	migrateKey(o_json, "EffectToggleKey", settings.EffectToggleKey);
	migrateKey(o_json, "OverlayToggleKey", settings.OverlayToggleKey);
	migrateKey(o_json, "ShaderBlockPrevKey", settings.ShaderBlockPrevKey);
	migrateKey(o_json, "ShaderBlockNextKey", settings.ShaderBlockNextKey);
	migrateKey(o_json, "WeatherEditorToggleKey", settings.WeatherEditorToggleKey);

	auto loadComboList = [](const json& j, const char* keyName, std::vector<InputCombo>& target) {
		if (j.contains(keyName) && j[keyName].is_array()) {
			try {
				InputCombo::ComboList::from_json(j[keyName], target);
			} catch (const std::exception& e) {
				logger::warn("Failed to load combo list '{}': {}, using default", keyName, e.what());
			}
		}
	};

	loadComboList(o_json, "ToggleKey", settings.ToggleKey);
	loadComboList(o_json, "SkipCompilationKey", settings.SkipCompilationKey);
	loadComboList(o_json, "EffectToggleKey", settings.EffectToggleKey);
	loadComboList(o_json, "OverlayToggleKey", settings.OverlayToggleKey);
	loadComboList(o_json, "ShaderBlockPrevKey", settings.ShaderBlockPrevKey);
	loadComboList(o_json, "ShaderBlockNextKey", settings.ShaderBlockNextKey);
	loadComboList(o_json, "WeatherEditorToggleKey", settings.WeatherEditorToggleKey);
}

void Menu::Save(json& o_json)
{
	o_json = settings;

	InputCombo::ComboList::to_json(o_json["ToggleKey"], settings.ToggleKey);
	InputCombo::ComboList::to_json(o_json["SkipCompilationKey"], settings.SkipCompilationKey);
	InputCombo::ComboList::to_json(o_json["EffectToggleKey"], settings.EffectToggleKey);
	InputCombo::ComboList::to_json(o_json["OverlayToggleKey"], settings.OverlayToggleKey);
	InputCombo::ComboList::to_json(o_json["ShaderBlockPrevKey"], settings.ShaderBlockPrevKey);
	InputCombo::ComboList::to_json(o_json["ShaderBlockNextKey"], settings.ShaderBlockNextKey);
	InputCombo::ComboList::to_json(o_json["WeatherEditorToggleKey"], settings.WeatherEditorToggleKey);
}

// ============================================================================
// Init - Ported from OAR UIManager::Init()
// ============================================================================

void Menu::Init()
{
	if (initialized) {
		return;
	}

	const auto renderManager = RE::BSGraphics::Renderer::GetSingleton();

	const auto device = reinterpret_cast<ID3D11Device*>(renderManager->GetRuntimeData().forwarder);
	const auto context = reinterpret_cast<ID3D11DeviceContext*>(renderManager->GetRuntimeData().context);
	const auto swapChain = reinterpret_cast<IDXGISwapChain*>(renderManager->GetRuntimeData().renderWindows[0].swapChain);

	DXGI_SWAP_CHAIN_DESC sd{};
	swapChain->GetDesc(&sd);

	// Calculate screen scale ratio (ported from OAR)
	RECT rect{};
	if (GetClientRect(sd.OutputWindow, &rect) == TRUE) {
		_userData.screenScaleRatio = { static_cast<float>(sd.BufferDesc.Width) / static_cast<float>(rect.right), static_cast<float>(sd.BufferDesc.Height) / static_cast<float>(rect.bottom) };
	} else {
		_userData.screenScaleRatio = { 1.0f, 1.0f };
	}

	ImGui::CreateContext();

	auto& io = ImGui::GetIO();

	io.DisplaySize = { static_cast<float>(sd.BufferDesc.Width), static_cast<float>(sd.BufferDesc.Height) };
	io.ConfigWindowsMoveFromTitleBarOnly = true;
	io.UserData = &_userData;
	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

	ImGui_ImplWin32_Init(sd.OutputWindow);
	ImGui_ImplDX11_Init(device, context);

	// Query DXGI adapter for GPU info
	{
		winrt::com_ptr<IDXGIDevice> dxgiDevice;
		if (!FAILED(device->QueryInterface(dxgiDevice.put()))) {
			winrt::com_ptr<IDXGIAdapter> dxgiAdapter;
			if (!FAILED(dxgiDevice->GetAdapter(dxgiAdapter.put()))) {
				dxgiAdapter->QueryInterface(dxgiAdapter3.put());
			}
		}
	}

	// Apply OAR-style dark theme
	ImGui::StyleColorsDark();
	auto& style = ImGui::GetStyle();
	style = GetDefaultStyle();

	initialized = true;
}

// ============================================================================
// OAR-style Default Style
// ============================================================================

ImGuiStyle Menu::GetDefaultStyle()
{
	ImGuiStyle style;
	ImGui::StyleColorsDark(&style);
	style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.65f);
	return style;
}

void Menu::UpdateStyle()
{
	const auto scale = settings.UIScale;
	if (scale != _prevScale) {
		auto& style = ImGui::GetStyle();
		style = GetDefaultStyle();
		ScaleAllSizes(style, scale);
		_prevScale = scale;

		auto& io = ImGui::GetIO();
		io.FontGlobalScale = scale;
	}
}

// ============================================================================
// ScaleAllSizes - Ported from OAR UICommon::ScaleAllSizes()
// ============================================================================

void Menu::ScaleAllSizes(ImGuiStyle& a_style, float a_scaleFactor)
{
	a_style.WindowPadding = ImFloor(a_style.WindowPadding * a_scaleFactor);
	a_style.WindowRounding = ImFloor(a_style.WindowRounding * a_scaleFactor);
	a_style.WindowMinSize = ImFloor(a_style.WindowMinSize * a_scaleFactor);
	a_style.ChildRounding = ImFloor(a_style.ChildRounding * a_scaleFactor);
	a_style.PopupRounding = ImFloor(a_style.PopupRounding * a_scaleFactor);
	a_style.FramePadding = ImFloor(a_style.FramePadding * a_scaleFactor);
	a_style.FrameRounding = ImFloor(a_style.FrameRounding * a_scaleFactor);
	a_style.ItemSpacing = ImFloor(a_style.ItemSpacing * a_scaleFactor);
	a_style.ItemInnerSpacing = ImFloor(a_style.ItemInnerSpacing * a_scaleFactor);
	a_style.CellPadding = ImFloor(a_style.CellPadding * a_scaleFactor);
	a_style.TouchExtraPadding = ImFloor(a_style.TouchExtraPadding * a_scaleFactor);
	a_style.IndentSpacing = ImFloor(a_style.IndentSpacing * a_scaleFactor);
	a_style.ColumnsMinSpacing = ImFloor(a_style.ColumnsMinSpacing * a_scaleFactor);
	a_style.ScrollbarSize = ImFloor(a_style.ScrollbarSize * a_scaleFactor);
	a_style.ScrollbarRounding = ImFloor(a_style.ScrollbarRounding * a_scaleFactor);
	a_style.GrabMinSize = ImFloor(a_style.GrabMinSize * a_scaleFactor);
	a_style.GrabRounding = ImFloor(a_style.GrabRounding * a_scaleFactor);
	a_style.LogSliderDeadzone = ImFloor(a_style.LogSliderDeadzone * a_scaleFactor);
	a_style.TabRounding = ImFloor(a_style.TabRounding * a_scaleFactor);
}

// ============================================================================
// Input Consumption - Ported from OAR UIManager
// ============================================================================

bool Menu::ShouldConsumeInput() const
{
	return _menusConsumingInput > 0;
}

void Menu::AddInputConsumer()
{
	if (++_menusConsumingInput > 0) {
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.MouseDrawCursor = true;
	}
}

void Menu::RemoveInputConsumer()
{
	if (--_menusConsumingInput == 0) {
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
		io.MouseDrawCursor = false;
	}
}

// ============================================================================
// VK Key Mapping - Ported from OAR UIManager
// ============================================================================

#define IM_VK_KEYPAD_ENTER (VK_RETURN + 256)

static ImGuiKey ImGui_ImplWin32_VirtualKeyToImGuiKey(WPARAM wParam)
{
	switch (wParam) {
	case VK_TAB:
		return ImGuiKey_Tab;
	case VK_LEFT:
		return ImGuiKey_LeftArrow;
	case VK_RIGHT:
		return ImGuiKey_RightArrow;
	case VK_UP:
		return ImGuiKey_UpArrow;
	case VK_DOWN:
		return ImGuiKey_DownArrow;
	case VK_PRIOR:
		return ImGuiKey_PageUp;
	case VK_NEXT:
		return ImGuiKey_PageDown;
	case VK_HOME:
		return ImGuiKey_Home;
	case VK_END:
		return ImGuiKey_End;
	case VK_INSERT:
		return ImGuiKey_Insert;
	case VK_DELETE:
		return ImGuiKey_Delete;
	case VK_BACK:
		return ImGuiKey_Backspace;
	case VK_SPACE:
		return ImGuiKey_Space;
	case VK_RETURN:
		return ImGuiKey_Enter;
	case VK_ESCAPE:
		return ImGuiKey_Escape;
	case VK_OEM_7:
		return ImGuiKey_Apostrophe;
	case VK_OEM_COMMA:
		return ImGuiKey_Comma;
	case VK_OEM_MINUS:
		return ImGuiKey_Minus;
	case VK_OEM_PERIOD:
		return ImGuiKey_Period;
	case VK_OEM_2:
		return ImGuiKey_Slash;
	case VK_OEM_1:
		return ImGuiKey_Semicolon;
	case VK_OEM_PLUS:
		return ImGuiKey_Equal;
	case VK_OEM_4:
		return ImGuiKey_LeftBracket;
	case VK_OEM_5:
		return ImGuiKey_Backslash;
	case VK_OEM_6:
		return ImGuiKey_RightBracket;
	case VK_OEM_3:
		return ImGuiKey_GraveAccent;
	case VK_CAPITAL:
		return ImGuiKey_CapsLock;
	case VK_SCROLL:
		return ImGuiKey_ScrollLock;
	case VK_NUMLOCK:
		return ImGuiKey_NumLock;
	case VK_SNAPSHOT:
		return ImGuiKey_PrintScreen;
	case VK_PAUSE:
		return ImGuiKey_Pause;
	case VK_NUMPAD0:
		return ImGuiKey_Keypad0;
	case VK_NUMPAD1:
		return ImGuiKey_Keypad1;
	case VK_NUMPAD2:
		return ImGuiKey_Keypad2;
	case VK_NUMPAD3:
		return ImGuiKey_Keypad3;
	case VK_NUMPAD4:
		return ImGuiKey_Keypad4;
	case VK_NUMPAD5:
		return ImGuiKey_Keypad5;
	case VK_NUMPAD6:
		return ImGuiKey_Keypad6;
	case VK_NUMPAD7:
		return ImGuiKey_Keypad7;
	case VK_NUMPAD8:
		return ImGuiKey_Keypad8;
	case VK_NUMPAD9:
		return ImGuiKey_Keypad9;
	case VK_DECIMAL:
		return ImGuiKey_KeypadDecimal;
	case VK_DIVIDE:
		return ImGuiKey_KeypadDivide;
	case VK_MULTIPLY:
		return ImGuiKey_KeypadMultiply;
	case VK_SUBTRACT:
		return ImGuiKey_KeypadSubtract;
	case VK_ADD:
		return ImGuiKey_KeypadAdd;
	case IM_VK_KEYPAD_ENTER:
		return ImGuiKey_KeypadEnter;
	case VK_LSHIFT:
		return ImGuiKey_LeftShift;
	case VK_LCONTROL:
		return ImGuiKey_LeftCtrl;
	case VK_LMENU:
		return ImGuiKey_LeftAlt;
	case VK_LWIN:
		return ImGuiKey_LeftSuper;
	case VK_RSHIFT:
		return ImGuiKey_RightShift;
	case VK_RCONTROL:
		return ImGuiKey_RightCtrl;
	case VK_RMENU:
		return ImGuiKey_RightAlt;
	case VK_RWIN:
		return ImGuiKey_RightSuper;
	case VK_APPS:
		return ImGuiKey_Menu;
	case '0':
		return ImGuiKey_0;
	case '1':
		return ImGuiKey_1;
	case '2':
		return ImGuiKey_2;
	case '3':
		return ImGuiKey_3;
	case '4':
		return ImGuiKey_4;
	case '5':
		return ImGuiKey_5;
	case '6':
		return ImGuiKey_6;
	case '7':
		return ImGuiKey_7;
	case '8':
		return ImGuiKey_8;
	case '9':
		return ImGuiKey_9;
	case 'A':
		return ImGuiKey_A;
	case 'B':
		return ImGuiKey_B;
	case 'C':
		return ImGuiKey_C;
	case 'D':
		return ImGuiKey_D;
	case 'E':
		return ImGuiKey_E;
	case 'F':
		return ImGuiKey_F;
	case 'G':
		return ImGuiKey_G;
	case 'H':
		return ImGuiKey_H;
	case 'I':
		return ImGuiKey_I;
	case 'J':
		return ImGuiKey_J;
	case 'K':
		return ImGuiKey_K;
	case 'L':
		return ImGuiKey_L;
	case 'M':
		return ImGuiKey_M;
	case 'N':
		return ImGuiKey_N;
	case 'O':
		return ImGuiKey_O;
	case 'P':
		return ImGuiKey_P;
	case 'Q':
		return ImGuiKey_Q;
	case 'R':
		return ImGuiKey_R;
	case 'S':
		return ImGuiKey_S;
	case 'T':
		return ImGuiKey_T;
	case 'U':
		return ImGuiKey_U;
	case 'V':
		return ImGuiKey_V;
	case 'W':
		return ImGuiKey_W;
	case 'X':
		return ImGuiKey_X;
	case 'Y':
		return ImGuiKey_Y;
	case 'Z':
		return ImGuiKey_Z;
	case VK_F1:
		return ImGuiKey_F1;
	case VK_F2:
		return ImGuiKey_F2;
	case VK_F3:
		return ImGuiKey_F3;
	case VK_F4:
		return ImGuiKey_F4;
	case VK_F5:
		return ImGuiKey_F5;
	case VK_F6:
		return ImGuiKey_F6;
	case VK_F7:
		return ImGuiKey_F7;
	case VK_F8:
		return ImGuiKey_F8;
	case VK_F9:
		return ImGuiKey_F9;
	case VK_F10:
		return ImGuiKey_F10;
	case VK_F11:
		return ImGuiKey_F11;
	case VK_F12:
		return ImGuiKey_F12;
	default:
		return ImGuiKey_None;
	}
}

// ============================================================================
// Input Processing - Ported from OAR, extended with CS hotkeys and VR
// ============================================================================

void Menu::ProcessInputEvents(RE::InputEvent* const* a_events)
{
	for (auto it = *a_events; it; it = it->next) {
		if (it->GetEventType() != RE::INPUT_EVENT_TYPE::kButton &&
			it->GetEventType() != RE::INPUT_EVENT_TYPE::kChar &&
			it->GetEventType() != RE::INPUT_EVENT_TYPE::kThumbstick)
			continue;

		if (it->GetEventType() == RE::INPUT_EVENT_TYPE::kButton) {
			addToEventQueue(KeyEvent(static_cast<RE::ButtonEvent*>(it)));
		} else if (it->GetEventType() == RE::INPUT_EVENT_TYPE::kChar) {
			addToEventQueue(KeyEvent(static_cast<CharEvent*>(it)));
		} else if (it->GetEventType() == RE::INPUT_EVENT_TYPE::kThumbstick) {
			addToEventQueue(KeyEvent(static_cast<RE::ThumbstickEvent*>(it)));
		}
	}
}

void Menu::addToEventQueue(KeyEvent e)
{
	std::unique_lock<std::shared_mutex> mutex(_inputEventMutex);
	_keyEventQueue.emplace_back(e);
}

void Menu::OnFocusChanged()
{
	if (const auto& inputMgr = RE::BSInputDeviceManager::GetSingleton()) {
		if (const auto& device = inputMgr->GetKeyboard()) {
			device->ClearInputState();
		}
	}
	ImGui::GetIO().ClearInputKeys();
}

void Menu::ProcessInputEventQueue()
{
	std::unique_lock<std::shared_mutex> mutex(_inputEventMutex);
	ImGuiIO& io = ImGui::GetIO();

	// Handle focus lost (ported from OAR)
	if (_bFocusLost) {
		io.AddFocusEvent(false);
		_keyEventQueue.clear();
		_bShiftHeld = false;
		_bCtrlHeld = false;
		_bAltHeld = false;
		_bFocusLost = false;
	}

	// Handle alt-tab focus change
	if (focusChanged) {
		OnFocusChanged();
		focusChanged = false;
	}

	// Split VR and non-VR events
	std::vector<KeyEvent> vrEvents;
	std::vector<KeyEvent> nonVREvents;
	for (auto& event : _keyEventQueue) {
		bool isVRController = ((event.device == RE::INPUT_DEVICE::kVivePrimary || event.device == RE::INPUT_DEVICE::kViveSecondary ||
								event.device == RE::INPUT_DEVICE::kOculusPrimary || event.device == RE::INPUT_DEVICE::kOculusSecondary ||
								event.device == RE::INPUT_DEVICE::kWMRPrimary || event.device == RE::INPUT_DEVICE::kWMRSecondary));

		if (globals::features::vr.IsOpenVRCompatible() && isVRController) {
			vrEvents.push_back(event);
		} else {
			nonVREvents.push_back(event);
		}
	}

	// Process VR events
	if (!vrEvents.empty()) {
		globals::features::vr.ProcessVREvents(vrEvents);
		globals::features::vr.UpdateOverlayMenuStateFromInput();
	}

	// Process non-VR events (OAR-style DIK→VK→ImGuiKey pipeline + CS hotkeys)
	for (auto& event : nonVREvents) {
		switch (event.eventType) {
		case RE::INPUT_EVENT_TYPE::kChar:
			{
				io.AddInputCharacter(event.keyCode);
				break;
			}
		case RE::INPUT_EVENT_TYPE::kButton:
			{
				// DIK to VK conversion (ported from OAR)
				uint32_t key = MapVirtualKeyEx(event.keyCode, MAPVK_VSC_TO_VK_EX, GetKeyboardLayout(0));
				switch (event.keyCode) {
				case DIK_LEFTARROW:
					key = VK_LEFT;
					break;
				case DIK_RIGHTARROW:
					key = VK_RIGHT;
					break;
				case DIK_UPARROW:
					key = VK_UP;
					break;
				case DIK_DOWNARROW:
					key = VK_DOWN;
					break;
				case DIK_DELETE:
					key = VK_DELETE;
					break;
				case DIK_END:
					key = VK_END;
					break;
				case DIK_HOME:
					key = VK_HOME;
					break;
				case DIK_PRIOR:
					key = VK_PRIOR;
					break;
				case DIK_NEXT:
					key = VK_NEXT;
					break;
				case DIK_INSERT:
					key = VK_INSERT;
					break;
				case DIK_NUMPAD0:
					key = VK_NUMPAD0;
					break;
				case DIK_NUMPAD1:
					key = VK_NUMPAD1;
					break;
				case DIK_NUMPAD2:
					key = VK_NUMPAD2;
					break;
				case DIK_NUMPAD3:
					key = VK_NUMPAD3;
					break;
				case DIK_NUMPAD4:
					key = VK_NUMPAD4;
					break;
				case DIK_NUMPAD5:
					key = VK_NUMPAD5;
					break;
				case DIK_NUMPAD6:
					key = VK_NUMPAD6;
					break;
				case DIK_NUMPAD7:
					key = VK_NUMPAD7;
					break;
				case DIK_NUMPAD8:
					key = VK_NUMPAD8;
					break;
				case DIK_NUMPAD9:
					key = VK_NUMPAD9;
					break;
				case DIK_DECIMAL:
					key = VK_DECIMAL;
					break;
				case DIK_NUMPADENTER:
					key = IM_VK_KEYPAD_ENTER;
					break;
				case DIK_RMENU:
					key = VK_RMENU;
					break;
				case DIK_RCONTROL:
					key = VK_RCONTROL;
					break;
				case DIK_LWIN:
					key = VK_LWIN;
					break;
				case DIK_RWIN:
					key = VK_RWIN;
					break;
				case DIK_APPS:
					key = VK_APPS;
					break;
				default:
					break;
				}

				const ImGuiKey imGuiKey = ImGui_ImplWin32_VirtualKeyToImGuiKey(key);

				// Track modifier state (ported from OAR)
				if (imGuiKey == ImGuiKey_LeftShift || imGuiKey == ImGuiKey_RightShift) {
					_bShiftHeld = event.IsPressed();
				} else if (imGuiKey == ImGuiKey_LeftCtrl || imGuiKey == ImGuiKey_RightCtrl) {
					_bCtrlHeld = event.IsPressed();
				} else if (imGuiKey == ImGuiKey_LeftAlt || imGuiKey == ImGuiKey_RightAlt) {
					_bAltHeld = event.IsPressed();
				}

				io.AddKeyEvent(ImGuiMod_Shift, _bShiftHeld);
				io.AddKeyEvent(ImGuiMod_Ctrl, _bCtrlHeld);
				io.AddKeyEvent(ImGuiMod_Alt, _bAltHeld);

				// Handle mouse events
				if (event.device == RE::INPUT_DEVICE::kMouse) {
					auto* ew = EditorWindow::GetSingleton();
					bool flying = ew && ew->IsPreviewFlying();
					if (event.keyCode > 7) {
						if (ew && ew->previewMode == EditorWindow::PreviewMode::FreeCamera) {
							ew->AdjustFlySpeed(event.keyCode == 8 ? 1.0f : -1.0f);
						} else if (!flying) {
							io.AddMouseWheelEvent(0, event.value * (event.keyCode == 8 ? 1 : -1));
						}
					} else if (!flying) {
						if (event.keyCode > 5)
							event.keyCode = 5;
						io.AddMouseButtonEvent(event.keyCode, event.IsPressed());
					}
				}

				// Handle keyboard events
				if (event.device == RE::INPUT_DEVICE::kKeyboard) {
					// Handle key release actions (CS hotkeys)
					if (!event.IsPressed()) {
						struct HotkeyAction
						{
							std::vector<InputCombo>* settingKey;
							bool* settingFlag;
							std::function<void(std::vector<InputCombo>)> action;
						};
						auto shaderCache = globals::shaderCache;
						HotkeyAction hotkeyActions[] = {
							{ &settings.ToggleKey, &settingToggleKey, [this](std::vector<InputCombo> keys) { settings.ToggleKey = keys; settingToggleKey = false; } },
							{ &settings.SkipCompilationKey, &settingSkipCompilationKey, [this](std::vector<InputCombo> keys) { settings.SkipCompilationKey = keys; settingSkipCompilationKey = false; } },
							{ &settings.EffectToggleKey, &settingsEffectsToggle, [this](std::vector<InputCombo> keys) { settings.EffectToggleKey = keys; settingsEffectsToggle = false; } },
							{ &settings.OverlayToggleKey, &settingOverlayToggleKey, [this](std::vector<InputCombo> keys) { settings.OverlayToggleKey = keys; settingOverlayToggleKey = false; } },
							{ &settings.ShaderBlockPrevKey, &settingShaderBlockPrevKey, [this](std::vector<InputCombo> keys) { settings.ShaderBlockPrevKey = keys; settingShaderBlockPrevKey = false; } },
							{ &settings.ShaderBlockNextKey, &settingShaderBlockNextKey, [this](std::vector<InputCombo> keys) { settings.ShaderBlockNextKey = keys; settingShaderBlockNextKey = false; } },
							{ &settings.WeatherEditorToggleKey, &settingWeatherEditorToggleKey, [this](std::vector<InputCombo> keys) { settings.WeatherEditorToggleKey = keys; settingWeatherEditorToggleKey = false; } },
						};
						bool handled = false;
						for (auto& h : hotkeyActions) {
							if (*(h.settingFlag)) {
								bool isModifier = (key == VK_CONTROL || key == VK_LCONTROL || key == VK_RCONTROL ||
												   key == VK_SHIFT || key == VK_LSHIFT || key == VK_RSHIFT ||
												   key == VK_MENU || key == VK_LMENU || key == VK_RMENU);
								if (isModifier) {
									handled = true;
									break;
								}

								std::vector<InputCombo> combo;
								if ((GetAsyncKeyState(VK_CONTROL) & Constants::KEY_PRESSED_MASK) &&
									key != VK_CONTROL && key != VK_LCONTROL && key != VK_RCONTROL)
									combo.push_back(InputCombo::Keyboard(VK_CONTROL));
								if ((GetAsyncKeyState(VK_SHIFT) & Constants::KEY_PRESSED_MASK) &&
									key != VK_SHIFT && key != VK_LSHIFT && key != VK_RSHIFT)
									combo.push_back(InputCombo::Keyboard(VK_SHIFT));
								if ((GetAsyncKeyState(VK_MENU) & Constants::KEY_PRESSED_MASK) &&
									key != VK_MENU && key != VK_LMENU && key != VK_RMENU)
									combo.push_back(InputCombo::Keyboard(VK_MENU));

								combo.push_back(InputCombo::Keyboard(key));
								h.action(combo);
								handled = true;
								break;
							}
						}
						if (!handled) {
							struct KeyAction
							{
								std::vector<InputCombo>& settingKey;
								std::function<void()> action;
							};
							KeyAction keyActions[] = {
								{ settings.ToggleKey, [this]() { IsEnabled = !IsEnabled; } },
								{ settings.SkipCompilationKey, [this, shaderCache]() { if (!ShouldSwallowInput() && shaderCache->IsCompiling()) shaderCache->backgroundCompilation = true; } },
								{ settings.EffectToggleKey, [shaderCache]() { shaderCache->SetEnabled(!shaderCache->IsEnabled()); } },
								{ settings.ShaderBlockPrevKey, [this, shaderCache]() { if (settings.EnableShaderBlocking) shaderCache->IterateShaderBlock(); } },
								{ settings.ShaderBlockNextKey, [this, shaderCache]() { if (settings.EnableShaderBlocking) shaderCache->IterateShaderBlock(false); } },
								{ settings.OverlayToggleKey, []() { Menu::GetSingleton()->overlayVisible = !Menu::GetSingleton()->overlayVisible; } },
								{ settings.WeatherEditorToggleKey, []() {
									 auto* ew = EditorWindow::GetSingleton();
									 if (!ew)
										 return;
									 if (ew->GetPreviewMode() == EditorWindow::PreviewMode::FreeCamera) {
										 ew->ToggleFreeCameraLock();
									 } else if (ew->IsInPreviewMode()) {
										 ew->ExitPreviewMode();
									 } else if (EditorWindow::CanBeOpen()) {
										 ew->open = !ew->open;
									 }
								 } },
							};
							for (const auto& ka : keyActions) {
								if (InputCombo::MatchesKeyboardCombo(ka.settingKey, key)) {
									ka.action();
									break;
								}
							}
						}

						// Handle ESC key for menu and editor window
						auto* editorWindow = EditorWindow::GetSingleton();
						if (key == VK_ESCAPE) {
							if (editorWindow && editorWindow->IsInPreviewMode()) {
								editorWindow->ExitPreviewMode();
							} else if (editorWindow && editorWindow->open && editorWindow->ShouldHandleEscapeKey()) {
								editorWindow->open = false;
							} else if (IsEnabled && (!editorWindow || !editorWindow->open)) {
								IsEnabled = false;
							}
						}
					}

					// Forward key events to ImGui (OAR pattern)
					const std::vector<InputCombo>* hotkeys[] = {
						&settings.ToggleKey, &settings.EffectToggleKey,
						&settings.OverlayToggleKey, &settings.ShaderBlockPrevKey, &settings.ShaderBlockNextKey,
						&settings.WeatherEditorToggleKey
					};
					bool isHotkey = ShouldSwallowInput() && std::any_of(std::begin(hotkeys), std::end(hotkeys),
														[key](const auto* combo) { return InputCombo::MatchesKeyboardCombo(*combo, key); });

					if (!isHotkey) {
						bool pressed = event.IsPressed() && (GetAsyncKeyState(key) & Constants::KEY_PRESSED_MASK);
						io.AddKeyEvent(imGuiKey, pressed);

						if (key == VK_LCONTROL || key == VK_RCONTROL)
							io.AddKeyEvent(ImGuiMod_Ctrl, pressed);
						else if (key == VK_LSHIFT || key == VK_RSHIFT)
							io.AddKeyEvent(ImGuiMod_Shift, pressed);
						else if (key == VK_LMENU || key == VK_RMENU)
							io.AddKeyEvent(ImGuiMod_Alt, pressed);
					}
				}
				break;
			}
		}
	}

	_keyEventQueue.clear();
}

// ============================================================================
// Render - Ported from OAR UIManager::Render(), extended with CS features
// ============================================================================

void Menu::DrawOverlay()
{
	if (!initialized) {
		return;
	}

	// VR setup
	if (globals::features::vr.IsOpenVRCompatible()) {
		globals::features::vr.RecreateOverlayTexturesIfNeeded();
	}

	ProcessInputEventQueue();

	if (globals::features::vr.IsOpenVRCompatible()) {
		globals::features::vr.ProcessControllerInputForImGui();
	}

	// Check if we need to render anything
	auto shaderCache = globals::shaderCache;
	auto failed = shaderCache->GetCurrentFailedCount();
	auto hide = shaderCache->IsHideErrors();
	auto* abTestingManager = ABTestingManager::GetSingleton();
	auto* renderDoc = RenderDoc::GetSingleton();

	bool bShouldDraw = shaderCache->IsCompiling() ||
	                    IsEnabled ||
	                    EditorWindow::GetSingleton()->open ||
	                    abTestingManager->IsEnabled() ||
	                    (failed && !hide) ||
	                    globals::features::performanceOverlay.settings.ShowInOverlay ||
	                    renderDoc->IsAvailable();

	if (!bShouldDraw) {
		auto& io = ImGui::GetIO();
		io.ClearInputKeys();
		io.ClearEventsQueue();
		return;
	}

	// Update style (OAR pattern)
	UpdateStyle();

	// Begin ImGui frame (OAR pattern)
	ImGui_ImplWin32_NewFrame();
	ImGui_ImplDX11_NewFrame();
	ImGui::NewFrame();

	// Render shader compilation status
	RenderShaderCompilationStatus();
	RenderShaderBlockingStatus();

	// Weather editor
	auto* editorWindow = EditorWindow::GetSingleton();
	if (editorWindow->open && !EditorWindow::CanBeOpen()) {
		editorWindow->open = false;
		if (editorWindow->IsInPreviewMode())
			editorWindow->ExitPreviewMode();
	}
	editorWindow->UpdateOpenState();
	if (editorWindow->open) {
		bool flying = editorWindow->IsPreviewFlying();
		auto& io = ImGui::GetIO();
		io.MouseDrawCursor = !flying;
		if (flying)
			io.MousePos = { -FLT_MAX, -FLT_MAX };
		editorWindow->Draw();
	} else if (IsEnabled) {
		ImGui::GetIO().MouseDrawCursor = true;
		DrawSettings();
	} else {
		ImGui::GetIO().MouseDrawCursor = false;
	}

	// Feature overlays
	RenderFeatureOverlays();
	HandleABTesting();

	// Finalize (OAR pattern)
	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

	if (globals::features::vr.IsOpenVRCompatible()) {
		globals::features::vr.SubmitOverlayFrame();
	}
}

// ============================================================================
// DrawSettings - Simple feature list window
// ============================================================================

// ============================================================================
// Menu Item Types for Navigation
// ============================================================================

struct BuiltInMenu
{
	std::string name;
	std::function<void()> drawFunc;
};

struct CategoryHeader
{
	std::string name;
};

using MenuFuncInfo = std::variant<BuiltInMenu, CategoryHeader, std::string, Feature*>;

static std::vector<MenuFuncInfo> BuildMenuList(
	const std::string& searchFilter,
	std::map<std::string, bool>& categoryExpansionStates,
	const std::function<void()>& drawGeneralSettings,
	const std::function<void()>& drawAdvancedSettings)
{
	auto& featureList = Feature::GetFeatureList();
	auto sortedFeatureList{ featureList };
	std::ranges::sort(sortedFeatureList, [](Feature* a, Feature* b) {
		return a->GetName() < b->GetName();
	});

	if (!searchFilter.empty()) {
		auto it = std::remove_if(sortedFeatureList.begin(), sortedFeatureList.end(),
			[&searchFilter](Feature* feat) { return !Util::FeatureMatchesSearch(feat, searchFilter); });
		sortedFeatureList.erase(it, sortedFeatureList.end());
	}

	std::vector<MenuFuncInfo> menuList;
	menuList.push_back(BuiltInMenu{ "General", drawGeneralSettings });
	menuList.push_back(BuiltInMenu{ "Advanced", drawAdvancedSettings });

	// Group features by category
	std::map<std::string, std::vector<Feature*>> categorizedFeatures;
	for (Feature* feat : sortedFeatureList) {
		if (feat->IsInMenu() && feat->loaded) {
			std::string category(feat->GetCategory());
			categorizedFeatures[category].push_back(feat);
		}
	}

	for (auto& [category, features] : categorizedFeatures) {
		std::ranges::sort(features, [](Feature* a, Feature* b) {
			return a->GetName() < b->GetName();
		});
	}

	// Category display order
	std::vector<std::string> categoryOrder = { "Display", "Utility", "Characters", "Grass", "Lighting", "Materials", "Post-Processing", "Sky", "Landscape & Textures", "Water", "Other" };

	for (const std::string& category : categoryOrder) {
		if (categorizedFeatures.find(category) != categorizedFeatures.end() && !categorizedFeatures[category].empty()) {
			if (categoryExpansionStates.find(category) == categoryExpansionStates.end()) {
				categoryExpansionStates[category] = true;
			}
			menuList.push_back(CategoryHeader{ category });
			if (categoryExpansionStates[category]) {
				std::ranges::copy(categorizedFeatures[category], std::back_inserter(menuList));
			}
		}
	}

	// Add uncategorized
	for (const auto& [category, features] : categorizedFeatures) {
		if (std::find(categoryOrder.begin(), categoryOrder.end(), category) == categoryOrder.end() && !features.empty()) {
			if (categoryExpansionStates.find(category) == categoryExpansionStates.end()) {
				categoryExpansionStates[category] = true;
			}
			menuList.push_back(CategoryHeader{ category });
			if (categoryExpansionStates[category]) {
				std::ranges::copy(features, std::back_inserter(menuList));
			}
		}
	}

	// Unloaded features
	auto unloadedFeatures = sortedFeatureList | std::ranges::views::filter([](Feature* feat) {
		return !feat->loaded && feat->IsInMenu() && (!FeatureIssues::IsObsoleteFeature(feat->GetShortName()) || globals::state->IsDeveloperMode());
	});
	if (std::ranges::distance(unloadedFeatures) != 0) {
		menuList.push_back("Unloaded Features"s);
		std::ranges::copy(unloadedFeatures, std::back_inserter(menuList));
	}

	// Feature issues at top
	if (FeatureIssues::HasFeatureIssues()) {
		menuList.insert(menuList.begin(), BuiltInMenu{ "Feature Issues", []() {
														  FeatureIssues::DrawFeatureIssuesUI();
													  } });
	}

	return menuList;
}

void Menu::DrawSettings()
{
	if (focusChanged) {
		OnFocusChanged();
		focusChanged = false;
	}

	ImGui::DockSpaceOverViewport(0, NULL, ImGuiDockNodeFlags_PassthruCentralNode);

	ImGui::SetNextWindowPos(Util::GetNativeViewportSizeScaled(0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(Util::GetNativeViewportSizeScaled(0.8f), ImGuiCond_FirstUseEver);

	auto title = std::format("Community Shaders {}", Util::GetFormattedVersion(Plugin::VERSION));

	ImGui::Begin(title.c_str(), &IsEnabled, ImGuiWindowFlags_NoCollapse);
	{
		float footerHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 3;

		auto menuList = BuildMenuList(
			featureSearch,
			categoryExpansionStates,
			[this]() { DrawGeneralSettings(); },
			[this]() { DrawAdvancedSettings(); });

		// Handle pending feature selection
		if (!pendingFeatureSelection.empty()) {
			for (size_t i = 0; i < menuList.size(); ++i) {
				if (std::holds_alternative<Feature*>(menuList[i])) {
					Feature* feature = std::get<Feature*>(menuList[i]);
					if (feature->GetShortName() == pendingFeatureSelection) {
						selectedMenu = i;
						break;
					}
				}
			}
			pendingFeatureSelection.clear();
		}

		// Two-panel layout
		ImGui::BeginChild("Menus Table", ImVec2(0, -footerHeight));
		if (ImGui::BeginTable("Menus Table", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable)) {
			ImGui::TableSetupColumn("##ListOfMenus", 0, 2);
			ImGui::TableSetupColumn("##MenuConfig", 0, 8);

			// Left column - navigation
			ImGui::TableNextColumn();
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4());
			if (ImGui::BeginListBox("##MenusList", { -FLT_MIN, -FLT_MIN })) {
				// Built-in menus first (General, Advanced - skip Feature Issues which was inserted at front)
				for (size_t i = 0; i < menuList.size(); i++) {
					if (std::holds_alternative<BuiltInMenu>(menuList[i])) {
						const BuiltInMenu& menu = std::get<BuiltInMenu>(menuList[i]);
						bool isFeatureIssues = (menu.name == "Feature Issues");
						if (isFeatureIssues) {
							ImGui::PushStyleColor(ImGuiCol_Text, themeSettings.StatusPalette.Error);
						}
						if (ImGui::Selectable(fmt::format(" {} ", menu.name).c_str(), selectedMenu == i, ImGuiSelectableFlags_SpanAllColumns))
							selectedMenu = i;
						if (isFeatureIssues) {
							ImGui::PopStyleColor();
						}
					}
				}

				// Features header and search
				ImGui::Spacing();
				ImGui::SeparatorText("Features");
				Util::DrawFeatureSearchBar(featureSearch);

				// Categories and features
				for (size_t i = 0; i < menuList.size(); i++) {
					if (std::holds_alternative<BuiltInMenu>(menuList[i]))
						continue;  // Already rendered above

					if (std::holds_alternative<CategoryHeader>(menuList[i])) {
						const CategoryHeader& header = std::get<CategoryHeader>(menuList[i]);
						bool isExpanded = categoryExpansionStates[header.name];
						Util::DrawCategoryHeader(header.name.c_str(), isExpanded, -1);
						categoryExpansionStates[header.name] = isExpanded;
					} else if (std::holds_alternative<std::string>(menuList[i])) {
						const std::string& label = std::get<std::string>(menuList[i]);
						ImGui::Spacing();
						ImGui::SeparatorText(label.c_str());
					} else if (std::holds_alternative<Feature*>(menuList[i])) {
						Feature* feat = std::get<Feature*>(menuList[i]);
						auto featureName = feat->GetShortName();
						bool isSelected = selectedMenu == i;

						if (!feat->loaded) {
							ImGui::PushStyleColor(ImGuiCol_Text, themeSettings.StatusPalette.Disable);
						}

						if (ImGui::Selectable(fmt::format(" {} ", featureName).c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns))
							selectedMenu = i;

						if (!feat->loaded) {
							ImGui::PopStyleColor();
						}
					}
				}

				ImGui::EndListBox();
			}
			ImGui::PopStyleVar();
			ImGui::PopStyleColor();

			// Right column - selected menu content
			ImGui::TableNextColumn();
			if (selectedMenu < menuList.size()) {
				if (std::holds_alternative<BuiltInMenu>(menuList[selectedMenu])) {
					std::get<BuiltInMenu>(menuList[selectedMenu]).drawFunc();
				} else if (std::holds_alternative<Feature*>(menuList[selectedMenu])) {
					Feature* feat = std::get<Feature*>(menuList[selectedMenu]);
					auto featureName = feat->GetShortName();

					// Feature header
					ImGui::SetWindowFontScale(1.5f);
					ImGui::TextUnformatted(featureName.c_str());
					ImGui::SetWindowFontScale(1.0f);
					if (!feat->version.empty()) {
						std::string formattedVersion = feat->version;
						std::replace(formattedVersion.begin(), formattedVersion.end(), '-', '.');
						ImGui::SameLine();
						ImVec4 versionColor = themeSettings.Palette.Text;
						versionColor.w *= 0.6f;
						ImGui::TextColored(versionColor, "v%s", formattedVersion.c_str());
					}
					ImGui::Separator();

					feat->DrawSettings();
				} else {
					ImGui::TextDisabled("Select an item on the left.");
				}
			} else {
				ImGui::TextDisabled("Select an item on the left.");
			}

			ImGui::EndTable();
		}
		ImGui::EndChild();

		// Footer
		ImGui::Spacing();
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal, 3.0f);
		ImGui::Spacing();
		DrawFooter();

		Util::DrawClearShaderCacheConfirmation();
	}
	ImGui::End();
}

// ============================================================================
// General Settings
// ============================================================================

void Menu::DrawGeneralSettings()
{
	if (ImGui::BeginTabBar("##GeneralTabBar")) {
		if (ImGui::BeginTabItem("Shaders")) {
			auto shaderCache = globals::shaderCache;

			bool useCustomShaders = shaderCache->IsEnabled();
			if (ImGui::Checkbox("Use Custom Shaders", &useCustomShaders)) {
				shaderCache->SetEnabled(useCustomShaders);
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Disabling this effectively disables all features.");
			}

			bool useDiskCache = shaderCache->IsDiskCache();
			if (ImGui::Checkbox("Enable Disk Cache", &useDiskCache)) {
				shaderCache->SetDiskCache(useDiskCache);
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Disables loading shaders from disk and prevents saving compiled shaders to disk cache.");
			}

			bool skipUnchanged = shaderCache->IsSkipUnchangedShaders();
			ImGui::BeginDisabled(!useDiskCache);
			if (ImGui::Checkbox("Skip Unchanged Shaders", &skipUnchanged)) {
				shaderCache->SetSkipUnchangedShaders(skipUnchanged);
			}
			ImGui::EndDisabled();
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"When enabled, each shader is recompiled from source only if its .hlsl file "
					"is newer than the cached .bin on disk. "
					"Shaders whose source has not changed are loaded directly from the disk cache, "
					"avoiding the full startup compilation cost.");
			}

			bool useAsync = shaderCache->IsAsync();
			if (ImGui::Checkbox("Enable Async", &useAsync)) {
				shaderCache->SetAsync(useAsync);
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Skips a shader being replaced if it hasn't been compiled yet. Also makes compilation blazingly fast!");
			}

			if (shaderCache->GetTotalTasks() > 0) {
				ImGui::Text("Last shader cache build duration: %s",
					shaderCache->GetShaderStatsString(true, true).c_str());
			}

			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Keybindings")) {
			Util::InputComboWidget("Toggle Key:", settings.ToggleKey, settingToggleKey, "Change##toggle");
			Util::InputComboWidget("Effect Toggle Key:", settings.EffectToggleKey, settingsEffectsToggle, "Change##EffectToggle");
			Util::InputComboWidget("Skip Compilation Key:", settings.SkipCompilationKey, settingSkipCompilationKey, "Change##skip");
			Util::InputComboWidget("Overlay Toggle Key:", settings.OverlayToggleKey, settingOverlayToggleKey, "Change##OverlayToggle");
			Util::InputComboWidget("Weather Editor Toggle Key:", settings.WeatherEditorToggleKey, settingWeatherEditorToggleKey, "Change##WeatherEditorToggle");

			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Interface")) {
			ImGui::SliderFloat("UI Scale", &settings.UIScale, 0.5f, 3.0f, "%.1f");
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Scales the entire UI. Applied next frame.");
			}
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}
}

// ============================================================================
// Advanced Settings
// ============================================================================

void Menu::DrawAdvancedSettings()
{
	if (ImGui::BeginTabBar("##AdvancedTabBar")) {
		if (ImGui::BeginTabItem("TruePBR")) {
			globals::truePBR->DrawSettings();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Disable at Boot")) {
			DrawDisableAtBootSettings();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
}

void Menu::DrawDisableAtBootSettings()
{
	auto state = globals::state;
	auto& disabledFeatures = state->GetDisabledFeatures();

	ImGui::Text(
		"Select features to disable at boot. "
		"This is the same as deleting a feature.ini file. "
		"Restart will be required to reenable.");
	ImGui::Spacing();

	if (ImGui::CollapsingHeader("Special Features", ImGuiTreeNodeFlags_DefaultOpen)) {
		std::vector<std::string> specialFeatureNames;
		for (const auto& [featureName, _] : state->specialFeatures) {
			specialFeatureNames.push_back(featureName);
		}
		std::sort(specialFeatureNames.begin(), specialFeatureNames.end());

		for (const auto& featureName : specialFeatureNames) {
			bool isDisabled = disabledFeatures.contains(featureName) && disabledFeatures[featureName];
			if (ImGui::Checkbox(featureName.c_str(), &isDisabled)) {
				disabledFeatures[featureName] = isDisabled;
			}
		}
	}

	if (ImGui::CollapsingHeader("Features", ImGuiTreeNodeFlags_DefaultOpen)) {
		auto featureList = Feature::GetFeatureList();
		std::sort(featureList.begin(), featureList.end(), [](Feature* a, Feature* b) {
			return a->GetShortName() < b->GetShortName();
		});
		for (auto* feature : featureList) {
			const std::string featureName = feature->GetShortName();
			bool isDisabled = disabledFeatures.contains(featureName) && disabledFeatures[featureName];
			if (ImGui::Checkbox(featureName.c_str(), &isDisabled)) {
				disabledFeatures[featureName] = isDisabled;
			}
		}
	}
}

// ============================================================================
// Footer
// ============================================================================

void Menu::DrawFooter()
{
	ImGui::BulletText(std::format("Game Version: {} {}", magic_enum::enum_name(REL::Module::GetRuntime()), Util::GetFormattedVersion(REL::Module::get().version()).c_str()).c_str());
	ImGui::SameLine();
	ImGui::BulletText(std::format("D3D12 Swap Chain: {}", globals::features::upscaling.d3d12SwapChainActive ? "Active" : "Inactive").c_str());
	ImGui::SameLine();
	ImGui::BulletText(std::format("GPU: {}", globals::state->adapterDescription.c_str()).c_str());
}

// ============================================================================
// Shader Compilation Status - Moved inline from OverlayRenderer
// ============================================================================

void Menu::RenderShaderCompilationStatus()
{
	auto shaderCache = globals::shaderCache;
	auto failed = shaderCache->GetCurrentFailedCount();
	auto hide = shaderCache->IsHideErrors();
	auto* renderDoc = RenderDoc::GetSingleton();
	bool renderDocAvailable = renderDoc->IsAvailable();
	const auto renderDocInformation = renderDoc->GetOverlayWarningMessage();

	constexpr float pos = 10.0f;

	uint64_t totalShaders = shaderCache->GetTotalTasks();
	uint64_t compiledShaders = shaderCache->GetCompletedTasks();

	auto state = globals::state;

	if (shaderCache->IsCompiling()) {
		auto progressTitle = fmt::format("{}Compiling Shaders: {}",
			shaderCache->backgroundCompilation ? "Background " : "",
			shaderCache->GetShaderStatsString(!state->IsDeveloperMode()).c_str());
		auto percent = (float)compiledShaders / (float)totalShaders;
		auto progressOverlay = fmt::format("{}/{} ({:2.1f}%)", compiledShaders, totalShaders, 100 * percent);

		ImGui::SetNextWindowPos(ImVec2(pos, pos));
		if (ImGui::Begin("ShaderCompilationInfo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
			ImGui::TextUnformatted(progressTitle.c_str());
			ImGui::ProgressBar(percent, ImVec2(0.0f, 0.0f), progressOverlay.c_str());
			if (state->IsDeveloperMode()) {
				int32_t threadLimit = shaderCache->backgroundCompilation ? shaderCache->backgroundCompilationThreadCount : shaderCache->compilationThreadCount;
				int compilationRunning = (int)shaderCache->compilationPool.get_tasks_running();
				int heavyInFlight = shaderCache->GetHeavyTasksInFlight();
				int heavyLimit = static_cast<int>(Util::GetPerformanceCoreCount());
				uint64_t slow = shaderCache->GetSlowTasks();
				uint64_t verySlow = shaderCache->GetVerySlowTasks();
				ImGui::Text("Threads: %d / %d limit | Heavy: %d / %d P-cores | %d workers",
					compilationRunning, threadLimit, heavyInFlight, heavyLimit,
					(int)shaderCache->compilationPool.get_thread_count());
				if (slow > 0) {
					ImGui::Text("Slow shaders: %llu (very slow: %llu)", slow, verySlow);
				}
			}
			if (!shaderCache->backgroundCompilation && shaderCache->menuLoaded) {
				auto skipShadersText = fmt::format(
					"Press {} to proceed without completing shader compilation. ",
					Util::Input::KeyIdToString(settings.SkipCompilationKey));
				ImGui::TextUnformatted(skipShadersText.c_str());
				ImGui::TextUnformatted("WARNING: Uncompiled shaders will have visual errors or cause stuttering when loading.");
			}
			if (renderDocAvailable)
				ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", renderDocInformation.c_str());
		}
		ImGui::End();
	}

	if (failed) {
		if (!hide) {
			ImGui::SetNextWindowPos(ImVec2(pos, pos));
			if (ImGui::Begin("ShaderCompilationInfo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "ERROR: %d shaders failed to compile. Check installation and CommunityShaders.log", failed);
				if (FeatureIssues::HasPotentialShaderModifyingFeatures()) {
					ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Features that may have modified shaders detected. Check Feature Issues in the Menu.");
				}
				if (renderDocAvailable)
					ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", renderDocInformation.c_str());
			}
			ImGui::End();
		}
	} else if (renderDocAvailable) {
		ImGui::SetNextWindowPos(ImVec2(pos, pos));
		if (ImGui::Begin("ShaderCompilationInfo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
			ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", renderDocInformation.c_str());
		}
		ImGui::End();
	}
}

void Menu::RenderShaderBlockingStatus()
{
	auto shaderCache = globals::shaderCache;
	auto state = globals::state;

	if (!state->IsDeveloperMode() || shaderCache->blockedKey.empty()) {
		return;
	}

	constexpr float pos = 10.0f;

	float yPos = pos;
	if (auto* shaderWin = ImGui::FindWindowByName("ShaderCompilationInfo")) {
		if (shaderWin->Active) {
			yPos = shaderWin->Pos.y + shaderWin->Size.y + ImGui::GetStyle().ItemSpacing.y;
		}
	}

	ImGui::SetNextWindowPos(ImVec2(pos, yPos));
	if (ImGui::Begin("ShaderBlockingInfo", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Shader Blocking Active");
		ImGui::Text("Blocked: %s", shaderCache->blockedKey.c_str());

		auto activeShaders = shaderCache->GetActiveShaders();
		size_t blockedIndex = 0;
		bool foundBlocked = false;
		for (size_t i = 0; i < activeShaders.size(); ++i) {
			if (activeShaders[i].key == shaderCache->blockedKey) {
				blockedIndex = i + 1;
				foundBlocked = true;
				break;
			}
		}

		if (foundBlocked) {
			ImGui::Text("Index: %zu/%zu", blockedIndex, activeShaders.size());
		} else {
			ImGui::Text("Index: N/A (%zu active)", activeShaders.size());
		}

		for (const auto& shader : activeShaders) {
			if (shader.key == shaderCache->blockedKey) {
				ImGui::Text("Type: %s | Class: %s | Descriptor: 0x%X",
					magic_enum::enum_name(shader.shaderType).data(),
					magic_enum::enum_name(shader.shaderClass).data(),
					shader.descriptor);
				break;
			}
		}
	}
	ImGui::End();
}

void Menu::RenderFeatureOverlays()
{
	for (Feature* feat : Feature::GetFeatureList()) {
		if (feat && feat->loaded) {
			if (auto* overlay = dynamic_cast<OverlayFeature*>(feat)) {
				overlay->DrawOverlay();
			}
		}
	}
}

void Menu::HandleABTesting()
{
	auto* abTestingManager = ABTestingManager::GetSingleton();
	abTestingManager->Update();

	if (abTestingManager->IsEnabled()) {
		globals::features::performanceOverlay.UpdateAllShaderTestData();
		auto& overlay = globals::features::performanceOverlay;
		auto [mainRows, summaryRows] = overlay.BuildDrawCallRows();
		std::vector<DrawCallRow> allRows = mainRows;
		allRows.insert(allRows.end(), summaryRows.begin(), summaryRows.end());
		abTestingManager->GetAggregator().OnFrame(allRows);
	}

	abTestingManager->DrawOverlayUI();
}

bool Menu::ShouldSwallowInput()
{
	auto editorWindow = EditorWindow::GetSingleton();
	return IsEnabled || (editorWindow && editorWindow->open);
}

bool Menu::IsPreviewFlying()
{
	auto editorWindow = EditorWindow::GetSingleton();
	return editorWindow && editorWindow->IsPreviewFlying();
}
