#include "Menu.h"

#include "IconsFontAwesome5.h"

#ifndef DIRECTINPUT_VERSION
#	define DIRECTINPUT_VERSION 0x0800
#endif
#include <dinput.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include "DX12SwapChain.h"
#include "Deferred.h"
#include "Feature.h"
#include "FeatureIssues.h"
#include "FeatureVersions.h"
#include "Menu/AdvancedSettingsRenderer.h"
#include "Menu/DisplaySettingsRenderer.h"
#include "Menu/FeatureListRenderer.h"
#include "Menu/MenuHeaderRenderer.h"
#include "Menu/OverlayRenderer.h"
#include "Menu/SettingsTabRenderer.h"
#include "Menu/ThemeManager.h"
#include "ShaderCache.h"
#include "State.h"
#include "Streamline.h"
#include "TruePBR.h"
#include "Upscaling.h"
#include "Util.h"
#include "Utils/UI.h"

#include "Features/LightLimitFix/ParticleLights.h"
#include "Features/PerformanceOverlay.h"
#include "Features/PerformanceOverlay/ABTesting/ABTestAggregator.h"
#include "Features/PerformanceOverlay/ABTesting/ABTesting.h"
#include "Features/VR.h"
#include "Features/WeatherPicker.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Menu::ThemeSettings::PaletteColors,
	Background,
	Text,
	Border)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Menu::ThemeSettings::StatusPaletteColors,
	Disable,
	Error,
	Warning,
	RestartNeeded,
	CurrentHotkey,
	SuccessColor,
	InfoColor)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Menu::ThemeSettings::FeatureHeadingColors,
	ColorDefault,
	ColorHovered,
	MinimizedFactor)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ImGuiStyle,
	WindowPadding,
	WindowRounding,
	WindowBorderSize,
	WindowMinSize,
	ChildRounding,
	ChildBorderSize,
	PopupRounding,
	PopupBorderSize,
	FramePadding,
	FrameRounding,
	FrameBorderSize,
	ItemSpacing,
	ItemInnerSpacing,
	CellPadding,
	IndentSpacing,
	ColumnsMinSpacing,
	ScrollbarSize,
	ScrollbarRounding,
	GrabMinSize,
	GrabRounding,
	LogSliderDeadzone,
	TabRounding,
	TabBorderSize,
	TabMinWidthForCloseButton,
	TabBarBorderSize,
	TableAngledHeadersAngle,
	ColorButtonPosition,
	ButtonTextAlign,
	SelectableTextAlign,
	SeparatorTextBorderSize,
	SeparatorTextAlign,
	SeparatorTextPadding,
	DockingSeparatorSize,
	MouseCursorScale)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Menu::ThemeSettings,
	FontSize,
	GlobalScale,
	UseSimplePalette,
	ShowActionIcons,
	TooltipHoverDelay,
	Palette,
	StatusPalette,
	FeatureHeading,
	Style,
	FullPalette)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Menu::Settings,
	ToggleKey,
	SkipCompilationKey,
	EffectToggleKey,
	OverlayToggleKey,
	Theme)

bool IsEnabled = false;
std::unordered_map<std::string, int> Menu::categoryCounts;

Menu::~Menu()
{  // Release icon textures if loaded
	uiIcons.saveSettings.Release();
	uiIcons.loadSettings.Release();
	uiIcons.clearCache.Release();
	uiIcons.logo.Release();
	uiIcons.characters.Release();
	uiIcons.grass.Release();
	uiIcons.lighting.Release();
	uiIcons.sky.Release();
	uiIcons.landscape.Release();
	uiIcons.water.Release();
	uiIcons.debug.Release();
	uiIcons.materials.Release();

	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	dxgiAdapter3 = nullptr;

	globals::features::vr.DestroyOverlay();
}

void Menu::Load(json& o_json)
{
	settings = o_json;
}

void Menu::Save(json& o_json)
{
	o_json = settings;
}

void Menu::Init()
{
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	auto& imgui_io = ImGui::GetIO();
	imgui_io.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_DockingEnable;
	imgui_io.BackendFlags = ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_HasGamepad;

	// Enhanced font configuration for sharper text rendering
	ImFontConfig font_config;
	font_config.OversampleH = ThemeManager::Constants::FCONF_OVERSAMPLE_H;
	font_config.OversampleV = ThemeManager::Constants::FCONF_OVERSAMPLE_V;
	font_config.PixelSnapH = ThemeManager::Constants::FCONF_PIXELSNAP_H;
	font_config.RasterizerMultiply = ThemeManager::Constants::FCONF_RASTERIZER_MULTIPLY;

	DXGI_SWAP_CHAIN_DESC desc{};
	globals::d3d::swapChain->GetDesc(&desc);

	float fontSize = settings.Theme.FontSize;

	if (std::round(fontSize) != std::round(ThemeManager::Constants::DEFAULT_FONT_SIZE)) {
		if (globals::state->screenSize.y > 0) {
			fontSize = globals::state->screenSize.y * ThemeManager::Constants::DEFAULT_FONT_RATIO;
		} else {
			logger::warn("Menu::Init() - Failed to get game resolution from globals::state->screenSize.");
		}
	}

	fontSize = std::clamp(fontSize, ThemeManager::Constants::MIN_FONT_SIZE, ThemeManager::Constants::MAX_FONT_SIZE);

	if (!imgui_io.Fonts->AddFontFromFileTTF("Data\\Interface\\CommunityShaders\\Fonts\\Jost-Regular.ttf",
			std::round(fontSize), &font_config)) {
		logger::warn("Menu::Init() - Failed to load custom font. Using default font.");
		imgui_io.Fonts->AddFontDefault();
	}

	imgui_io.FontGlobalScale = exp2(settings.Theme.GlobalScale);

		// add font awesome 5
	static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
	ImFontConfig icons_config;
	icons_config.MergeMode = true;
	icons_config.PixelSnapH = true;
	icons_config.FontDataOwnedByAtlas = true;

	imgui_io.Fonts->AddFontFromFileTTF("Data\\Interface\\CommunityShaders\\Fonts\\fa-solid-900.ttf", 24, &icons_config, icons_ranges);

	// Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(desc.OutputWindow);
	ImGui_ImplDX11_Init(globals::d3d::device, globals::d3d::context);

	{
		winrt::com_ptr<IDXGIDevice> dxgiDevice;
		if (!FAILED(globals::d3d::device->QueryInterface(dxgiDevice.put()))) {
			winrt::com_ptr<IDXGIAdapter> dxgiAdapter;
			if (!FAILED(dxgiDevice->GetAdapter(dxgiAdapter.put()))) {
				dxgiAdapter->QueryInterface(dxgiAdapter3.put());
			}
		}
	}
	// Load UI icons
	if (!Util::InitializeMenuIcons(this)) {
		logger::warn("Menu::Init() - Failed to load UI icons. Will fallback to text buttons");
	}

	BuildCategoryCounts();

	if (globals::features::vr.IsOpenVRCompatible()) {
		globals::features::vr.EnsureOverlayInitialized();
	}

	initialized = true;
}

/**
 * @brief Main UI rendering coordinator for the Community Shaders menu
 *
 * This method serves as the primary entry point for rendering the entire menu interface.
 * It handles window setup, docking configuration, and delegates rendering to specialized
 * renderer components for better separation of concerns.
 *
 * The method manages:
 * - ImGui docking space and window positioning
 * - Focus change handling
 * - Dynamic window flags based on docking state
 * - Header, navigation tabs, and settings panels coordination
 */
void Menu::DrawSettings()
{
	if (focusChanged) {
		OnFocusChanged();
		focusChanged = false;
	}
	ImGui::DockSpaceOverViewport(NULL, ImGuiDockNodeFlags_PassthruCentralNode);

	ImGui::SetNextWindowPos(Util::GetNativeViewportSizeScaled(0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(Util::GetNativeViewportSizeScaled(0.8f), ImGuiCond_FirstUseEver);
	auto title = std::format("Community Shaders {}", Util::GetFormattedVersion(Plugin::VERSION));

	// Determine window flags based on docking state
	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar;
	// Check if this will be docked (we need to peek at the docking state)
	static bool wasDocked = false;
	bool willBeDocked = wasDocked;  // Use previous frame's state as approximation

	// Only hide title bar when not docked
	if (!willBeDocked) {
		windowFlags |= ImGuiWindowFlags_NoTitleBar;
	}

	ImGui::Begin(title.c_str(), &IsEnabled, windowFlags);
	{
		// Update docking state tracking
		bool isDocked = ImGui::IsWindowDocked();
		wasDocked = isDocked;

		const float uiScale = exp2(settings.Theme.GlobalScale);  // Get current UI scale
		// Check if we can show icons - require setting enabled and at least some icons loaded (for undocked)
		// For docked mode, always show icons if textures are available
		bool canShowIcons = settings.Theme.ShowActionIcons &&
		                    (uiIcons.saveSettings.texture ||
								uiIcons.loadSettings.texture ||
								uiIcons.clearCache.texture);  // Always show logo if available, regardless of action icons setting
		bool showLogo = uiIcons.logo.texture != nullptr;

		// Render header using extracted component
		MenuHeaderRenderer::RenderHeader(isDocked, showLogo, canShowIcons, uiScale, uiIcons);

		// Main content starts here - no additional separator needed as it's already handled in the conditions above

		float footer_height = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 3 + 3.0f;  // text + separator

		// Static storage for menu state - must persist across frames
		static size_t selectedMenu = 0;
		static std::map<std::string, bool> categoryExpansionStates;

		// Render feature list using extracted component
		FeatureListRenderer::RenderFeatureList(
			footer_height,
			selectedMenu,
			featureSearch,
			pendingFeatureSelection,
			categoryExpansionStates,
			[&]() { DrawGeneralSettings(); },
			[&]() { DrawAdvancedSettings(); },
			[&]() { DrawDisplaySettings(); });

		ImGui::Spacing();
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal, ThemeManager::Constants::SEPARATOR_THICKNESS);
		ImGui::Spacing();

		DrawFooter();
	}
	ImGui::End();
}

/**
 * @brief Renders the General settings tab content
 *
 * Delegates rendering to SettingsTabRenderer for the general configuration panel,
 * which includes Shaders, Keybindings, and Interface sub-tabs. This method provides
 * the callback for key-to-string conversion while maintaining separation of concerns.
 */
void Menu::DrawGeneralSettings()
{
	// Prepare settings state for the renderer
	SettingsTabRenderer::SettingsState state{
		.settingToggleKey = settingToggleKey,
		.settingsEffectsToggle = settingsEffectsToggle,
		.settingSkipCompilationKey = settingSkipCompilationKey,
		.settingOverlayToggleKey = settingOverlayToggleKey
	};

	// Render settings using extracted component
	SettingsTabRenderer::RenderGeneralSettings(
		state,
		[](uint32_t key) { return Util::Input::KeyIdToString(key); });
}

/**
 * @brief Renders the Advanced settings tab content
 *
 * Delegates rendering to AdvancedSettingsRenderer for developer and advanced user
 * settings. Uses lambda callbacks to access private Menu methods while maintaining
 * encapsulation and proper separation of concerns.
 */
void Menu::DrawAdvancedSettings()
{
	// Render advanced settings using extracted component
	AdvancedSettingsRenderer::RenderAdvancedSettings(
		[]() { globals::truePBR->DrawSettings(); },
		[this]() { DrawDisableAtBootSettings(); });
}

void Menu::DrawDisableAtBootSettings()
{
	auto state = globals::state;
	auto& disabledFeatures = state->GetDisabledFeatures();

	if (ImGui::CollapsingHeader("Disable at Boot", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick)) {
		ImGui::Text(
			"Select features to disable at boot. "
			"This is the same as deleting a feature.ini file. "
			"Restart will be required to reenable.");

		if (ImGui::CollapsingHeader("Special Features")) {
			// Prepare a sorted list of special feature names
			std::vector<std::string> specialFeatureNames;
			for (const auto& [featureName, _] : state->specialFeatures) {
				specialFeatureNames.push_back(featureName);
			}
			std::sort(specialFeatureNames.begin(), specialFeatureNames.end());

			// Display sorted special features
			for (const auto& featureName : specialFeatureNames) {
				// Check if the feature is currently disabled
				bool isDisabled = disabledFeatures.contains(featureName) && disabledFeatures[featureName];

				// Create a checkbox for each feature
				if (ImGui::Checkbox(featureName.c_str(), &isDisabled)) {
					// Update the disabledFeatures map based on user interaction
					disabledFeatures[featureName] = isDisabled;
				}
			}
		}

		if (ImGui::CollapsingHeader("Features")) {
			// Prepare a sorted list of feature pointers
			auto featureList = Feature::GetFeatureList();
			std::sort(featureList.begin(), featureList.end(), [](Feature* a, Feature* b) {
				return a->GetShortName() < b->GetShortName();
			});

			// Display sorted features
			for (auto* feature : featureList) {
				const std::string featureName = feature->GetShortName();
				bool isDisabled = disabledFeatures.contains(featureName) && disabledFeatures[featureName];

				if (ImGui::Checkbox(featureName.c_str(), &isDisabled)) {
					// Update the disabledFeatures map based on user interaction
					disabledFeatures[featureName] = isDisabled;
				}
			}
		}
	}
}

/**
 * @brief Renders the Display settings tab content
 *
 * Delegates rendering to DisplaySettingsRenderer to handle upscaling and frame
 * generation settings. Provides callbacks for feature status checking and upscaling
 * configuration while maintaining clean architecture separation.
 */
void Menu::DrawDisplaySettings()
{
	DisplaySettingsRenderer::RenderDisplaySettings(
		globals::state->upscalerLoaded,
		[](const std::string& featureName) { return globals::state->IsFeatureDisabled(featureName); },
		[]() { globals::upscaling->DrawSettings(); });
}

void Menu::DrawFooter()
{
	ImGui::BulletText(std::format("Game Version: {} {}", magic_enum::enum_name(REL::Module::GetRuntime()), Util::GetFormattedVersion(REL::Module::get().version()).c_str()).c_str());
	ImGui::SameLine();
	ImGui::BulletText(std::format("D3D12 Interop: {}", globals::upscaling->d3d12Interop ? "Active" : "Inactive").c_str());
	ImGui::SameLine();
	ImGui::Text(std::format("GPU: {}", globals::state->adapterDescription.c_str()).c_str());
}

/**
 * @brief Main overlay rendering coordinator
 *
 * Delegates all overlay rendering to OverlayRenderer while providing necessary
 * callbacks for input processing, settings rendering, and key mapping. This method
 * serves as the bridge between Menu's state and the extracted overlay rendering logic.
 *
 * Handles VR setup, input event processing, shader compilation status, feature overlays,
 * A/B testing, and ImGui frame management through the specialized renderer component.
 */
void Menu::DrawOverlay()
{
	OverlayRenderer::RenderOverlay(
		*this,
		[this]() { ProcessInputEventQueue(); },
		[this]() { DrawSettings(); },
		[](uint32_t key) { return Util::Input::KeyIdToString(key); },
		cachedFontSize,
		settings.Theme.FontSize);
}

/**
 * @brief Processes queued input events for both VR and non-VR devices
 *
 * This method handles the complex logic of routing input events to appropriate handlers:
 * - VR controller events are forwarded to the VR system for specialized processing
 * - Non-VR events (keyboard, mouse) are processed directly for ImGui integration
 * - Includes key state normalization and stuck key detection/correction
 *
 * The method maintains thread safety through mutex protection of the input event queue.
 *
 * @note This method contains Menu-specific logic and state management that makes it
 *       inappropriate for extraction to a utility class.
 */
void Menu::ProcessInputEventQueue()
{
	std::unique_lock<std::shared_mutex> mutex(_inputEventMutex);
	ImGuiIO& io = ImGui::GetIO();
	// Split the queue into VR and non-VR events
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
	// Process VR events in VR
	if (!vrEvents.empty()) {
		globals::features::vr.ProcessVREvents(vrEvents);
		globals::features::vr.UpdateOverlayMenuStateFromInput();
	}
	// Process non-VR events in Menu (original logic here)
	for (auto& event : nonVREvents) {
		if (event.eventType == RE::INPUT_EVENT_TYPE::kChar) {
			io.AddInputCharacter(event.keyCode);
			continue;
		}
		if (event.device == RE::INPUT_DEVICE::kMouse) {
			logger::trace("Detect mouse scan code {} value {} pressed: {}", event.keyCode, event.value, event.IsPressed());
			if (event.keyCode > 7) {  // middle scroll
				io.AddMouseWheelEvent(0, event.value * (event.keyCode == 8 ? 1 : -1));
			} else {
				if (event.keyCode > 5)
					event.keyCode = 5;
				io.AddMouseButtonEvent(event.keyCode, event.IsPressed());
			}
		}

		if (event.device == RE::INPUT_DEVICE::kKeyboard) {
			uint32_t key = Util::Input::DIKToVK(event.keyCode);
			logger::trace("Detected key code {} ({})", event.keyCode, key);
			if (key == event.keyCode)
				key = MapVirtualKeyEx(event.keyCode, MAPVK_VSC_TO_VK_EX, GetKeyboardLayout(0));
			if (!event.IsPressed()) {
				struct HotkeyAction
				{
					uint32_t* settingKey;
					bool* settingFlag;
					std::function<void(uint32_t)> action;
				};
				auto shaderCache = globals::shaderCache;
				auto devMode = globals::state->IsDeveloperMode();
				HotkeyAction hotkeyActions[] = {
					{ &settings.ToggleKey, &settingToggleKey, [this](uint32_t key) { settings.ToggleKey = key; settingToggleKey = false; } },
					{ &settings.SkipCompilationKey, &settingSkipCompilationKey, [this](uint32_t key) { settings.SkipCompilationKey = key; settingSkipCompilationKey = false; } },
					{ &settings.EffectToggleKey, &settingsEffectsToggle, [this](uint32_t key) { settings.EffectToggleKey = key; settingsEffectsToggle = false; } },
					{ &settings.OverlayToggleKey, &settingOverlayToggleKey, [this](uint32_t key) { settings.OverlayToggleKey = key; settingOverlayToggleKey = false; } },
				};
				bool handled = false;
				for (auto& h : hotkeyActions) {
					if (*(h.settingFlag)) {
						h.action(key);
						handled = true;
						break;
					}
				}
				if (!handled) {
					struct KeyAction
					{
						uint32_t settingKey;
						std::function<void()> action;
					};
					KeyAction keyActions[] = {
						{ settings.ToggleKey, [this]() { IsEnabled = !IsEnabled; } },
						{ settings.SkipCompilationKey, [shaderCache]() { shaderCache->backgroundCompilation = true; } },
						{ settings.EffectToggleKey, [shaderCache]() { shaderCache->SetEnabled(!shaderCache->IsEnabled()); } },
						{ priorShaderKey, [shaderCache, devMode]() { if (devMode) shaderCache->IterateShaderBlock(); } },
						{ nextShaderKey, [shaderCache, devMode]() { if (devMode) shaderCache->IterateShaderBlock(false); } },
						{ settings.OverlayToggleKey, []() {
							 Menu::GetSingleton()->overlayVisible = !Menu::GetSingleton()->overlayVisible;
						 } },
					};
					for (const auto& ka : keyActions) {
						if (key == ka.settingKey) {
							ka.action();
							break;
						}
					}
				}
				if (key == VK_ESCAPE && IsEnabled) {
					IsEnabled = false;
				}
			}

			io.AddKeyEvent(Util::Input::VirtualKeyToImGuiKey(key), event.IsPressed());

			if (key == VK_LCONTROL || key == VK_RCONTROL)
				io.AddKeyEvent(ImGuiMod_Ctrl, event.IsPressed());
			else if (key == VK_LSHIFT || key == VK_RSHIFT)
				io.AddKeyEvent(ImGuiMod_Shift, event.IsPressed());
			else if (key == VK_LMENU || key == VK_RMENU)
				io.AddKeyEvent(ImGuiMod_Alt, event.IsPressed());
		}
	}

	_keyEventQueue.clear();

	// Fallback: release stuck Shift and Tab if OS reports them not pressed
	if ((io.KeysDown[ImGuiKey_LeftShift] && !(GetAsyncKeyState(VK_LSHIFT) & Constants::KEY_PRESSED_MASK)) ||
		(io.KeysDown[ImGuiKey_RightShift] && !(GetAsyncKeyState(VK_RSHIFT) & Constants::KEY_PRESSED_MASK))) {
		io.AddKeyEvent(ImGuiKey_LeftShift, false);
		io.AddKeyEvent(ImGuiKey_RightShift, false);
	}
	if (io.KeysDown[ImGuiKey_Tab] && !(GetAsyncKeyState(VK_TAB) & Constants::KEY_PRESSED_MASK)) {
		io.AddKeyEvent(ImGuiKey_Tab, false);
	}
}

void Menu::addToEventQueue(KeyEvent e)
{
	std::unique_lock<std::shared_mutex> mutex(_inputEventMutex);
	_keyEventQueue.emplace_back(e);
}

void Menu::OnFocusChanged()
{
	// Solves the alt+tab stuck issue, but disables tab after tabbing back in.
	if (const auto& inputMgr = RE::BSInputDeviceManager::GetSingleton()) {
		if (const auto& device = inputMgr->GetKeyboard()) {
			device->Reset();
		}
	}
	// Allows tab to work again after alt+tabbing back in.
	ImGui::GetIO().ClearInputKeys();
}

void Menu::ProcessInputEvents(RE::InputEvent* const* a_events)
{
	for (auto it = *a_events; it; it = it->next) {
		// Accept button, char, and thumbstick events
		if (it->GetEventType() != RE::INPUT_EVENT_TYPE::kButton &&
			it->GetEventType() != RE::INPUT_EVENT_TYPE::kChar &&

			it->GetEventType() != RE::INPUT_EVENT_TYPE::kThumbstick

			)  // we do not care about non button/char/thumbstick events
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

bool Menu::ShouldSwallowInput()
{
	return IsEnabled;
}

void Menu::SelectFeatureMenu(const std::string& featureName)
{
	pendingFeatureSelection = featureName;
	logger::info("Queued navigation to {} feature menu", featureName);
}

/**
 * @brief Renders the standalone weather details window when enabled
 *
 * Delegates to the WeatherPicker feature for rendering the weather details window
 * that can remain open even when the main menu is closed. This provides a simple
 * coordination layer between the Menu system and the WeatherPicker feature.
 */
void Menu::DrawWeatherDetailsWindow()
{
	if (!globals::features::weatherPicker.WeatherDetailsWindow.Enabled) {
		return;
	}

	// Use Weather core feature for all window management and rendering
	auto& weather = globals::features::weatherPicker;
	bool* p_open = &globals::features::weatherPicker.WeatherDetailsWindow.Enabled;
	weather.RenderWeatherDetailsWindow(p_open);
}

/**
 * @brief Builds category counts for feature organization and display
 *
 * Iterates through all loaded features and counts how many features belong to each
 * category. This information is used for UI organization and displaying category
 * statistics in the feature navigation interface.
 *
 * @note Only counts features that are both loaded and configured to appear in the menu.
 */
void Menu::BuildCategoryCounts()
{
	const std::vector<Feature*>& features = Feature::GetFeatureList();
	// Get the category of each feature, and increment the count for that category
	for (auto& feature : features) {
		if (feature->IsInMenu() && feature->loaded) {
			std::string_view category = feature->GetCategory();
			categoryCounts[std::string(category)]++;
		}
	}
}
