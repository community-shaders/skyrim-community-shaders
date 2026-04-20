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
#include "Features/WeatherEditor.h"
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
	MinimizedFactor,
	FeatureTitleScale)

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
	ImageRounding,
	ImageBorderSize,
	TabRounding,
	TabBorderSize,
	TabCloseButtonMinWidthSelected,
	TabCloseButtonMinWidthUnselected,
	TabBarBorderSize,
	TabBarOverlineSize,
	TableAngledHeadersAngle,
	TableAngledHeadersTextAlign,
	TreeLinesSize,
	TreeLinesRounding,
	DragDropTargetRounding,
	DragDropTargetBorderSize,
	DragDropTargetPadding,
	ColorMarkerSize,
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
	Style)

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
	FirstTimeSetupCompleted,
	SkipClearCacheConfirmation,
	AutoHideFeatureList,
	SkipConstraintWarning,
	RequireShiftToDock,
	Theme,
	SelectedThemePreset)

bool IsEnabled = false;
std::unordered_map<std::string, int> Menu::categoryCounts;

// Pad FontRoles JSON array with defaults if shorter than FontRole::Count.
// Prevents deserialization failure when loading old settings with fewer font roles.
static void SanitizeFontRolesJson(json& themeJson)
{
	if (!themeJson.contains("FontRoles") || !themeJson["FontRoles"].is_array())
		return;

	auto& fontRoles = themeJson["FontRoles"];
	const size_t expected = static_cast<size_t>(Menu::FontRole::Count);

	if (fontRoles.size() < expected) {
		auto defaults = Menu::ThemeSettings{}.FontRoles;
		for (size_t i = fontRoles.size(); i < expected; ++i) {
			fontRoles.push_back(defaults[i]);
		}
	}
}

// Serialize palette as named color map. Resilient to ImGui enum reordering.
void Menu::PaletteToJson(json& themeJson, const std::array<ImVec4, ImGuiCol_COUNT>& palette)
{
	json colors = json::object();
	for (int i = 0; i < ImGuiCol_COUNT; i++)
		colors[ImGui::GetStyleColorName(i)] = palette[i];
	themeJson["Colors"] = colors;
}

// Deserialize palette from named color map (preferred) or legacy positional array.
void Menu::PaletteFromJson(const json& themeJson, std::array<ImVec4, ImGuiCol_COUNT>& palette)
{
	ThemeSettings defaults;
	palette = defaults.FullPalette;

	auto loadVec4 = [](const json& c) -> ImVec4 {
		if (c.is_array() && c.size() >= 4)
			return c.get<ImVec4>();
		return ImVec4(0, 0, 0, 0);
	};

	if (themeJson.contains("Colors") && themeJson["Colors"].is_object()) {
		// Named color map: look up each color by ImGui's style color name
		const auto& colors = themeJson["Colors"];
		for (int i = 0; i < ImGuiCol_COUNT; i++) {
			const char* name = ImGui::GetStyleColorName(i);
			if (colors.contains(name) && colors[name].is_array())
				palette[i] = loadVec4(colors[name]);
		}
	} else if (themeJson.contains("FullPalette") && themeJson["FullPalette"].is_array()) {
		// Legacy positional array
		const auto& arr = themeJson["FullPalette"];

		if (arr.size() == 55) {
			// Migrate from ImGui 1.90 (55 entries) to 1.92+ (62 entries).
			// Tab/TabHovered swapped, 7 new slots inserted mid-enum.
			for (int i = 0; i <= 32; i++)
				palette[i] = loadVec4(arr[i]);
			// [33] InputTextCursor: stays default
			palette[34] = loadVec4(arr[34]);  // old TabHovered → TabHovered
			palette[35] = loadVec4(arr[33]);  // old Tab → Tab (swapped)
			palette[36] = loadVec4(arr[35]);  // old TabActive → TabSelected
			// [37] TabSelectedOverline: stays default
			palette[38] = loadVec4(arr[36]);  // old TabUnfocused → TabDimmed
			palette[39] = loadVec4(arr[37]);  // old TabUnfocusedActive → TabDimmedSelected
			// [40] TabDimmedSelectedOverline: stays default
			for (int i = 38; i <= 48; i++)
				palette[i + 3] = loadVec4(arr[i]);
			// [52] TextLink: stays default
			palette[53] = loadVec4(arr[49]);  // TextSelectedBg
			// [54] TreeLines: stays default
			palette[55] = loadVec4(arr[50]);  // DragDropTarget
			// [56] DragDropTargetBg: stays default
			// [57] UnsavedMarker: stays default
			for (int i = 51; i <= 54; i++)
				palette[i + 7] = loadVec4(arr[i]);
		} else {
			// Direct positional load (matching or close size)
			size_t count = std::min(arr.size(), static_cast<size_t>(ImGuiCol_COUNT));
			for (size_t i = 0; i < count; i++)
				palette[i] = loadVec4(arr[i]);
		}
	}
}

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
	uiIcons.freeCamera.Release();
	uiIcons.playMode.Release();
	uiIcons.search.Release();

	// Clean up blur resources
	BackgroundBlur::Cleanup();

	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	dxgiAdapter3 = nullptr;
}

void Menu::Load(json& o_json)
{
	// Store current Theme state before loading config
	auto currentTheme = settings.Theme;

	settings = o_json;

	// Restore Theme - don't load it from config, only from theme preset files
	settings.Theme = currentTheme;

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

	// Helper for new smart serialization with error handling
	auto loadComboList = [](const json& j, const char* keyName, std::vector<InputCombo>& target) {
		if (j.contains(keyName) && j[keyName].is_array()) {
			try {
				InputCombo::ComboList::from_json(j[keyName], target);
			} catch (const std::exception& e) {
				logger::warn("Failed to load combo list '{}': {}, using default", keyName, e.what());
				// Leave target unchanged (keeps default or migrated value)
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

	// Legacy support: If old config has Theme data and no SelectedThemePreset, load it
	if (o_json.contains("Theme") && o_json["Theme"].is_object() && settings.SelectedThemePreset.empty()) {
		bool hasFontRoles = o_json["Theme"].contains("FontRoles");
		SanitizeFontRolesJson(o_json["Theme"]);
		settings.Theme = o_json["Theme"];
		PaletteFromJson(o_json["Theme"], settings.Theme.FullPalette);
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

	// Manually save input combos using the smart serializer
	InputCombo::ComboList::to_json(o_json["ToggleKey"], settings.ToggleKey);
	InputCombo::ComboList::to_json(o_json["SkipCompilationKey"], settings.SkipCompilationKey);
	InputCombo::ComboList::to_json(o_json["EffectToggleKey"], settings.EffectToggleKey);
	InputCombo::ComboList::to_json(o_json["OverlayToggleKey"], settings.OverlayToggleKey);
	InputCombo::ComboList::to_json(o_json["ShaderBlockPrevKey"], settings.ShaderBlockPrevKey);
	InputCombo::ComboList::to_json(o_json["ShaderBlockNextKey"], settings.ShaderBlockNextKey);
	InputCombo::ComboList::to_json(o_json["WeatherEditorToggleKey"], settings.WeatherEditorToggleKey);
}

void Menu::LoadTheme(json& o_json)
{
	if (o_json["Theme"].is_object()) {
		bool hasFontRoles = o_json["Theme"].contains("FontRoles");
		SanitizeFontRolesJson(o_json["Theme"]);
		settings.Theme = o_json["Theme"];
		PaletteFromJson(o_json["Theme"], settings.Theme.FullPalette);
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
	PaletteToJson(o_json["Theme"], settings.Theme.FullPalette);
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
		bool hasFontRoles = themeSettings.contains("FontRoles");

		SanitizeFontRolesJson(themeSettings);

		try {
			settings.Theme = themeSettings;
			PaletteFromJson(themeSettings, settings.Theme.FullPalette);

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
	imgui_io.ConfigDockingWithShift = settings.RequireShiftToDock;
	imgui_io.BackendFlags = ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_HasGamepad;

	cachedIniPath = Util::PathHelpers::GetImGuiIniPath().string();
	imgui_io.IniFilename = cachedIniPath.c_str();

	// Register settings handler to persist display size for cross-session resolution change detection
	ImGuiSettingsHandler handler{};
	handler.TypeName = "CommunityShaders";
	handler.TypeHash = ImHashStr("CommunityShaders");
	handler.UserData = &lastDisplaySize;
	handler.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler*, const char*) -> void* { return (void*)1; };
	handler.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler* h, void*, const char* line) {
		float w, ht;
		if (sscanf(line, "DisplaySize=%f,%f", &w, &ht) == 2)
			*static_cast<float2*>(h->UserData) = { w, ht };
	};
	handler.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* h, ImGuiTextBuffer* buf) {
		auto& ds = ImGui::GetIO().DisplaySize;
		buf->appendf("[%s][Data]\nDisplaySize=%g,%g\n\n", h->TypeName, ds.x, ds.y);
	};
	ImGui::GetCurrentContext()->SettingsHandlers.push_back(handler);

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

	ImGui::DockSpaceOverViewport(0, NULL, ImGuiDockNodeFlags_PassthruCentralNode);

	const auto layoutCond = resetLayout ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
	ImGui::SetNextWindowPos(Util::GetNativeViewportSizeScaled(0.5f), layoutCond, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(Util::GetNativeViewportSizeScaled(0.8f), layoutCond);
	resetLayout = false;
	auto versionStr = Util::GetFormattedVersion(Plugin::VERSION);
	auto expectedTag = std::format("v{}", versionStr);
	auto displayTitle = Plugin::BUILD_DESCRIBE == expectedTag ? std::format("Community Shaders {}", versionStr) : std::format("Community Shaders {} [{}]", versionStr, Plugin::BUILD_DESCRIBE);
	// Use ### to keep a stable window ID regardless of build suffix, preserving docking state
	auto title = std::format("{}###CommunityShaders", displayTitle);

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

		const float uiScale = exp2(globalScale);  // User's manual GlobalScale for header icons
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
		                          (ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 3) :
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

		// Draw global popups (needs to be called once per frame)
		Util::DrawClearShaderCacheConfirmation();
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
		.settingShaderBlockNextKey = settingShaderBlockNextKey,
		.settingWeatherEditorToggleKey = settingWeatherEditorToggleKey
	};

	// Render settings using extracted component
	SettingsTabRenderer::RenderGeneralSettings(state);
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
	bool canReload = ctx && !ctx->WithinFrameScope && ctx->WithinEndChildID == 0;

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
		[](std::vector<InputCombo> keys) -> const char* {
			static std::string result_cache;
			result_cache = Util::Input::KeyIdToString(keys);
			return result_cache.c_str();
		},
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

	// Process non-VR events in Menu
	for (auto& event : nonVREvents) {
		if (event.eventType == RE::INPUT_EVENT_TYPE::kChar) {
			io.AddInputCharacter(event.keyCode);
			continue;
		}
		if (event.device == RE::INPUT_DEVICE::kMouse) {
			logger::trace("Detect mouse scan code {} value {} pressed: {}", event.keyCode, event.value, event.IsPressed());
			auto* ew = EditorWindow::GetSingleton();
			bool flying = ew && ew->IsPreviewFlying();
			if (event.keyCode > 7) {  // middle scroll
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

		if (event.device == RE::INPUT_DEVICE::kKeyboard) {
			uint32_t key = Util::Input::DIKToVK(event.keyCode);
			logger::trace("Detected key code {} ({})", event.keyCode, key);
			if (key == event.keyCode)
				key = MapVirtualKeyEx(event.keyCode, MAPVK_VSC_TO_VK_EX, GetKeyboardLayout(0));

			const bool wasCapturingHotkey = IsCapturingHotkeyInput();
			const bool allowSetupCloseKey = wasCapturingHotkey && HomePageRenderer::ShouldShowFirstTimeSetup() &&
			                                (key == VK_RETURN || key == VK_ESCAPE);
			if (!event.IsPressed()) {
				// Skip key release if it was used to close the first-time setup dialog
				if (HomePageRenderer::ShouldSkipKeyRelease(key)) {
					io.AddKeyEvent(Util::Input::VirtualKeyToImGuiKey(key), event.IsPressed());
					continue;
				}

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
						// During first-time setup, don't capture Enter or Escape as hotkeys
						// These keys are reserved for closing the dialog, unless we are recording a modifier
						if (HomePageRenderer::ShouldShowFirstTimeSetup() && (key == VK_RETURN || key == VK_ESCAPE)) {
							// Do not stop capture here, just let it pass through to the UI
							// The UI code in HomePageRenderer checks for Enter/Escape and completes setup
							*(h.settingFlag) = false;  // Cancel hotkey capture mode
							handled = true;
							break;
						}

						// Ignore modifier-only key releases during recording
						bool isModifier = (key == VK_CONTROL || key == VK_LCONTROL || key == VK_RCONTROL ||
										   key == VK_SHIFT || key == VK_LSHIFT || key == VK_RSHIFT ||
										   key == VK_MENU || key == VK_LMENU || key == VK_RMENU);

						if (isModifier) {
							handled = true;
							break;
						}

						// Capture modifiers + key
						std::vector<InputCombo> combo;

						// Add active modifiers to combo
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
						{ settings.ToggleKey, [this]() {
							 if (!HomePageRenderer::ShouldShowFirstTimeSetup()) {
								 IsEnabled = !IsEnabled;
								 if (IsEnabled)
									 ImGui::GetIO().ClearInputKeys();  // Prevent toggle key from remaining "held" in ImGui after open.
							 }
						 } },
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
								 // Flying → lock camera position for editing
								 ew->ToggleFreeCameraLock();
							 } else if (ew->IsInPreviewMode()) {
								 // Locked or PlayMode → fully exit preview
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

			// Don't forward hotkey events to ImGui when input is captured (prevents e.g. End key scrolling the feature list)
			// SkipCompilationKey (ESC) is excluded — ESC must reach ImGui for menu/dialog close.
			const std::vector<InputCombo>* hotkeys[] = {
				&settings.ToggleKey, &settings.EffectToggleKey,
				&settings.OverlayToggleKey, &settings.ShaderBlockPrevKey, &settings.ShaderBlockNextKey,
				&settings.WeatherEditorToggleKey
			};
			bool isHotkey = ShouldSwallowInput() && std::any_of(std::begin(hotkeys), std::end(hotkeys),
														[key](const auto* combo) { return InputCombo::MatchesKeyboardCombo(*combo, key); });

			// Always forward key-up events. Suppress key-down during active hotkeys,
			// and during hotkey capture except setup close keys (Enter/Escape).
			const bool isKeyDown = event.IsPressed();
			const bool suppressForwarding = isKeyDown && (isHotkey || (wasCapturingHotkey && !allowSetupCloseKey));
			if (!suppressForwarding) {
				// DirectInput loses key-up events after alt-tab; validate against OS state.
				bool pressed = isKeyDown && (GetAsyncKeyState(key) & Constants::KEY_PRESSED_MASK);
				io.AddKeyEvent(Util::Input::VirtualKeyToImGuiKey(key), pressed);

				if (key == VK_LCONTROL || key == VK_RCONTROL)
					io.AddKeyEvent(ImGuiMod_Ctrl, pressed);
				else if (key == VK_LSHIFT || key == VK_RSHIFT)
					io.AddKeyEvent(ImGuiMod_Shift, pressed);
				else if (key == VK_LMENU || key == VK_RMENU)
					io.AddKeyEvent(ImGuiMod_Alt, pressed);
			}
		}
	}

	_keyEventQueue.clear();
}

bool Menu::IsCapturingHotkeyInput() const
{
	return settingToggleKey || settingSkipCompilationKey || settingsEffectsToggle ||
	       settingOverlayToggleKey || settingShaderBlockPrevKey || settingShaderBlockNextKey || settingWeatherEditorToggleKey;
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
			device->ClearInputState();
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
	auto editorWindow = EditorWindow::GetSingleton();
	return IsEnabled || HomePageRenderer::ShouldShowFirstTimeSetup() || (editorWindow && editorWindow->open);
}

bool Menu::IsPreviewFlying()
{
	auto editorWindow = EditorWindow::GetSingleton();
	return editorWindow && editorWindow->IsPreviewFlying();
}

void Menu::SelectFeatureMenu(const std::string& featureName)
{
	pendingFeatureSelection = featureName;
	logger::info("Queued navigation to {} feature menu", featureName);
}

/**
 * @brief Renders the standalone weather details window when enabled
 *
 * Delegates to the WeatherEditor feature for rendering the weather details window
 * that can remain open even when the main menu is closed. This provides a simple
 * coordination layer between the Menu system and the WeatherEditor feature.
 */
void Menu::DrawWeatherDetailsWindow()
{
	if (!globals::features::weatherEditor.WeatherDetailsWindow.Enabled) {
		return;
	}
	if (!globals::features::weatherEditor.loaded) {
		return;
	}

	// Use Weather core feature for all window management and rendering
	auto& weather = globals::features::weatherEditor;
	bool* p_open = &globals::features::weatherEditor.WeatherDetailsWindow.Enabled;
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
