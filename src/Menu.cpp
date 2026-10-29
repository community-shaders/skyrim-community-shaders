#include "Menu.h"
#include "Menu/FeatureUndo.h"
#include "Menu/FontAtlasState.h"
#include "Utils/Notifications.h"

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
#include "Features/RenderDoc.h"
#include "Features/Upscaling.h"
#include "I18n/I18n.h"
#include "Menu/BackgroundBlur.h"
#include "Menu/CursorLoader.h"
#include "Menu/FeatureListRenderer.h"
#include "Menu/Fonts.h"
#include "Menu/GoToPalette.h"
#include "Menu/HomePageRenderer.h"
#include "Menu/IconLoader.h"
#include "Menu/MenuHeaderRenderer.h"
#include "Menu/MenuShortcuts.h"
#include "Menu/OverlayRenderer.h"
#include "Menu/PresetsPageRenderer.h"
#include "Menu/SettingsPageRenderer.h"
#include "Menu/StatusBarRenderer.h"
#include "Menu/ThemeManager.h"
#include "SaveManager.h"
#include "ShaderCache.h"
#include "State.h"
#include "Util.h"
#include "Utils/UI.h"
#include "Utils/UIHooks.h"

#include "CSEditor/Browser/BrowserWidgets.h"
#include "CSEditor/EditorWindow.h"
#include "CSEditor/SceneManager/SceneSettingsManager.h"
#include "CSEditor/SceneManager/SceneWidgetInterceptor.h"
#include "Features/CSEditor.h"
#include "Features/Effects11.h"
#include "Features/Effects11/Editor/Effects11Editor.h"
#include "Features/PerformanceOverlay.h"
#include "Features/PerformanceOverlay/ABTesting/ABTestAggregator.h"
#include "Features/PerformanceOverlay/ABTesting/ABTesting.h"
#include "Features/ScreenshotFeature.h"

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
	Menu::ThemeSettings::CursorImageSettings,
	File,
	HotspotX,
	HotspotY)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	Menu::ThemeSettings::CursorSettings,
	Scale,
	File,
	HotspotX,
	HotspotY)

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
	MenuFadeEnabled,
	UseCustomCursor,
	Cursor,
	ScrollbarOpacity,
	Surfaces,
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
	CSEditorToggleKey,
	EnableShaderBlocking,
	FirstTimeSetupCompleted,
	SkipClearCacheConfirmation,
	AutoHideFeatureList,
	ReduceUIAnimations,
	AnimationSpeed,
	FeatureListSort,
	SkipConstraintWarning,
	RequireShiftToDock,
	UseResolutionFont,
	Theme,
	SelectedThemePreset)

bool IsEnabled = false;

namespace
{
	/**
	 * @brief FeatureUndo's target for a feature, plus the bookkeeping DrawFeatureBody does after a settings
	 * rewrite. Its restore already marks the settings changed and drops the cached baseline-edits answer;
	 * the scene layer's cached apply document must also take the restored values before anything replays it.
	 */
	UndoHistory::Target MakeMenuFeatureUndoTarget(Feature* feature)
	{
		auto target = FeatureUndo::MakeTarget(feature);
		target.restore = [restore = std::move(target.restore), feature](const UndoHistory::Snapshot& snapshot) {
			restore(snapshot);
			globals::sceneSettingsManager->CaptureExternalFeatureChanges(feature, true);
		};
		return target;
	}
}
std::unordered_map<std::string, int> Menu::categoryCounts;

namespace
{
	struct CursorTypeKey
	{
		const char* key;
		ImGuiMouseCursor type;
	};

	constexpr CursorTypeKey kCursorTypeKeys[] = {
		{ "Arrow", ImGuiMouseCursor_Arrow },
		{ "TextInput", ImGuiMouseCursor_TextInput },
		{ "ResizeAll", ImGuiMouseCursor_ResizeAll },
		{ "ResizeNS", ImGuiMouseCursor_ResizeNS },
		{ "ResizeEW", ImGuiMouseCursor_ResizeEW },
		{ "ResizeNESW", ImGuiMouseCursor_ResizeNESW },
		{ "ResizeNWSE", ImGuiMouseCursor_ResizeNWSE },
		{ "Hand", ImGuiMouseCursor_Hand },
		{ "NotAllowed", ImGuiMouseCursor_NotAllowed },
	};
}

void Menu::ThemeFromJson(const json& themeJson, ThemeSettings& theme)
{
	theme = themeJson.get<ThemeSettings>();
	PaletteFromJson(themeJson, theme.FullPalette);
}

void Menu::CursorFromJson(const json& cursorJson, ThemeSettings::CursorSettings& cursor)
{
	cursor.Types = {};

	if (!cursorJson.contains("Types")) {
		return;
	}

	const auto& types = cursorJson["Types"];
	if (types.is_object()) {
		for (const auto& [key, type] : kCursorTypeKeys) {
			if (types.contains(key) && types[key].is_object()) {
				types[key].get_to(cursor.Types[static_cast<size_t>(type)]);
			}
		}
		return;
	}

	// Legacy: sparse array indexed by ImGuiMouseCursor_*
	if (types.is_array()) {
		for (size_t i = 0; i < ImGuiMouseCursor_COUNT && i < types.size(); ++i) {
			if (types[i].is_object()) {
				types[i].get_to(cursor.Types[i]);
			}
		}
	}
}

void Menu::CursorToJson(json& cursorJson, const ThemeSettings::CursorSettings& cursor)
{
	json types = json::object();
	for (const auto& [key, type] : kCursorTypeKeys) {
		const auto& settings = cursor.Types[static_cast<size_t>(type)];
		if (!settings.File.empty() || settings.HotspotX != 0.0f || settings.HotspotY != 0.0f) {
			types[key] = settings;
		}
	}
	if (!types.empty()) {
		cursorJson["Types"] = types;
	}
}

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
{
	Util::IconLoader::GetIcons().ReleaseAll();

	Util::CursorLoader::Shutdown();

	// Clean up blur resources
	BackgroundBlur::Cleanup();

	if (!initialized)
		return;

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
	migrateKey(o_json, "CSEditorToggleKey", settings.CSEditorToggleKey);
	migrateKey(o_json, "ScreenshotKey", settings.ScreenshotKey);
	migrateKey(o_json, "Effects11ToggleKey", settings.Effects11ToggleKey);
	migrateKey(o_json, "Effects11EditorKey", settings.Effects11EditorKey);

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
	loadComboList(o_json, "CSEditorToggleKey", settings.CSEditorToggleKey);
	loadComboList(o_json, "ScreenshotKey", settings.ScreenshotKey);
	loadComboList(o_json, "Effects11ToggleKey", settings.Effects11ToggleKey);
	loadComboList(o_json, "Effects11EditorKey", settings.Effects11EditorKey);

	// Unknown or malformed values (hand-edited config, a section that no longer exists) open the first section
	settings.LastSettingsSection = std::string(SettingsPageRenderer::GetFirstSection());
	if (const auto it = o_json.find("LastSettingsSection"); it != o_json.end() && it->is_string()) {
		auto section = it->get<std::string>();
		if (SettingsPageRenderer::IsKnownSection(section))
			settings.LastSettingsSection = std::move(section);
	}

	// Feature list filters; bits from a newer or hand-edited config that no chip uses are dropped
	settings.FeatureListFilter = 0;
	if (const auto it = o_json.find("FeatureListFilter"); it != o_json.end() && it->is_number_integer() && it->get<std::int64_t>() >= 0)
		settings.FeatureListFilter = static_cast<std::uint32_t>(it->get<std::int64_t>() & FeatureListFilter::kKnown);

	// Recently opened features: known short names only, without repeats, capped; malformed entries are dropped
	settings.RecentFeatures.clear();
	if (const auto it = o_json.find("RecentFeatures"); it != o_json.end() && it->is_array()) {
		for (const auto& entry : *it) {
			if (settings.RecentFeatures.size() >= MAX_RECENT_FEATURES)
				break;
			if (!entry.is_string())
				continue;
			auto name = entry.get<std::string>();
			if (Feature::FindFeatureByShortName(name) && std::ranges::find(settings.RecentFeatures, name) == settings.RecentFeatures.end())
				settings.RecentFeatures.push_back(std::move(name));
		}
	}

	// Legacy support: If old config has Theme data and no SelectedThemePreset, load it
	if (o_json.contains("Theme") && o_json["Theme"].is_object() && settings.SelectedThemePreset.empty()) {
		bool hasFontRoles = o_json["Theme"].contains("FontRoles");
		SanitizeFontRolesJson(o_json["Theme"]);
		settings.Theme = o_json["Theme"];
		PaletteFromJson(o_json["Theme"], settings.Theme.FullPalette);
		if (o_json["Theme"].contains("Cursor") && o_json["Theme"]["Cursor"].is_object()) {
			CursorFromJson(o_json["Theme"]["Cursor"], settings.Theme.Cursor);
		}
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
	InputCombo::ComboList::to_json(o_json["CSEditorToggleKey"], settings.CSEditorToggleKey);
	InputCombo::ComboList::to_json(o_json["ScreenshotKey"], settings.ScreenshotKey);
	InputCombo::ComboList::to_json(o_json["Effects11ToggleKey"], settings.Effects11ToggleKey);
	InputCombo::ComboList::to_json(o_json["Effects11EditorKey"], settings.Effects11EditorKey);

	o_json["LastSettingsSection"] = settings.LastSettingsSection;
	o_json["FeatureListFilter"] = settings.FeatureListFilter;
	o_json["RecentFeatures"] = settings.RecentFeatures;
}

void Menu::LoadTheme(json& o_json)
{
	if (!o_json["Theme"].is_object())
		return;
	const ThemeSettings backupTheme = settings.Theme;
	try {
		bool hasFontRoles = o_json["Theme"].contains("FontRoles");
		SanitizeFontRolesJson(o_json["Theme"]);
		settings.Theme = o_json["Theme"];
		PaletteFromJson(o_json["Theme"], settings.Theme.FullPalette);
		if (o_json["Theme"].contains("Cursor") && o_json["Theme"]["Cursor"].is_object()) {
			CursorFromJson(o_json["Theme"]["Cursor"], settings.Theme.Cursor);
		}
		MenuFonts::NormalizeFontRoles(settings.Theme, hasFontRoles);
		Util::CursorLoader::MigrateLegacyCursorSettings(settings.Theme);

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
	} catch (const std::exception& e) {
		logger::warn("Error loading theme: {}", e.what());
		settings.Theme = backupTheme;
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
	CursorToJson(o_json["Theme"]["Cursor"], settings.Theme.Cursor);
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
			if (themeSettings.contains("Cursor") && themeSettings["Cursor"].is_object()) {
				CursorFromJson(themeSettings["Cursor"], settings.Theme.Cursor);
			}

			MenuFonts::NormalizeFontRoles(settings.Theme, hasFontRoles);
			Util::CursorLoader::MigrateLegacyCursorSettings(settings.Theme);
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
			if (settings.Theme.FontName != MenuFonts::GetAtlasState().cachedFontName) {
				MenuFonts::GetAtlasState().pendingFontReload = true;
			}

			// Schedule deferred icon reload to apply theme-specific icon overrides
			Util::IconLoader::PendingReload() = true;
			pendingCursorReload = true;

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
	// Skyrim delivers input as a frame-sized batch. ImGui's default trickle mode may consume only
	// part of such a batch to spread rapid transitions over subsequent frames. High-rate wheel input
	// can therefore grow InputEventsQueue without bound and replay stale cursor/input state seconds
	// later. Consume the complete batch in the next NewFrame instead.
	imgui_io.ConfigInputTrickleEventQueue = false;
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

	ThemeManager::ReloadFont(*this, MenuFonts::GetAtlasState().cachedFontSize);

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

	Util::CursorLoader::Reload(this);

	// Initialize background blur system
	if (!BackgroundBlur::Initialize()) {
		logger::warn("Menu::Init() - Failed to initialize background blur system");
	}

	// Detours must land before any feature draws, and this is the first guaranteed render-thread
	// point; Install logs its own failure and leaves the interceptor inert.
	SceneWidgetInterceptor::Install();
	Util::UIHooks::Install();

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

	// Outside the feature body and its baseline interceptor scope, before anything reads the settings this frame
	ApplyPendingUndo();

	// Apply theme styling with universal contrast enhancement
	ThemeManager::SetupImGuiStyle(*this);

	// After the style setup, which overwrites the whole style including Alpha.
	const Util::FadeScope fadeScope(openFade);

	ImGui::DockSpaceOverViewport(0, NULL, ImGuiDockNodeFlags_PassthruCentralNode);

	const auto layoutCond = resetLayout ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
	ImGui::SetNextWindowPos(Util::GetNativeViewportSizeScaled(0.5f), layoutCond, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(Util::GetNativeViewportSizeScaled(0.8f), layoutCond);
	resetLayout = false;
	const auto versionStr = Util::GetFormattedVersion(Plugin::VERSION);
	auto* i18n = I18n::GetSingleton();
	const auto displayTitle = Plugin::BUILD_DESCRIBE == std::format("v{}", versionStr) ?
	                              i18n->Format("menu.window_title", { { "version", versionStr } }, "Community Shaders {version}") :
	                              i18n->Format("menu.window_title_dev", { { "version", versionStr }, { "build", Util::GetBuildLabel() } }, "Community Shaders {version} {build}");
	// ### keeps the window ID stable across build suffixes, preserving docking state
	const auto title = std::format("{}###CommunityShaders", displayTitle);

	// The title bar only shows while docked; the window cannot be asked before it begins, so last frame's state stands in
	static bool wasDocked = false;
	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar;
	if (!wasDocked)
		windowFlags |= ImGuiWindowFlags_NoTitleBar;

	Util::BeginWithRoundedClose(title.c_str(), &IsEnabled, windowFlags);
	{
		const bool isDocked = ImGui::IsWindowDocked();
		wasDocked = isDocked;

		// Before anything draws, so a shortcut's request reaches the header and feature list this frame.
		DispatchShortcuts();

		// A scale within rounding of the default snaps to it exactly, so built-in themes stay at the default
		float globalScale = settings.Theme.GlobalScale;
		if (std::abs(globalScale - ThemeManager::Constants::DEFAULT_GLOBAL_SCALE) < 0.001f)
			globalScale = ThemeManager::Constants::DEFAULT_GLOBAL_SCALE;

		const float uiScale = exp2(globalScale);  // The user's manual GlobalScale, for header icons
		// Action icons always show; one whose texture is missing falls back to its glyph
		auto& uiIcons = Util::IconLoader::GetIcons();
		const bool showLogo = uiIcons.logo.texture != nullptr;

		MenuHeaderRenderer::RenderHeader(isDocked, showLogo, uiScale, uiIcons, shortcuts);
		Util::DrawRecacheBanner();

		// A modal, so it is drawn at window level next to the header's popovers
		GoToPalette::Draw(shortcuts);

		// With the status bar off there is no footer; the left pane keeps the Settings and Profiling buttons instead
		const float footerHeight = settings.Theme.ShowFooter ? StatusBarRenderer::GetHeight() : 0.0f;

		static size_t selectedMenu = 0;
		static std::map<std::string, bool> categoryExpansionStates;

		shownFeature = nullptr;
		FeatureListRenderer::Hooks listHooks{
			.featureStep = std::exchange(pendingFeatureStep, 0),
			.onFeatureShown = [this](Feature* feat) {
				shownFeature = feat;
				if (feat->IsInMenu())
					RecordRecentFeature(feat->GetShortName());
				// Watched before the header, so its Restore / Use Baselines clicks and the body's edits are both seen
				if (feat->loaded && feat->UsesMainSettings() && !globals::state->IsFeatureDisabled(feat->GetShortName()))
					undoHistory.Watch(MakeMenuFeatureUndoTarget(feat)); },
			.runFeatureCommand = [this](Feature* feat, const std::function<void()>& command) { undoHistory.Change(MakeMenuFeatureUndoTarget(feat), command); },
			.drawSettingsNav = [this] { DrawSettingsPage(true); },
		};
		FeatureListRenderer::RenderFeatureList(
			footerHeight,
			selectedMenu,
			pendingFeatureSelection,
			categoryExpansionStates,
			[&]() { DrawSettingsPage(false); },
			listHooks);

		if (settings.Theme.ShowFooter)
			StatusBarRenderer::Render(shortcuts);

		// Global popups, once per frame
		Util::DrawClearShaderCacheConfirmation();
		Util::DrawRestoreSavedSettingsConfirmation();
	}
	ImGui::End();

	// After every watched feature has drawn; records the change an interaction that ended this frame made
	undoHistory.EndFrame();
	// The overlay also hosts the toasts; Draw() only draws once per frame
	Util::Notifications::Draw();
}

void Menu::ApplyPendingUndo()
{
	const auto request = std::exchange(pendingUndo, UndoRequest::None);
	if (request == UndoRequest::None)
		return;

	const bool undo = request == UndoRequest::Undo;
	auto* i18n = I18n::GetSingleton();
	if (const auto label = undo ? undoHistory.Undo() : undoHistory.Redo()) {
		const auto message = undo ? i18n->Format("menu.shortcuts.undid", { { "name", *label } }, "Undid: {name}") :
		                            i18n->Format("menu.shortcuts.redid", { { "name", *label } }, "Redid: {name}");
		Util::Notifications::Show(message, Util::Colors::GetInfo(), ThemeManager::Constants::UNDO_TOAST_SECONDS);
	} else {
		Util::Notifications::ShowOnce(undo ? T("menu.shortcuts.nothing_to_undo", "Nothing to undo") : T("menu.shortcuts.nothing_to_redo", "Nothing to redo"),
			settings.Theme.StatusPalette.Disable, ThemeManager::Constants::UNDO_TOAST_SECONDS);
	}
}

MenuShortcuts::Actions Menu::MakeShortcutActions()
{
	return {
		.save = [] { SaveManager::SaveAll(); },
		.undo = [this] { pendingUndo = UndoRequest::Undo; },
		.redo = [this] { pendingUndo = UndoRequest::Redo; },
		.openGoTo = [] { GoToPalette::Open(); },
		.focusSearch = [] { GoToPalette::Open(GoToPalette::Scope::Features); },
		.stepFeature = [this](int step) { pendingFeatureStep = step; },
		.canTogglePin = [this] { return shownFeature && shownFeature->loaded; },
		.togglePin = [this] { FeatureListRenderer::ToggleFavorite(shownFeature); },
		.openPage = [this](std::string_view pageId) { SelectFeatureMenu(std::string(pageId)); },
		.canOpenEditor = [] { return EditorWindow::CanBeOpen(); },
		.openEditor = [] { CSEditor::OpenEditorWindow(); },
		.openHelp = [this] {
			SelectFeatureMenu(std::string(MenuPageIds::kHome));
			HomePageRenderer::RequestHelpPopover(); },
	};
}

void Menu::DispatchShortcuts()
{
	if (shortcuts.empty())
		shortcuts = MenuShortcuts::BuildTable(MakeShortcutActions());

	// The CS Editor has its own Ctrl+Z / Ctrl+S, a focused text field owns its keys, a hotkey capture
	// records the next chord as a binding, and a modal (confirmation) must be answered first.
	const auto* editorWindow = EditorWindow::GetSingleton();
	const bool allowed = IsEnabled && !ImGui::GetIO().WantTextInput && !IsCapturingHotkeyInput() &&
	                     !(editorWindow && editorWindow->open) && !ImGui::GetTopMostPopupModal() &&
	                     !HomePageRenderer::ShouldShowFirstTimeSetup();
	if (allowed)
		MenuShortcuts::Dispatch(shortcuts);
}

/**
 * @brief Renders the Settings page content
 *
 * Delegates rendering to SettingsPageRenderer, passing the key-capture flags and
 * selected section it updates, and a callback for the private disable-at-boot list.
 */
void Menu::DrawSettingsPage(bool navOnly)
{
	SettingsPageRenderer::PageState state{
		.section = settings.LastSettingsSection,
		.keys = {
			.settingToggleKey = settingToggleKey,
			.settingsEffectsToggle = settingsEffectsToggle,
			.settingSkipCompilationKey = settingSkipCompilationKey,
			.settingOverlayToggleKey = settingOverlayToggleKey,
			.settingShaderBlockPrevKey = settingShaderBlockPrevKey,
			.settingShaderBlockNextKey = settingShaderBlockNextKey,
			.settingCSEditorToggleKey = settingCSEditorToggleKey,
			.settingScreenshotKey = settingScreenshotKey,
			.settingEffects11ToggleKey = settingEffects11ToggleKey,
			.settingEffects11EditorKey = settingEffects11EditorKey },
		.drawDisableAtBoot = [this]() { DrawDisableAtBootSettings(); }
	};

	if (navOnly)
		SettingsPageRenderer::RenderSectionList(state);
	else
		SettingsPageRenderer::Render(state);
}

void Menu::DrawDisableAtBootSettings()
{
	auto& disabledFeatures = globals::state->GetDisabledFeatures();

	ImGui::TextWrapped("%s",
		T("menu.disable_at_boot_desc",
			"Select features to disable at boot. "
			"This is the same as deleting a feature.ini file. "
			"Restart will be required to reenable."));
	ImGui::PushStyleColor(ImGuiCol_Text, settings.Theme.StatusPalette.RestartNeeded);
	ImGui::TextWrapped("%s", T("menu.settings.disable_at_boot_restart", "Changes here take effect the next time the game starts."));
	ImGui::PopStyleColor();

	ImGui::Spacing();

	const char* disabledOnlyLabel = T("menu.settings.disabled_only", "Disabled only");
	const float chipWidth = BrowserUI::MeasureChip(disabledOnlyLabel, false);
	const float searchWidth = std::max(ImGui::GetFrameHeight() * ThemeManager::Constants::DISABLE_AT_BOOT_MIN_SEARCH_FRAMES, ImGui::GetContentRegionAvail().x - chipWidth - ImGui::GetStyle().ItemSpacing.x);
	BrowserUI::SearchField("##DisableAtBootSearch", disableAtBootSearch.data(), disableAtBootSearch.size(), T("ui.search", "Search..."), searchWidth, false);
	BrowserUI::SameLineIfFits(chipWidth);
	if (BrowserUI::FilterChip("##DisabledOnly", { disabledOnlyLabel }, disableAtBootDisabledOnly))
		disableAtBootDisabledOnly = !disableAtBootDisabledOnly;

	ImGui::Spacing();

	// Label by the (translated) display name the feature list shows; the short name keys the setting and the widget ID
	const std::string query(disableAtBootSearch.data());
	std::vector<std::pair<std::string, std::string>> entries;  // display name, short name
	for (auto* feature : Feature::GetFeatureList()) {
		if (feature->IsHiddenUnreleased() || !Util::FeatureMatchesSearch(feature, query))
			continue;
		const std::string shortName(feature->GetShortName());
		if (disableAtBootDisabledOnly && !(disabledFeatures.contains(shortName) && disabledFeatures[shortName]))
			continue;
		entries.emplace_back(feature->GetDisplayName(), shortName);
	}
	std::ranges::sort(entries);

	if (entries.empty())
		ImGui::TextDisabled("%s", T("menu.settings.disable_at_boot_no_match", "No features match."));

	// As many columns as the widest name allows, filled row by row
	const ImGuiStyle& style = ImGui::GetStyle();
	float widestName = 0.0f;
	for (const auto& entry : entries)
		widestName = std::max(widestName, ImGui::CalcTextSize(entry.first.c_str()).x);
	const float columnWidth = ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + widestName + style.ItemSpacing.x;
	const int columns = std::clamp(static_cast<int>(ImGui::GetContentRegionAvail().x / columnWidth), 1, std::max(1, static_cast<int>(entries.size())));
	if (entries.empty() || !ImGui::BeginTable("##DisableAtBoot", columns, ImGuiTableFlags_SizingStretchSame))
		return;
	for (const auto& [displayName, shortName] : entries) {
		ImGui::TableNextColumn();
		bool isDisabled = disabledFeatures.contains(shortName) && disabledFeatures[shortName];
		if (ImGui::Checkbox(std::format("{}##{}", displayName, shortName).c_str(), &isDisabled))
			disabledFeatures[shortName] = isDisabled;
	}
	ImGui::EndTable();
}

/**
 * @brief Main overlay rendering coordinator
 *
 * Delegates all overlay rendering to OverlayRenderer while providing necessary
 * callbacks for input processing, settings rendering, and key mapping. This method
 * serves as the bridge between Menu's state and the extracted overlay rendering logic.
 *
 * Handles input event processing, shader compilation status, feature overlays,
 * A/B testing, and ImGui frame management through the specialized renderer component.
 */
void Menu::DrawOverlay()
{
	// Only process reloads when ImGui is NOT in an active frame
	ImGuiContext* ctx = ImGui::GetCurrentContext();
	bool canReload = ctx && !ctx->WithinFrameScope && ctx->WithinEndChildID == 0;

	if (auto& atlas = MenuFonts::GetAtlasState(); !IsEnabled && atlas.wantsFontPreviewAtlas) {
		atlas.wantsFontPreviewAtlas = false;
		atlas.pendingFontReload = true;
	}

	// Deferred font reload, before any ImGui operation: the safest place to modify the font atlas.
	// The flag stays set on failure, so it retries next frame.
	if (MenuFonts::GetAtlasState().pendingFontReload && canReload) {
		if (ThemeManager::ReloadFont(*this, MenuFonts::GetAtlasState().cachedFontSize))
			MenuFonts::GetAtlasState().pendingFontReload = false;
		else
			logger::warn("Menu::DrawOverlay() - Font reload failed, will retry next frame");
	}

	// Deferred icon reload, before rendering
	if (Util::IconLoader::PendingReload() && canReload) {
		if (Util::IconLoader::InitializeMenuIcons(this))
			Util::IconLoader::PendingReload() = false;
		else
			logger::warn("Menu::DrawOverlay() - Icon reload failed, will retry next frame");
	}

	if (pendingCursorReload && canReload) {
		static bool loggedCursorReloadRetry = false;
		if (Util::CursorLoader::Reload(this)) {
			pendingCursorReload = false;
			loggedCursorReloadRetry = false;
		} else if (!loggedCursorReloadRetry) {
			logger::warn("Menu::DrawOverlay() - Cursor reload deferred (will retry when ready)");
			loggedCursorReloadRetry = true;
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
		MenuFonts::GetAtlasState().cachedFontSize,
		ThemeManager::ResolveFontSize(*this));
}

/** @brief The CS Editor key derived from the menu key: Shift plus its base key; empty when the menu key already uses Shift or has no base key. */
static std::vector<InputCombo> DeriveCSEditorKey(const std::vector<InputCombo>& menuKey)
{
	bool hasShift = false;
	uint32_t baseKey = 0;

	for (const auto& combo : menuKey) {
		uint32_t vk = combo.GetKey();
		if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT) {
			hasShift = true;
		} else if (vk != VK_CONTROL && vk != VK_LCONTROL && vk != VK_RCONTROL &&
				   vk != VK_MENU && vk != VK_LMENU && vk != VK_RMENU) {
			baseKey = vk;
		}
	}

	if (hasShift || baseKey == 0)
		return {};

	return { InputCombo::Keyboard(VK_SHIFT), InputCombo::Keyboard(baseKey) };
}

/**
 * @brief Processes queued input events
 *
 * This method handles the logic of routing input events to appropriate handlers:
 * - Keyboard and mouse events are processed directly for ImGui integration
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
	for (auto& event : _keyEventQueue) {
		if (event.eventType == RE::INPUT_EVENT_TYPE::kChar) {
			if (!Util::Input::IsRejectedByNumericTextEntry(event.keyCode))
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
				}
			} else if (flying) {
				// Right-click exits free-cam / play mode (alongside the CSEditorToggleKey combo).
				if (event.keyCode == ImGuiMouseButton_Right && event.IsPressed())
					ew->ExitPreviewMode();
			} else if (static_cast<int>(event.keyCode) < ImGuiMouseButton_COUNT) {
				io.AddMouseButtonEvent(event.keyCode, event.IsPressed());
			}
		}

		if (event.device == RE::INPUT_DEVICE::kKeyboard) {
			uint32_t key = Util::Input::DIKToVK(event.keyCode);
			logger::trace("Detected key code {} ({})", event.keyCode, key);
			if (key == event.keyCode)
				key = MapVirtualKeyEx(event.keyCode, MAPVK_VSC_TO_VK_EX, GetKeyboardLayout(0));

			const bool wasCapturingHotkey = IsCapturingHotkeyInput();

			// Dispatch bound hotkey actions for `key`. Combo bindings (modifier + key)
			// fire on key-down for responsiveness; single-key bindings fire on key-up.
			bool effects11EditorToggled = false;
			auto dispatchHotkeyActions = [this, key, &effects11EditorToggled](bool combosOnly) {
				struct KeyAction
				{
					std::vector<InputCombo>& settingKey;
					std::function<void()> action;
				};
				auto shaderCache = globals::shaderCache;
				KeyAction keyActions[] = {
					{ settings.ToggleKey, [this]() {
						 if (!HomePageRenderer::ShouldShowFirstTimeSetup() && !EditorWindow::GetSingleton()->open) {
							 IsEnabled = !IsEnabled;
							 if (IsEnabled)
								 ImGui::GetIO().ClearInputKeys();  // Prevent toggle key from remaining "held" in ImGui after open.
						 } else if (HomePageRenderer::ShouldShowFirstTimeSetup()) {
							 HomePageRenderer::MarkFirstTimeSetupComplete();
						 }
					 } },
					{ settings.SkipCompilationKey, [this, shaderCache]() { if (!ShouldSwallowInput() && shaderCache->IsCompiling()) shaderCache->SetBackgroundCompilation(true); } },
					{ settings.EffectToggleKey, [shaderCache]() { shaderCache->SetEnabled(!shaderCache->IsEnabled()); } },
					{ settings.ShaderBlockPrevKey, [this, shaderCache]() { if (settings.EnableShaderBlocking) shaderCache->IterateShaderBlock(); } },
					{ settings.ShaderBlockNextKey, [this, shaderCache]() { if (settings.EnableShaderBlocking) shaderCache->IterateShaderBlock(false); } },
					{ settings.OverlayToggleKey, []() { Menu::GetSingleton()->overlayVisible = !Menu::GetSingleton()->overlayVisible; } },
					{ settings.CSEditorToggleKey, []() {
						 auto* ew = EditorWindow::GetSingleton();
						 if (!ew)
							 return;
						 if (ew->GetPreviewMode() == EditorWindow::PreviewMode::FreeCamera) {
							 // Flying → lock camera position for editing
							 ew->ToggleFreeCameraLock();
						 } else if (ew->IsInPreviewMode()) {
							 // Locked or PlayMode → fully exit preview
							 ew->ExitPreviewMode();
						 } else {
							 CSEditor::ToggleEditorWindow();
						 }
					 } },
					{ settings.ScreenshotKey, []() {
						 if (globals::features::screenshotFeature.loaded)
							 globals::features::screenshotFeature.captureRequested = true;
					 } },
					{ settings.Effects11ToggleKey, []() {
						 if (globals::features::effects11.loaded)
							 globals::features::effects11.ToggleEnabled();
					 } },
					{ settings.Effects11EditorKey, [&effects11EditorToggled]() {
						 if (!HomePageRenderer::ShouldShowFirstTimeSetup()) {
							 Effects11Editor::GetSingleton().Toggle();
							 effects11EditorToggled = true;
						 }
					 } },
				};
				// RenderDoc's capture key is a single, unmodified key; only consider it on key-up.
				if (!combosOnly && globals::features::renderDoc.HandleCaptureHotkey(key))
					return true;
				for (const auto& ka : keyActions) {
					const bool isCombo = ka.settingKey.size() > 1;
					if (isCombo == combosOnly && InputCombo::MatchesKeyboardCombo(ka.settingKey, key)) {
						ka.action();
						return true;
					}
				}
				return false;
			};

			if (!event.IsPressed()) {
				struct HotkeyAction
				{
					std::vector<InputCombo>* settingKey;
					bool* settingFlag;
					std::function<void(std::vector<InputCombo>)> action;
				};
				HotkeyAction hotkeyActions[] = {
					{ &settings.ToggleKey, &settingToggleKey, [this](std::vector<InputCombo> keys) {
						 settings.ToggleKey = keys;
						 settingToggleKey = false;
						 if (!settings.FirstTimeSetupCompleted)
							 settings.CSEditorToggleKey = DeriveCSEditorKey(keys);
					 } },
					{ &settings.SkipCompilationKey, &settingSkipCompilationKey, [this](std::vector<InputCombo> keys) { settings.SkipCompilationKey = keys; settingSkipCompilationKey = false; } },
					{ &settings.EffectToggleKey, &settingsEffectsToggle, [this](std::vector<InputCombo> keys) { settings.EffectToggleKey = keys; settingsEffectsToggle = false; } },
					{ &settings.OverlayToggleKey, &settingOverlayToggleKey, [this](std::vector<InputCombo> keys) { settings.OverlayToggleKey = keys; settingOverlayToggleKey = false; } },
					{ &settings.ShaderBlockPrevKey, &settingShaderBlockPrevKey, [this](std::vector<InputCombo> keys) { settings.ShaderBlockPrevKey = keys; settingShaderBlockPrevKey = false; } },
					{ &settings.ShaderBlockNextKey, &settingShaderBlockNextKey, [this](std::vector<InputCombo> keys) { settings.ShaderBlockNextKey = keys; settingShaderBlockNextKey = false; } },
					{ &settings.CSEditorToggleKey, &settingCSEditorToggleKey, [this](std::vector<InputCombo> keys) { settings.CSEditorToggleKey = keys; settingCSEditorToggleKey = false; } },
					{ &settings.ScreenshotKey, &settingScreenshotKey, [this](std::vector<InputCombo> keys) { settings.ScreenshotKey = keys; settingScreenshotKey = false; } },
					{ &settings.Effects11ToggleKey, &settingEffects11ToggleKey, [this](std::vector<InputCombo> keys) { settings.Effects11ToggleKey = keys; settingEffects11ToggleKey = false; } },
					{ &settings.Effects11EditorKey, &settingEffects11EditorKey, [this](std::vector<InputCombo> keys) { settings.Effects11EditorKey = keys; settingEffects11EditorKey = false; } },
				};
				bool handled = false;
				for (auto& h : hotkeyActions) {
					if (*(h.settingFlag)) {
						// During first-time setup, don't capture Enter or Escape as hotkeys;
						// they cancel capture instead
						if (HomePageRenderer::ShouldShowFirstTimeSetup() && (key == VK_RETURN || key == VK_ESCAPE)) {
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
					// Single-key hotkeys fire on key-up; combos already fired on key-down.
					// If this key's key-down already fired a combo, suppress the single-key
					// binding so releasing the modifier first doesn't trigger it as well.
					if (_comboFiredKeys.erase(key) == 0)
						dispatchHotkeyActions(false);
				}

				// Handle ESC key for menu and editor window
				auto* editorWindow = EditorWindow::GetSingleton();
				// An Escape-bound Effects11 editor hotkey already toggled the editor this release
				if (key == VK_ESCAPE && !effects11EditorToggled) {
					if (PresetsPageRenderer::CloseLightboxIfOpen()) {
						// Screenshot lightbox takes priority over closing the menu.
					} else if (editorWindow && editorWindow->IsInPreviewMode()) {
						editorWindow->ExitPreviewMode();
					} else if (editorWindow && editorWindow->open) {
						// Both guards consume one-shot flags, so evaluate each unconditionally.
						auto& hostedEffects11Editor = Effects11Editor::GetSingleton();
						const bool editorHandlesEscape = editorWindow->ShouldHandleEscapeKey();
						const bool effects11Allows = !hostedEffects11Editor.IsOpen() || hostedEffects11Editor.ShouldHandleEscapeKey();
						if (editorHandlesEscape && effects11Allows)
							editorWindow->open = false;
					} else if (auto& effects11Editor = Effects11Editor::GetSingleton(); effects11Editor.IsOpen()) {
						if (effects11Editor.ShouldHandleEscapeKey())
							effects11Editor.Close();
					} else if (IsEnabled && (!editorWindow || !editorWindow->open)) {
						if (!TakeConsumedEscape())
							IsEnabled = false;
					}
				}
			} else if (event.IsDown() && !wasCapturingHotkey) {
				// Fire combo hotkeys on the key-down transition so they respond on
				// press rather than release. IsDown() (not IsPressed()) ensures we
				// trigger only once instead of every frame the key is held.
				if (dispatchHotkeyActions(true))
					_comboFiredKeys.insert(key);
			}

			// Don't forward hotkey events to ImGui when input is captured (prevents e.g. End key scrolling the feature list)
			// SkipCompilationKey (ESC) is excluded — ESC must reach ImGui for menu/dialog close.
			const std::vector<InputCombo>* hotkeys[] = {
				&settings.ToggleKey, &settings.EffectToggleKey,
				&settings.OverlayToggleKey, &settings.ShaderBlockPrevKey, &settings.ShaderBlockNextKey,
				&settings.CSEditorToggleKey,
				&settings.ScreenshotKey,
				&settings.Effects11ToggleKey,
				&settings.Effects11EditorKey
			};
			bool isHotkey = ShouldSwallowInput() && std::any_of(std::begin(hotkeys), std::end(hotkeys),
														[key](const auto* combo) { return InputCombo::MatchesKeyboardCombo(*combo, key); });

			// Always forward key-up events. Suppress key-down during active hotkeys and hotkey capture.
			const bool isKeyDown = event.IsPressed();
			const bool suppressForwarding = isKeyDown && (isHotkey || wasCapturingHotkey);
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

	const auto directInputWheelRaw = _directInputWheelDelta.exchange(0, std::memory_order_relaxed);
	const float wheelY = static_cast<float>(directInputWheelRaw) / static_cast<float>(WHEEL_DELTA);
	if (wheelY != 0.0f)
		io.AddMouseWheelEvent(0.0f, wheelY);

	_keyEventQueue.clear();
}

void Menu::RecordDirectInputWheelDelta(std::int32_t delta)
{
	if (delta != 0)
		_directInputWheelDelta.fetch_add(delta, std::memory_order_relaxed);
}

bool Menu::IsCapturingHotkeyInput() const
{
	return settingToggleKey || settingSkipCompilationKey || settingsEffectsToggle ||
	       settingOverlayToggleKey || settingShaderBlockPrevKey || settingShaderBlockNextKey || settingCSEditorToggleKey || settingScreenshotKey || settingEffects11ToggleKey ||
	       settingEffects11EditorKey;
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
		if (const auto& device = inputMgr->GetKeyboard())
			device->ClearInputState();
	}
	// Allows tab to work again after alt+tabbing back in.
	ImGui::GetIO().ClearInputKeys();
}

void Menu::ProcessInputEvents(RE::InputEvent* const* a_events)
{
	// Only button, char and thumbstick events matter
	for (auto it = *a_events; it; it = it->next) {
		switch (it->GetEventType()) {
		case RE::INPUT_EVENT_TYPE::kButton:
			addToEventQueue(KeyEvent(static_cast<RE::ButtonEvent*>(it)));
			break;
		case RE::INPUT_EVENT_TYPE::kChar:
			addToEventQueue(KeyEvent(static_cast<CharEvent*>(it)));
			break;
		case RE::INPUT_EVENT_TYPE::kThumbstick:
			addToEventQueue(KeyEvent(static_cast<RE::ThumbstickEvent*>(it)));
			break;
		default:
			break;
		}
	}
}

void Menu::UpdateOpenFade(float deltaTime)
{
	if (settings.Theme.MenuFadeEnabled)
		Util::StepFade(openFade, IsEnabled, deltaTime);
	else
		openFade = IsEnabled ? 1.0f : 0.0f;
}

bool Menu::ShouldSwallowInput()
{
	auto editorWindow = EditorWindow::GetSingleton();
	return IsEnabled || HomePageRenderer::ShouldShowFirstTimeSetup() || (editorWindow && editorWindow->open) ||
	       Effects11Editor::GetSingleton().IsOpen();
}

void Menu::SyncVanityCamera()
{
	const bool wantDisabled = ShouldSwallowInput();
	auto setting = RE::GetINISetting("fAutoVanityModeDelay:Camera");
	if (!setting)
		return;

	if (wantDisabled && !vanityCameraDisabled) {
		savedVanityCameraDelay = setting->GetFloat();
		setting->data.f = 10000.0f;
		vanityCameraDisabled = true;
		logger::info("Vanity camera disabled while CS UI is open (saved delay: {})", savedVanityCameraDelay);
	} else if (!wantDisabled && vanityCameraDisabled) {
		setting->data.f = savedVanityCameraDelay;
		vanityCameraDisabled = false;
		logger::info("Vanity camera restored (delay: {})", savedVanityCameraDelay);
	}
}

bool Menu::IsPreviewFlying()
{
	auto editorWindow = EditorWindow::GetSingleton();
	return editorWindow && editorWindow->IsPreviewFlying();
}

void Menu::RecordRecentFeature(const std::string& shortName)
{
	auto& recent = settings.RecentFeatures;
	if (!recent.empty() && recent.front() == shortName)
		return;  // the common case: the same page drawn again
	std::erase(recent, shortName);
	recent.insert(recent.begin(), shortName);
	if (recent.size() > MAX_RECENT_FEATURES)
		recent.resize(MAX_RECENT_FEATURES);
}

void Menu::SelectFeatureMenu(const std::string& featureName)
{
	// Issues live in Home's Needs Attention card
	if (featureName == MenuPageIds::kIssues) {
		HomePageRenderer::FocusNeedsAttention();
		return;
	}
	pendingFeatureSelection = featureName;
	logger::info("Queued navigation to {} feature menu", featureName);
}

/**
 * @brief Renders the standalone weather details window when enabled
 *
 * Delegates to the CSEditor feature for rendering the weather details window
 * that can remain open even when the main menu is closed. This provides a simple
 * coordination layer between the Menu system and the CSEditor feature.
 */
void Menu::DrawWeatherDetailsWindow()
{
	auto& csEditor = globals::features::csEditor;
	if (!csEditor.WeatherDetailsWindow.Enabled || !csEditor.loaded)
		return;
	csEditor.RenderWeatherDetailsWindow(&csEditor.WeatherDetailsWindow.Enabled, !globals::features::performanceOverlay.settings.ShowWeatherPicker);
}
