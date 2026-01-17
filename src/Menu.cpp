#include "Menu.h"

#ifndef DIRECTINPUT_VERSION
#	define DIRECTINPUT_VERSION 0x0800
#endif
#include <algorithm>
#include <cmath>
#include <dinput.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <iomanip>
#include <string>
#include <unordered_map>
#include <vector>

#include "Deferred.h"
#include "Feature.h"
#include "FeatureIssues.h"
#include "FeatureVersions.h"
#include "Features/Upscaling.h"
#include "Menu/AdvancedSettingsRenderer.h"
#include "Menu/BackgroundBlur.h"
#include "Menu/FeatureListRenderer.h"
#include "Menu/Fonts.h"
#include "Menu/HomePageRenderer.h"
#include "Menu/IconLoader.h"
#include "Menu/MenuHeaderRenderer.h"
#include "Menu/OverlayRenderer.h"
#include "Menu/SettingsTabRenderer.h"
#include "Menu/ThemeManager.h"
#include "ShaderCache.h"
#include "State.h"
#include "TruePBR.h"
#include "Util.h"
#include "Utils/UI.h"

#include "Features/PerformanceOverlay.h"
#include "Features/PerformanceOverlay/ABTesting/ABTestAggregator.h"
#include "Features/PerformanceOverlay/ABTesting/ABTesting.h"
#include "Features/VR.h"
#include "Features/WeatherPicker.h"
#include "WeatherEditor/EditorWindow.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Menu::ThemeSettings::PaletteColors,
	Background,
	Text,
	WindowBorder,
	FrameBorder,
	Separator,
	ResizeGrip)

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
	Menu::ThemeSettings::ScrollbarOpacitySettings,
	Background,
	Thumb,
	ThumbHovered,
	ThumbActive)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Menu::ThemeSettings::FontRoleSettings,
	Family,
	Style,
	File,
	SizeScale)

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
	FontName,
	GlobalScale,
	FontRoles,
	UseSimplePalette,
	ShowActionIcons,
	UseMonochromeIcons,
	UseMonochromeLogo,
	ShowFooter,
	CenterHeader,
	TooltipHoverDelay,
	BackgroundBlurEnabled,
	ScrollbarOpacity,
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
	ShaderBlockPrevKey,
	ShaderBlockNextKey,
	EnableShaderBlocking,
	FirstTimeSetupCompleted,
	Theme,
	SelectedThemePreset)

bool IsEnabled = false;
std::unordered_map<std::string, int> Menu::categoryCounts;

std::optional<Menu::FontRole> Menu::ResolveFontRole(std::string_view key)
{
	for (size_t i = 0; i < FontRoleDescriptors.size(); ++i) {
		if (FontRoleDescriptors[i].key == key) {
			return static_cast<FontRole>(i);
		}
	}
	return std::nullopt;
}

std::string Menu::BuildFontSignature(float baseFontSize) const
{
	return MenuFonts::BuildFontSignature(settings.Theme, baseFontSize);
}

const Menu::ThemeSettings::FontRoleSettings& Menu::GetDefaultFontRole(FontRole role)
{
	return MenuFonts::GetDefaultRole(role);
}

Menu::~Menu()
{  // Release icon textures if loaded
	uiIcons.saveSettings.Release();
	uiIcons.loadSettings.Release();
	uiIcons.deleteSettings.Release();
	uiIcons.clearCache.Release();
	uiIcons.logo.Release();
	uiIcons.featureSettingRevert.Release();
	uiIcons.applyToGame.Release();
	uiIcons.pauseTime.Release();
	uiIcons.undo.Release();
	uiIcons.discord.Release();
	uiIcons.characters.Release();
	uiIcons.display.Release();
	uiIcons.grass.Release();
	uiIcons.lighting.Release();
	uiIcons.sky.Release();
	uiIcons.landscape.Release();
	uiIcons.water.Release();
	uiIcons.debug.Release();
	uiIcons.materials.Release();
	uiIcons.postProcessing.Release();

	// Clean up blur resources
	BackgroundBlur::Cleanup();

	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	dxgiAdapter3 = nullptr;

	globals::features::vr.DestroyOverlay();
}

void Menu::Load(json& o_json)
{
	// Store current Theme state before loading config
	auto currentTheme = settings.Theme;

	settings = o_json;

	// Restore Theme - don't load it from config, only from theme preset files
	settings.Theme = currentTheme;

	// Legacy support: If old config has Theme data and no SelectedThemePreset, load it
	if (o_json.contains("Theme") && o_json["Theme"].is_object() && settings.SelectedThemePreset.empty()) {
		bool hasFontRoles = o_json["Theme"].contains("FontRoles");
		settings.Theme = o_json["Theme"];
		MenuFonts::NormalizeFontRoles(settings.Theme, hasFontRoles);

		auto& bodyRole = settings.Theme.FontRoles[static_cast<size_t>(FontRole::Body)];
		if (!Util::ValidateFont(bodyRole.File)) {
			const auto& defaults = Menu::GetDefaultFontRole(FontRole::Body);
			logger::warn("Font '{}' not found while loading settings, falling back to default font '{}'",
				bodyRole.File, defaults.File);
			settings.Theme.FontRoles[static_cast<size_t>(FontRole::Body)] = defaults;
			settings.Theme.FontName = defaults.File;
		}
		logger::info("Loaded legacy Theme data from config (no SelectedThemePreset)");
	}

	// Apply Default Dark theme on first launch if no theme is selected
	if (!settings.FirstTimeSetupCompleted && settings.SelectedThemePreset.empty()) {
		// Ensure default themes are created/available
		CreateDefaultThemes();

		// Load the Default Dark theme and mark it as selected to prevent override
		if (LoadThemePreset("Default")) {
			settings.SelectedThemePreset = "Default";  // Mark as selected to prevent State::LoadTheme override
			logger::info("Applied Default Dark theme on first launch");
		} else {
			logger::warn("Failed to load Default Dark theme on first launch");
		}
	} else if (!settings.SelectedThemePreset.empty()) {
		// Load the previously selected theme preset (including custom themes)
		if (LoadThemePreset(settings.SelectedThemePreset)) {
			logger::info("Loaded saved theme preset: {}", settings.SelectedThemePreset);
		} else {
			logger::warn("Failed to load saved theme preset '{}', falling back to Default", settings.SelectedThemePreset);
			if (LoadThemePreset("Default")) {
				settings.SelectedThemePreset = "Default";
			}
		}
	}
}

void Menu::Save(json& o_json)
{
	settings.Theme.FontName = settings.Theme.FontRoles[static_cast<size_t>(FontRole::Body)].File;

	// Save all settings except Theme values
	// Theme values should only be saved in theme preset files, not in the main config
	o_json = settings;

	// Remove Theme object from config, only keep SelectedThemePreset
	o_json.erase("Theme");
}

void Menu::LoadTheme(json& o_json)
{
	if (o_json["Theme"].is_object()) {
		bool hasFontRoles = o_json["Theme"].contains("FontRoles");
		settings.Theme = o_json["Theme"];
		MenuFonts::NormalizeFontRoles(settings.Theme, hasFontRoles);

		auto& bodyRole = settings.Theme.FontRoles[static_cast<size_t>(FontRole::Body)];
		if (!Util::ValidateFont(bodyRole.File)) {
			const auto& defaults = Menu::GetDefaultFontRole(FontRole::Body);
			logger::warn("Font '{}' not found, falling back to default font '{}'",
				bodyRole.File, defaults.File);
			settings.Theme.FontRoles[static_cast<size_t>(FontRole::Body)] = defaults;
			settings.Theme.FontName = defaults.File;
		}

		// Apply background blur enabled state from theme
		BackgroundBlur::SetEnabled(settings.Theme.BackgroundBlurEnabled);
	}
}
void Menu::SaveTheme(json& o_json)
{
	settings.Theme.FontName = settings.Theme.FontRoles[static_cast<size_t>(FontRole::Body)].File;

	if (!Util::ValidateFont(settings.Theme.FontName)) {
		const auto& defaults = Menu::GetDefaultFontRole(FontRole::Body);
		logger::warn("Font '{}' not found during save, falling back to default font '{}'",
			settings.Theme.FontName, defaults.File);
		settings.Theme.FontRoles[static_cast<size_t>(FontRole::Body)] = defaults;
		settings.Theme.FontName = defaults.File;
	}

	o_json["Theme"] = settings.Theme;
}

std::vector<std::string> Menu::DiscoverThemes()
{
	auto themeManager = ThemeManager::GetSingleton();
	if (themeManager) {
		themeManager->DiscoverThemes();
		return themeManager->GetThemeNames();
	}
	return {};
}

bool Menu::LoadThemePreset(const std::string& themeName)
{
	if (themeName.empty()) {
		// Empty theme name means custom/user theme
		settings.SelectedThemePreset = "";
		return true;
	}

	auto themeManager = ThemeManager::GetSingleton();
	json themeSettings;

	if (themeManager->LoadTheme(themeName, themeSettings)) {
		// Create a backup of current theme in case loading fails
		ThemeSettings backupTheme = settings.Theme;
		ThemeSettings defaultTheme;  // For fallback values
		bool hasFontRoles = themeSettings.contains("FontRoles");

		try {
			// Attempt to load theme with protection against malformed data
			try {
				settings.Theme = themeSettings;
			} catch (const json::out_of_range& e) {
				// Most likely FullPalette array size mismatch
				logger::warn("Theme '{}' has incomplete data ({}). Loading with defaults for missing fields.", themeName, e.what());

				// Manually load fields that exist, use defaults for missing ones
				if (themeSettings.contains("FontSize")) {
					try {
						settings.Theme.FontSize = themeSettings["FontSize"];
					} catch (...) {}
				}
				if (themeSettings.contains("FontName")) {
					try {
						settings.Theme.FontName = themeSettings["FontName"];
					} catch (...) {}
				}
				if (themeSettings.contains("GlobalScale")) {
					try {
						settings.Theme.GlobalScale = themeSettings["GlobalScale"];
					} catch (...) {}
				}
				if (themeSettings.contains("FontRoles")) {
					try {
						settings.Theme.FontRoles = themeSettings["FontRoles"];
					} catch (...) {}
				}
				if (themeSettings.contains("ShowActionIcons")) {
					try {
						settings.Theme.ShowActionIcons = themeSettings["ShowActionIcons"];
					} catch (...) {}
				}
				if (themeSettings.contains("UseMonochromeIcons")) {
					try {
						settings.Theme.UseMonochromeIcons = themeSettings["UseMonochromeIcons"];
					} catch (...) {}
				}
				if (themeSettings.contains("UseMonochromeLogo")) {
					try {
						settings.Theme.UseMonochromeLogo = themeSettings["UseMonochromeLogo"];
					} catch (...) {}
				}
				if (themeSettings.contains("TooltipHoverDelay")) {
					try {
						settings.Theme.TooltipHoverDelay = themeSettings["TooltipHoverDelay"];
					} catch (...) {}
				}
				if (themeSettings.contains("BackgroundBlurEnabled")) {
					try {
						settings.Theme.BackgroundBlurEnabled = themeSettings["BackgroundBlurEnabled"];
					} catch (...) {}
				}
				if (themeSettings.contains("ScrollbarOpacity")) {
					try {
						settings.Theme.ScrollbarOpacity = themeSettings["ScrollbarOpacity"];
					} catch (...) {}
				}
				if (themeSettings.contains("Palette")) {
					try {
						settings.Theme.Palette = themeSettings["Palette"];
					} catch (...) {}
				}
				if (themeSettings.contains("StatusPalette")) {
					try {
						settings.Theme.StatusPalette = themeSettings["StatusPalette"];
					} catch (...) {}
				}
				if (themeSettings.contains("FeatureHeading")) {
					try {
						settings.Theme.FeatureHeading = themeSettings["FeatureHeading"];
					} catch (...) {}
				}
				if (themeSettings.contains("Style")) {
					try {
						settings.Theme.Style = themeSettings["Style"];
					} catch (...) {}
				}

				// Handle FullPalette with extra care
				if (themeSettings.contains("FullPalette") && themeSettings["FullPalette"].is_array()) {
					const auto& paletteJson = themeSettings["FullPalette"];
					size_t jsonSize = paletteJson.size();
					size_t requiredSize = settings.Theme.FullPalette.size();  // Should be ImGuiCol_COUNT (55)

					if (jsonSize < requiredSize) {
						logger::warn("Theme '{}' FullPalette has {} elements, expected {}. Using defaults for missing colors.",
							themeName, jsonSize, requiredSize);
					}

					// Load colors that exist, use defaults for the rest
					for (size_t i = 0; i < requiredSize; ++i) {
						if (i < jsonSize) {
							try {
								if (paletteJson[i].is_array() && paletteJson[i].size() >= 4) {
									settings.Theme.FullPalette[i] = ImVec4(
										paletteJson[i][0].get<float>(),
										paletteJson[i][1].get<float>(),
										paletteJson[i][2].get<float>(),
										paletteJson[i][3].get<float>());
								} else {
									settings.Theme.FullPalette[i] = defaultTheme.FullPalette[i];
								}
							} catch (...) {
								settings.Theme.FullPalette[i] = defaultTheme.FullPalette[i];
							}
						} else {
							settings.Theme.FullPalette[i] = defaultTheme.FullPalette[i];
						}
					}
				} else {
					// FullPalette missing, use all defaults
					logger::warn("Theme '{}' missing FullPalette array, using defaults", themeName);
					settings.Theme.FullPalette = defaultTheme.FullPalette;
				}
			} catch (const std::exception& e) {
				logger::error("Error loading theme '{}': {}. Using previous theme.", themeName, e.what());
				settings.Theme = backupTheme;
				return false;
			}

			MenuFonts::NormalizeFontRoles(settings.Theme, hasFontRoles);
			auto& bodyRole = settings.Theme.FontRoles[static_cast<size_t>(FontRole::Body)];
			if (!Util::ValidateFont(bodyRole.File)) {
				const auto& defaults = Menu::GetDefaultFontRole(FontRole::Body);
				logger::warn("Font '{}' from theme '{}' not found, falling back to default font '{}'",
					bodyRole.File, themeName, defaults.File);
				settings.Theme.FontRoles[static_cast<size_t>(FontRole::Body)] = defaults;
				settings.Theme.FontName = defaults.File;
			}

			settings.SelectedThemePreset = themeName;

			// Schedule deferred font reload if font has changed
			if (settings.Theme.FontName != cachedFontName) {
				pendingFontReload = true;
			}

			// Schedule deferred icon reload to apply theme-specific icon overrides
			pendingIconReload = true;

			// Apply background blur enabled state from theme
			BackgroundBlur::SetEnabled(settings.Theme.BackgroundBlurEnabled);

			logger::info("Applied theme preset: {}", themeName);
			return true;
		} catch (const std::exception& e) {
			logger::warn("Error loading theme '{}': {}", themeName, e.what());
			// Restore backup to maintain UI consistency
			settings.Theme = backupTheme;
			return false;
		}
	} else {
		logger::warn("Failed to load theme preset: {}", themeName);
		return false;
	}
}

void Menu::CreateDefaultThemes()
{
	auto themeManager = ThemeManager::GetSingleton();
	themeManager->CreateDefaultThemeFiles();
}

void Menu::Init()
{
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	// IMPORTANT: Immediately override ImGui's default styles with our Default.json theme
	// This prevents hardcoded ImGui defaults from ever showing through
	auto* themeManager = ThemeManager::GetSingleton();
	json defaultThemeSettings;
	if (themeManager->LoadTheme("Default", defaultThemeSettings)) {
		// Temporarily create a minimal theme structure to apply defaults
		json tempSettings;
		tempSettings["Theme"] = defaultThemeSettings;
		LoadTheme(tempSettings);
		logger::info("Applied Default.json theme immediately after ImGui context creation");
	} else {
		logger::warn("Could not load Default.json theme - trying direct force application");
		// Last resort: Apply Default.json colors directly to ImGui
		ThemeManager::ForceApplyDefaultTheme();
	}

	// Re-apply user-selected preset after defaults are applied (covers Default and custom)
	if (!settings.SelectedThemePreset.empty()) {
		auto themeManagerSingleton = ThemeManager::GetSingleton();
		if (themeManagerSingleton && !themeManagerSingleton->IsDiscovered()) {
			themeManagerSingleton->DiscoverThemes();
		}

		if (!LoadThemePreset(settings.SelectedThemePreset)) {
			logger::warn("Failed to re-apply preset '{}' during Menu::Init. Keeping Default.", settings.SelectedThemePreset);
		} else {
			logger::info("Re-applied preset '{}' during Menu::Init", settings.SelectedThemePreset);
		}
	}

	auto& imgui_io = ImGui::GetIO();
	imgui_io.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_DockingEnable;
	imgui_io.BackendFlags = ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_HasGamepad;

	cachedIniPath = Util::PathHelpers::GetImGuiIniPath().string();
	imgui_io.IniFilename = cachedIniPath.c_str();

	DXGI_SWAP_CHAIN_DESC desc{};
	globals::d3d::swapChain->GetDesc(&desc);

	// Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(desc.OutputWindow);
	ImGui_ImplDX11_Init(globals::d3d::device, globals::d3d::context);

	ThemeManager::ReloadFont(*this, cachedFontSize);

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

	// Initialize background blur system
	if (!BackgroundBlur::Initialize()) {
		logger::warn("Menu::Init() - Failed to initialize background blur system");
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

	// Apply theme styling with universal contrast enhancement
	ThemeManager::SetupImGuiStyle(*this);

	ImGui::DockSpaceOverViewport(NULL, ImGuiDockNodeFlags_PassthruCentralNode);

	ImGui::SetNextWindowPos(Util::GetNativeViewportSizeScaled(0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(Util::GetNativeViewportSizeScaled(0.8f), ImGuiCond_FirstUseEver);
	auto title = std::format("Community Shaders {}", Util::GetFormattedVersion(Plugin::VERSION));

	if (EditorWindow::GetSingleton()->open) {
		EditorWindow::GetSingleton()->Draw();
		return;
	}

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

		float globalScale = settings.Theme.GlobalScale;

		// Use default global scale (0.0) for built-in themes when GlobalScale equals the default
		if (std::abs(globalScale - ThemeManager::Constants::DEFAULT_GLOBAL_SCALE) < 0.001f) {
			globalScale = ThemeManager::Constants::DEFAULT_GLOBAL_SCALE;  // Ensure built-in themes stay at 0.0
		}

		const float uiScale = exp2(globalScale);  // Get current UI scale
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

		float footer_height = settings.Theme.ShowFooter ?
		                          (ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 3 + 3.0f) :
		                          0.0f;

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
			[&]() { DrawAdvancedSettings(); });

		if (settings.Theme.ShowFooter) {
			ImGui::Spacing();
			ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal, ThemeManager::Constants::SEPARATOR_THICKNESS);
			ImGui::Spacing();
			DrawFooter();
		}
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
		.settingOverlayToggleKey = settingOverlayToggleKey,
		.settingShaderBlockPrevKey = settingShaderBlockPrevKey,
		.settingShaderBlockNextKey = settingShaderBlockNextKey
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
		[this]() { globals::truePBR->DrawSettings(); },
		[this]() { DrawDisableAtBootSettings(); });
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

	if (ImGui::CollapsingHeader("Features", ImGuiTreeNodeFlags_DefaultOpen)) {
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

void Menu::DrawFooter()
{
	ImGui::BulletText(std::format("Game Version: {} {}", magic_enum::enum_name(REL::Module::GetRuntime()), Util::GetFormattedVersion(REL::Module::get().version()).c_str()).c_str());
	ImGui::SameLine();
	ImGui::BulletText(std::format("D3D12 Swap Chain: {}", globals::features::upscaling.d3d12SwapChainActive ? "Active" : "Inactive").c_str());
	ImGui::SameLine();
	ImGui::BulletText(std::format("GPU: {}", globals::state->adapterDescription.c_str()).c_str());
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
	// Only process reloads when ImGui is NOT in an active frame
	ImGuiContext* ctx = ImGui::GetCurrentContext();
	bool canReload = ctx && !ctx->WithinFrameScope && !ctx->WithinEndChild;

	// Process deferred font reload BEFORE any ImGui operations
	// This is the safest place to do font atlas modifications
	if (pendingFontReload && canReload) {
		// Call ReloadFont first - only clear flag if it succeeds
		if (ThemeManager::ReloadFont(*this, cachedFontSize)) {
			// Reload completed successfully
			pendingFontReload = false;
		} else {
			// Reload failed - keep flag true to retry next frame
			logger::warn("Menu::DrawOverlay() - Font reload failed, will retry next frame");
		}
	}

	// Process deferred icon reload BEFORE rendering
	if (pendingIconReload && canReload) {
		if (Util::IconLoader::InitializeMenuIcons(this)) {
			pendingIconReload = false;
		} else {
			logger::warn("Menu::DrawOverlay() - Icon reload failed, will retry next frame");
		}
	}

	OverlayRenderer::RenderOverlay(
		*this,
		[this]() { ProcessInputEventQueue(); },
		[this]() { DrawSettings(); },
		[](uint32_t key) { return Util::Input::KeyIdToString(key); },
		cachedFontSize,
		ThemeManager::ResolveFontSize(*this));
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
				HotkeyAction hotkeyActions[] = {
					{ &settings.ToggleKey, &settingToggleKey, [this](uint32_t key) { settings.ToggleKey = key; settingToggleKey = false; } },
					{ &settings.SkipCompilationKey, &settingSkipCompilationKey, [this](uint32_t key) { settings.SkipCompilationKey = key; settingSkipCompilationKey = false; } },
					{ &settings.EffectToggleKey, &settingsEffectsToggle, [this](uint32_t key) { settings.EffectToggleKey = key; settingsEffectsToggle = false; } },
					{ &settings.OverlayToggleKey, &settingOverlayToggleKey, [this](uint32_t key) { settings.OverlayToggleKey = key; settingOverlayToggleKey = false; } },
					{ &settings.ShaderBlockPrevKey, &settingShaderBlockPrevKey, [this](uint32_t key) { settings.ShaderBlockPrevKey = key; settingShaderBlockPrevKey = false; } },
					{ &settings.ShaderBlockNextKey, &settingShaderBlockNextKey, [this](uint32_t key) { settings.ShaderBlockNextKey = key; settingShaderBlockNextKey = false; } },
				};
				bool handled = false;
				for (auto& h : hotkeyActions) {
					if (*(h.settingFlag)) {
						// During first-time setup, don't capture Enter or Escape as hotkeys
						// These keys are reserved for closing the dialog
						if (HomePageRenderer::ShouldShowFirstTimeSetup() && (key == VK_RETURN || key == VK_ESCAPE)) {
							*(h.settingFlag) = false;  // Cancel hotkey capture mode
							handled = true;
							break;
						}
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
						{ settings.ShaderBlockPrevKey, [this, shaderCache]() { if (settings.EnableShaderBlocking) shaderCache->IterateShaderBlock(); } },
						{ settings.ShaderBlockNextKey, [this, shaderCache]() { if (settings.EnableShaderBlocking) shaderCache->IterateShaderBlock(false); } },
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
				// Guard against a null EditorWindow singleton before accessing `open`.
				{
					auto* editorWindow = EditorWindow::GetSingleton();
					if (key == VK_ESCAPE && IsEnabled && editorWindow && !editorWindow->open) {
						IsEnabled = false;
					}
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
	return IsEnabled || HomePageRenderer::ShouldShowFirstTimeSetup();
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
