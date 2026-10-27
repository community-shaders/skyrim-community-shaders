#include "SettingsTabRenderer.h"

#include <format>
#include <set>
#include <string>
#include <windows.h>

#include "BackgroundBlur.h"
#include "CSEditor/Browser/BrowserWidgets.h"
#include "CursorLoader.h"
#include "Features/ScreenshotFeature.h"
#include "FontAtlasState.h"
#include "FontSelector.h"
#include "Fonts.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "IconLoader.h"
#include "IconsTabler.h"
#include "Menu.h"
#include "MenuIcons.h"
#include "ShaderCache.h"
#include "ShaderCacheStatusWidget.h"
#include "State.h"
#include "ThemeManager.h"
#include "Utils/UI.h"

using json = nlohmann::json;

namespace
{
	using FontRoleGuard = MenuFonts::FontRoleGuard;  // Convenience alias

	namespace Ids = SettingsSectionIds;

	// Setting labels the Settings page search matches, per section; keep in step with the widgets below
	constexpr SettingsSearchEntry kSearchIndex[] = {
		// Shaders & Cache
		{ Ids::kShadersCache, "menu.settings.use_custom_shaders", "Use Custom Shaders" },
		{ Ids::kShadersCache, "menu.settings.enable_disk_cache", "Enable Disk Cache" },
		{ Ids::kShadersCache, "menu.settings.skip_unchanged_shaders", "Skip Unchanged Shaders" },
		{ Ids::kShadersCache, "menu.settings.enable_async", "Enable Async" },
		{ Ids::kShadersCache, "menu.settings.skip_clear_cache_dialogue", "Skip Clear Cache Dialogue" },
		// Keybindings
		{ Ids::kKeybindings, "menu.settings.toggle_key", "Toggle Key:" },
		{ Ids::kKeybindings, "menu.settings.cs_editor_toggle_key", "CS Editor Toggle Key:" },
		{ Ids::kKeybindings, "menu.settings.effects11_editor_key", "Effects 11 Editor Key:" },
		{ Ids::kKeybindings, "menu.settings.overlay_toggle_key", "Overlay Toggle Key:" },
		{ Ids::kKeybindings, "menu.settings.screenshot_key", "Screenshot Key:" },
		{ Ids::kKeybindings, "menu.settings.effect_toggle_key", "Effect Toggle Key:" },
		{ Ids::kKeybindings, "menu.settings.effects11_toggle_key", "Effects 11 Toggle Key:" },
		{ Ids::kKeybindings, "menu.settings.skip_compilation_key", "Skip Compilation Key:" },
		{ Ids::kKeybindings, "menu.settings.shader_block_prev_key", "Shader Block Previous Key:" },
		{ Ids::kKeybindings, "menu.settings.shader_block_next_key", "Shader Block Next Key:" },
		// Language
		{ Ids::kLanguage, "menu.settings.language", "Language" },
		// Behavior
		{ Ids::kBehavior, "menu.settings.show_status_bar", "Show Status Bar" },
		{ Ids::kBehavior, "menu.settings.auto_hide_feature_list", "Auto-hide Feature List" },
		{ Ids::kBehavior, "menu.settings.ui_animations", "UI animations" },
		{ Ids::kBehavior, "menu.settings.animation_speed", "Animation speed" },
		{ Ids::kBehavior, "menu.settings.feature_list_order", "Feature List Order" },
		{ Ids::kBehavior, "menu.settings.require_shift_to_dock", "Require Shift to Dock" },
		{ Ids::kBehavior, "menu.settings.tooltip_hover_delay", "Tooltip Hover Delay" },
		{ Ids::kBehavior, "menu.settings.warn_forced_settings", "Warn When Settings Are Forced" },
		{ Ids::kBehavior, "menu.settings.reset_window_layout", "Reset Window Layout" },
		// Theme
		{ Ids::kTheme, "menu.settings.theme_preset", "Theme Preset" },
		{ Ids::kTheme, "menu.settings.open_themes_folder", "Open Themes Folder" },
		{ Ids::kTheme, "menu.settings.save_theme", "Save Theme" },
		{ Ids::kTheme, "menu.settings.save_as_new_theme", "Save As New Theme" },
		{ Ids::kTheme, "menu.settings.delete_theme", "Delete" },
		// Fonts
		{ Ids::kFonts, "menu.settings.use_resolution_based_font_size", "Use resolution-based font size" },
		{ Ids::kFonts, "menu.settings.base_font_size", "Base Font Size" },
		{ Ids::kFonts, "menu.settings.font_roles", "Font Roles" },
		{ Ids::kFonts, "menu.settings.refresh_font_families", "Refresh Font Families" },
		// Colors
		{ Ids::kColors, "menu.settings.color_background", "Background" },
		{ Ids::kColors, "menu.settings.color_text", "Text" },
		{ Ids::kColors, "menu.settings.borders_and_separators", "Borders & Separators" },
		{ Ids::kColors, "menu.settings.feature_headings", "Feature Headings" },
		{ Ids::kColors, "menu.settings.status", "Status" },
		{ Ids::kColors, "menu.settings.full_palette", "Full Palette" },
		// Styling
		{ Ids::kStyling, "menu.settings.global_scale", "Global Scale" },
		{ Ids::kStyling, "menu.settings.scrollbar_opacity", "Scrollbar Opacity" },
		{ Ids::kStyling, "menu.settings.window_padding", "Window Padding" },
		{ Ids::kStyling, "menu.settings.frame_padding", "Frame Padding" },
		{ Ids::kStyling, "menu.settings.item_spacing", "Item Spacing" },
		{ Ids::kStyling, "menu.settings.section_borders", "Borders" },
		{ Ids::kStyling, "menu.settings.section_rounding", "Rounding" },
		{ Ids::kStyling, "menu.settings.section_tables", "Tables" },
		{ Ids::kStyling, "menu.settings.section_docking", "Docking" },
		// Surfaces
		{ Ids::kStyling, "menu.settings.reset_surfaces", "Reset surfaces to theme defaults" },
		{ Ids::kStyling, "menu.settings.surface_home_card", "Home Cards" },
		{ Ids::kStyling, "menu.settings.surface_toast", "Notification Toasts" },
		{ Ids::kStyling, "menu.settings.surface_overlay_card", "Status Card" },
		{ Ids::kStyling, "menu.settings.surface_palette", "Go To Palette" },
		{ Ids::kStyling, "menu.settings.surface_nav_selected", "Selected Navigation Row" },
		{ Ids::kStyling, "menu.settings.surface_padding", "Inner Padding" },
		{ Ids::kStyling, "menu.settings.surface_fill_lift", "Fill Lift" },
		{ Ids::kStyling, "menu.settings.surface_border_enabled", "Border" },
		{ Ids::kStyling, "menu.settings.surface_shadow_enabled", "Shadow" },
		// Header & Effects
		{ Ids::kHeaderEffects, "menu.settings.use_monochrome_icons", "Use Monochrome Icons" },
		{ Ids::kHeaderEffects, "menu.settings.use_monochrome_cs_logo", "Use Monochrome CS Logo" },
		{ Ids::kHeaderEffects, "menu.settings.center_header_title", "Center Header Title" },
		{ Ids::kHeaderEffects, "menu.settings.background_blur", "Background Blur" },
		{ Ids::kHeaderEffects, "menu.settings.menu_fade", "Menu Fade" },
		{ Ids::kHeaderEffects, "menu.settings.use_custom_cursor", "Use Custom Theme Cursor" },
	};

	std::string GetLocaleDisplayLabel(std::string_view localeCode, std::string_view metadataName)
	{
		if (!metadataName.empty()) {
			return std::string(metadataName);
		}

		return std::string(localeCode);
	}

	// Convert ImGui internal color names to user-friendly display names
	const char* GetFriendlyColorName(int colorIndex)
	{
		switch (colorIndex) {
		case ImGuiCol_Text:
			return T("menu.settings.color_text", "Text");
		case ImGuiCol_TextDisabled:
			return T("menu.settings.color_text_disabled", "Text (Disabled)");
		case ImGuiCol_WindowBg:
			return T("menu.settings.color_window_bg", "Window Background");
		case ImGuiCol_ChildBg:
			return T("menu.settings.color_child_bg", "Child Window Background");
		case ImGuiCol_PopupBg:
			return T("menu.settings.color_popup_bg", "Popup Background");
		case ImGuiCol_Border:
			return T("menu.settings.color_border", "Border");
		case ImGuiCol_BorderShadow:
			return T("menu.settings.color_border_shadow", "Border Shadow");
		case ImGuiCol_FrameBg:
			return T("menu.settings.color_frame_bg", "Frame Background");
		case ImGuiCol_FrameBgHovered:
			return T("menu.settings.color_frame_bg_hovered", "Frame Background (Hovered)");
		case ImGuiCol_FrameBgActive:
			return T("menu.settings.color_frame_bg_active", "Frame Background (Active)");
		case ImGuiCol_TitleBg:
			return T("menu.settings.color_title_bg", "Title Bar Background");
		case ImGuiCol_TitleBgActive:
			return T("menu.settings.color_title_bg_active", "Title Bar Background (Active)");
		case ImGuiCol_TitleBgCollapsed:
			return T("menu.settings.color_title_bg_collapsed", "Title Bar Background (Collapsed)");
		case ImGuiCol_MenuBarBg:
			return T("menu.settings.color_menu_bar_bg", "Menu Bar Background");
		case ImGuiCol_ScrollbarBg:
			return T("menu.settings.color_scrollbar_bg", "Scrollbar Background");
		case ImGuiCol_ScrollbarGrab:
			return T("menu.settings.color_scrollbar_grab", "Scrollbar Grab");
		case ImGuiCol_ScrollbarGrabHovered:
			return T("menu.settings.color_scrollbar_grab_hovered", "Scrollbar Grab (Hovered)");
		case ImGuiCol_ScrollbarGrabActive:
			return T("menu.settings.color_scrollbar_grab_active", "Scrollbar Grab (Active)");
		case ImGuiCol_CheckMark:
			return T("menu.settings.color_check_mark", "Checkbox Checkmark");
		case ImGuiCol_SliderGrab:
			return T("menu.settings.color_slider_grab", "Slider Grab");
		case ImGuiCol_SliderGrabActive:
			return T("menu.settings.color_slider_grab_active", "Slider Grab (Active)");
		case ImGuiCol_Button:
			return T("menu.settings.color_button", "Button");
		case ImGuiCol_ButtonHovered:
			return T("menu.settings.color_button_hovered", "Button (Hovered)");
		case ImGuiCol_ButtonActive:
			return T("menu.settings.color_button_active", "Button (Active)");
		case ImGuiCol_Header:
			return T("menu.settings.color_header", "Header");
		case ImGuiCol_HeaderHovered:
			return T("menu.settings.color_header_hovered", "Header (Hovered)");
		case ImGuiCol_HeaderActive:
			return T("menu.settings.color_header_active", "Header (Active)");
		case ImGuiCol_Separator:
			return T("menu.settings.color_separator", "Separator");
		case ImGuiCol_SeparatorHovered:
			return T("menu.settings.color_separator_hovered", "Separator (Hovered)");
		case ImGuiCol_SeparatorActive:
			return T("menu.settings.color_separator_active", "Separator (Active)");
		case ImGuiCol_ResizeGrip:
			return T("menu.settings.color_resize_grip", "Resize Grip");
		case ImGuiCol_ResizeGripHovered:
			return T("menu.settings.color_resize_grip_hovered", "Resize Grip (Hovered)");
		case ImGuiCol_ResizeGripActive:
			return T("menu.settings.color_resize_grip_active", "Resize Grip (Active)");
		case ImGuiCol_InputTextCursor:
			return T("menu.settings.color_input_text_cursor", "Input Text Cursor");
		case ImGuiCol_Tab:
			return T("menu.settings.color_tab", "Tab");
		case ImGuiCol_TabHovered:
			return T("menu.settings.color_tab_hovered", "Tab (Hovered)");
		case ImGuiCol_TabSelected:
			return T("menu.settings.color_tab_selected", "Tab (Selected)");
		case ImGuiCol_TabSelectedOverline:
			return T("menu.settings.color_tab_selected_overline", "Tab Selected Overline");
		case ImGuiCol_TabDimmed:
			return T("menu.settings.color_tab_dimmed", "Tab (Dimmed)");
		case ImGuiCol_TabDimmedSelected:
			return T("menu.settings.color_tab_dimmed_selected", "Tab (Dimmed Selected)");
		case ImGuiCol_TabDimmedSelectedOverline:
			return T("menu.settings.color_tab_dimmed_selected_overline", "Tab Dimmed Selected Overline");
		case ImGuiCol_DockingPreview:
			return T("menu.settings.color_docking_preview", "Docking Preview");
		case ImGuiCol_DockingEmptyBg:
			return T("menu.settings.color_docking_empty_bg", "Docking Empty Background");
		case ImGuiCol_PlotLines:
			return T("menu.settings.color_plot_lines", "Plot Lines");
		case ImGuiCol_PlotLinesHovered:
			return T("menu.settings.color_plot_lines_hovered", "Plot Lines (Hovered)");
		case ImGuiCol_PlotHistogram:
			return T("menu.settings.color_plot_histogram", "Plot Histogram");
		case ImGuiCol_PlotHistogramHovered:
			return T("menu.settings.color_plot_histogram_hovered", "Plot Histogram (Hovered)");
		case ImGuiCol_TableHeaderBg:
			return T("menu.settings.color_table_header_bg", "Table Header Background");
		case ImGuiCol_TableBorderStrong:
			return T("menu.settings.color_table_border_strong", "Table Border (Strong)");
		case ImGuiCol_TableBorderLight:
			return T("menu.settings.color_table_border_light", "Table Border (Light)");
		case ImGuiCol_TableRowBg:
			return T("menu.settings.color_table_row_bg", "Table Row Background");
		case ImGuiCol_TableRowBgAlt:
			return T("menu.settings.color_table_row_bg_alt", "Table Row Background (Alternate)");
		case ImGuiCol_TextLink:
			return T("menu.settings.color_text_link", "Text Link");
		case ImGuiCol_TextSelectedBg:
			return T("menu.settings.color_text_selected_bg", "Text Selection Background");
		case ImGuiCol_TreeLines:
			return T("menu.settings.color_tree_lines", "Tree Lines");
		case ImGuiCol_DragDropTarget:
			return T("menu.settings.color_drag_drop_target", "Drag & Drop Target");
		case ImGuiCol_DragDropTargetBg:
			return T("menu.settings.color_drag_drop_target_bg", "Drag & Drop Target Background");
		case ImGuiCol_UnsavedMarker:
			return T("menu.settings.color_unsaved_marker", "Unsaved Marker");
		case ImGuiCol_NavCursor:
			return T("menu.settings.color_nav_cursor", "Navigation Cursor");
		case ImGuiCol_NavWindowingHighlight:
			return T("menu.settings.color_nav_windowing_highlight", "Window Navigation Highlight");
		case ImGuiCol_NavWindowingDimBg:
			return T("menu.settings.color_nav_windowing_dim_bg", "Window Navigation Dim Background");
		case ImGuiCol_ModalWindowDimBg:
			return T("menu.settings.color_modal_window_dim_bg", "Modal Window Dim Background");
		default:
			return ImGui::GetStyleColorName(colorIndex);
		}
	}

	void SeparatorTextWithFont(const char* text, Menu::FontRole role)
	{
		MenuFonts::FontRoleGuard guard(role);
		ImGui::SeparatorText(text);
	}

	void SeparatorTextWithFont(const std::string& text, Menu::FontRole role)
	{
		SeparatorTextWithFont(text.c_str(), role);
	}

	bool ComboWithFont(const char* label, int* currentItem, const char* const items[], int itemCount, Menu::FontRole role)
	{
		FontRoleGuard guard(role);
		return ImGui::Combo(label, currentItem, items, itemCount);
	}

	/** @brief Rebinding row; while the binding differs from its default, a reset button follows it. */
	void KeybindingRow(const char* label, std::vector<InputCombo>& combo, const std::vector<InputCombo>& defaultCombo, bool& capturing, const char* id)
	{
		Util::InputComboWidget(label, combo, capturing, id);
		if (capturing || combo == defaultCombo)
			return;

		ImGui::SameLine();
		const auto tooltip = I18n::GetSingleton()->Format("menu.settings.reset_keybinding_tooltip",
			{ { "key", Util::Input::KeyIdToString(defaultCombo) } }, "Reset to default ({key})");
		if (BrowserUI::IconButton(std::format("##Reset{}", id).c_str(), Icons::TI(ICON_TI_RESTORE), tooltip.c_str()))
			combo = defaultCombo;
	}

	bool IsPresetThemeSelected()
	{
		std::string selected = globals::menu->GetSettings().SelectedThemePreset;
		return !selected.empty() && ThemeManager::GetSingleton()->IsPresetTheme(selected);
	}
}

std::span<const SettingsSearchEntry> SettingsTabRenderer::GetSearchIndex()
{
	return kSearchIndex;
}

void SettingsTabRenderer::RenderThemeSaveNote()
{
	auto& ts = globals::menu->GetSettings().Theme;
	ImGui::PushStyleColor(ImGuiCol_Text, ts.StatusPalette.InfoColor);
	ImGui::TextWrapped("%s", T("menu.settings.theme_save_model",
								 "Theme edits are saved into the theme file with Save Theme. "
								 "Which theme is selected is saved with Save Settings (Ctrl+S)."));
	ImGui::PopStyleColor();
	ImGui::Spacing();
}

void SettingsTabRenderer::RenderShadersSection()
{
	auto shaderCache = globals::shaderCache;

	bool useCustomShaders = shaderCache->IsEnabled();
	if (ImGui::Checkbox(T("menu.settings.use_custom_shaders", "Use Custom Shaders"), &useCustomShaders)) {
		shaderCache->SetEnabled(useCustomShaders);
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("menu.settings.use_custom_shaders_tooltip", "Disabling this effectively disables all features."));
	}

	bool useDiskCache = shaderCache->IsDiskCache();
	if (ImGui::Checkbox(T("menu.settings.enable_disk_cache", "Enable Disk Cache"), &useDiskCache)) {
		shaderCache->SetDiskCache(useDiskCache);
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("menu.settings.enable_disk_cache_tooltip", "Disables loading shaders from disk and prevents saving compiled shaders to disk cache."));
	}

	bool skipUnchanged = shaderCache->IsSkipUnchangedShaders();
	ImGui::BeginDisabled(!useDiskCache);
	if (ImGui::Checkbox(T("menu.settings.skip_unchanged_shaders", "Skip Unchanged Shaders"), &skipUnchanged)) {
		shaderCache->SetSkipUnchangedShaders(skipUnchanged);
	}
	ImGui::EndDisabled();
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("menu.settings.skip_unchanged_shaders_tooltip",
							  "When enabled, each shader is recompiled from source only if its .hlsl file "
							  "is newer than the cached .bin on disk. "
							  "Shaders whose source has not changed are loaded directly from the disk cache, "
							  "avoiding the full startup compilation cost. "
							  "Useful for iterative testing: change a shader file and only that shader is rebuilt. "
							  "Requires 'Enable Disk Cache' to be active."));
	}

	bool useAsync = shaderCache->IsAsync();
	if (ImGui::Checkbox(T("menu.settings.enable_async", "Enable Async"), &useAsync)) {
		shaderCache->SetAsync(useAsync);
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("menu.settings.enable_async_tooltip", "Skips a shader being replaced if it hasn't been compiled yet. Also makes compilation blazingly fast!"));
	}

	// Skip confirmation when clearing shader cache
	auto& menuSettings = globals::menu->GetSettings();
	bool skipConfirmation = menuSettings.SkipClearCacheConfirmation;
	if (ImGui::Checkbox(T("menu.settings.skip_clear_cache_dialogue", "Skip Clear Cache Dialogue"), &skipConfirmation)) {
		menuSettings.SkipClearCacheConfirmation = skipConfirmation;
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("menu.settings.skip_clear_cache_dialogue_tooltip", "When checked, the shader cache will be cleared immediately without asking for confirmation."));
	}

	if (shaderCache->GetTotalTasks() > 0) {
		ImGui::Text(T("menu.settings.last_shader_cache_duration", "Last shader cache build duration: %s"),
			shaderCache->GetShaderStatsString(true, true).c_str());

		ShaderCacheStatusWidget::DrawBreakdownBar();

		auto state = globals::state;
		if (state->IsDeveloperMode()) {
			ImGui::Text("Threads: %d compile, %d background, %d pool | P-cores: %d",
				(int)shaderCache->compilationThreadCount,
				(int)shaderCache->backgroundCompilationThreadCount,
				(int)shaderCache->compilationPool.get_thread_count(),
				(int)Util::GetPerformanceCoreCount());
		}
	}
}

void SettingsTabRenderer::RenderKeybindingsSection(SettingsState& state)
{
	auto& settings = globals::menu->GetSettings();
	static const Menu::Settings defaults{};

	SeparatorTextWithFont(T("menu.settings.keys_menus_editors", "Menus & Editors"), Menu::FontRole::Subheading);
	KeybindingRow(T("menu.settings.toggle_key", "Toggle Key:"), settings.ToggleKey, defaults.ToggleKey, state.settingToggleKey, "Change##toggle");
	KeybindingRow(T("menu.settings.cs_editor_toggle_key", "CS Editor Toggle Key:"), settings.CSEditorToggleKey, defaults.CSEditorToggleKey, state.settingCSEditorToggleKey, "Change##CSEditorToggle");
	KeybindingRow(T("menu.settings.effects11_editor_key", "Effects 11 Editor Key:"), settings.Effects11EditorKey, defaults.Effects11EditorKey, state.settingEffects11EditorKey, "Change##Effects11Editor");

	SeparatorTextWithFont(T("menu.settings.keys_overlays_capture", "Overlays & Capture"), Menu::FontRole::Subheading);
	KeybindingRow(T("menu.settings.overlay_toggle_key", "Overlay Toggle Key:"), settings.OverlayToggleKey, defaults.OverlayToggleKey, state.settingOverlayToggleKey, "Change##OverlayToggle");
	KeybindingRow(T("menu.settings.screenshot_key", "Screenshot Key:"), settings.ScreenshotKey, defaults.ScreenshotKey, state.settingScreenshotKey, "Change##Screenshot");

	SeparatorTextWithFont(T("menu.settings.keys_debug", "Debug"), Menu::FontRole::Subheading);
	KeybindingRow(T("menu.settings.effect_toggle_key", "Effect Toggle Key:"), settings.EffectToggleKey, defaults.EffectToggleKey, state.settingsEffectsToggle, "Change##EffectToggle");
	KeybindingRow(T("menu.settings.effects11_toggle_key", "Effects 11 Toggle Key:"), settings.Effects11ToggleKey, defaults.Effects11ToggleKey, state.settingEffects11ToggleKey, "Change##Effects11Toggle");
	KeybindingRow(T("menu.settings.skip_compilation_key", "Skip Compilation Key:"), settings.SkipCompilationKey, defaults.SkipCompilationKey, state.settingSkipCompilationKey, "Change##skip");
	KeybindingRow(T("menu.settings.shader_block_prev_key", "Shader Block Previous Key:"), settings.ShaderBlockPrevKey, defaults.ShaderBlockPrevKey, state.settingShaderBlockPrevKey, "Change##ShaderBlockPrev");
	KeybindingRow(T("menu.settings.shader_block_next_key", "Shader Block Next Key:"), settings.ShaderBlockNextKey, defaults.ShaderBlockNextKey, state.settingShaderBlockNextKey, "Change##ShaderBlockNext");
	if (!settings.EnableShaderBlocking) {
		ImGui::TextDisabled("%s", T("menu.settings.shader_block_keys_hint",
									  "The shader block keys work while Enable Shader Blocking is on (Shader Debug, Developer Mode only)."));
	}
}

void SettingsTabRenderer::RenderLanguageSection()
{
	auto* i18n = I18n::GetSingleton();
	auto locales = i18n->GetAvailableLocales();
	auto currentLocale = i18n->GetCurrentLocale();

	// Find the display name for the current locale
	std::string currentDisplayName = currentLocale;
	for (const auto& [code, name] : locales) {
		if (code == currentLocale) {
			currentDisplayName = GetLocaleDisplayLabel(code, name);
			break;
		}
	}

	if (ImGui::BeginCombo(T("menu.settings.language", "Language"), currentDisplayName.c_str())) {
		for (const auto& [code, name] : locales) {
			bool isSelected = (code == currentLocale);
			auto displayName = GetLocaleDisplayLabel(code, name);
			ImGui::PushID(code.c_str());
			if (Util::Selectable(displayName.c_str(), isSelected)) {
				i18n->SetLocale(code);
				MenuFonts::GetAtlasState().pendingFontReload = true;
			}
			if (isSelected) {
				ImGui::SetItemDefaultFocus();
			}
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}
	if (const int coverage = i18n->GetCoveragePercent(); coverage < 100) {
		ImGui::TextDisabled("%s", i18n->Format("menu.settings.language_coverage",
										  { { "percent", std::to_string(coverage) } },
										  "Only {percent}% of the interface is translated in this language; the rest stays in English.")
									  .c_str());
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("menu.settings.language_tooltip", "Select the display language for the Community Shaders interface."));
	}
}

void SettingsTabRenderer::RenderBehaviorSection()
{
	auto& themeSettings = globals::menu->GetSettings().Theme;

	// The master switch is stored as its opposite (ReduceUIAnimations), so settings files from before the
	// animation speed option keep their meaning
	auto& menuSettings = globals::menu->GetSettings();
	bool animationsOn = !menuSettings.ReduceUIAnimations;
	if (ImGui::Checkbox(T("menu.settings.ui_animations", "UI animations"), &animationsOn))
		menuSettings.ReduceUIAnimations = !animationsOn;
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("menu.settings.ui_animations_tooltip", "Hover fades, sliding highlights and fade-ins in the menu. Turn off to make every change snap instead of easing."));
	}
	{
		Util::DisableGuard disable(!animationsOn);
		ImGui::SliderFloat(T("menu.settings.animation_speed", "Animation speed"), &menuSettings.AnimationSpeed,
			ThemeManager::Constants::ANIM_SPEED_MIN, ThemeManager::Constants::ANIM_SPEED_MAX, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T("menu.settings.animation_speed_tooltip", "How fast menu animations run: 0.5x is slower, 2x is quicker. 1x is the default."));
		}
	}

	ImGui::Checkbox(T("menu.settings.show_status_bar", "Show Status Bar"), &themeSettings.ShowFooter);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("menu.settings.show_status_bar_tooltip",
							  "Shows a bar at the bottom of the window with unsaved changes, shader compilation, "
							  "failed shaders and feature issues, plus the GPU, game version and swap chain"));
	}

	ImGui::Checkbox(T("menu.settings.auto_hide_feature_list", "Auto-hide Feature List"), &globals::menu->GetSettings().AutoHideFeatureList);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("menu.settings.auto_hide_feature_list_tooltip", "Automatically hides the left feature list panel. Move cursor to the left edge to show it."));
	}

	{
		FontRoleGuard comboFont(Menu::FontRole::Body);
		const char* sortModes[] = {
			T("menu.features.sort_by_category", "By Category"),
			T("menu.features.sort_alphabetically", "Alphabetical")
		};
		auto& sort = globals::menu->GetSettings().FeatureListSort;
		// Unknown values from a hand-edited config show as the category order the list falls back to
		int sortIndex = sort == Menu::FeatureSortMode::Alphabetical ? 1 : 0;
		if (ImGui::Combo(T("menu.settings.feature_list_order", "Feature List Order"), &sortIndex, sortModes, IM_ARRAYSIZE(sortModes)))
			sort = sortIndex == 1 ? Menu::FeatureSortMode::Alphabetical : Menu::FeatureSortMode::Category;
	}

	if (ImGui::Checkbox(T("menu.settings.require_shift_to_dock", "Require Shift to Dock"), &globals::menu->GetSettings().RequireShiftToDock)) {
		ImGui::GetIO().ConfigDockingWithShift = globals::menu->GetSettings().RequireShiftToDock;
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("menu.settings.require_shift_to_dock_tooltip", "When enabled, you must hold Shift while dragging to dock/snap windows. Prevents accidental docking."));
	}

	ImGui::SliderFloat(T("menu.settings.tooltip_hover_delay", "Tooltip Hover Delay"), &themeSettings.TooltipHoverDelay, 0.0f, 2.0f, "%.2f s", ImGuiSliderFlags_AlwaysClamp);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(T("menu.settings.tooltip_hover_delay_tooltip", "Time in seconds to wait before a tooltip appears when hovering over an item."));
	}

	bool warnForcedSettings = !globals::menu->GetSettings().SkipConstraintWarning;
	if (ImGui::Checkbox(T("menu.settings.warn_forced_settings", "Warn When Settings Are Forced"), &warnForcedSettings))
		globals::menu->GetSettings().SkipConstraintWarning = !warnForcedSettings;
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("menu.settings.warn_forced_settings_tooltip",
							  "Shows a popup when a setting change makes another feature force one of its settings to a fixed value."));
	}

	ImGui::Spacing();
	if (Util::Button(T("menu.settings.reset_window_layout", "Reset Window Layout")))
		globals::menu->resetLayout = true;
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("menu.settings.reset_window_layout_tooltip", "Moves the menu window back to the centre of the screen at its default size."));
	}
}

void SettingsTabRenderer::RenderHeaderEffectsSection()
{
	auto& themeSettings = globals::menu->GetSettings().Theme;

	SeparatorTextWithFont(T("menu.settings.section_header", "Header"), Menu::FontRole::Subheading);

	if (ImGui::Checkbox(T("menu.settings.use_monochrome_icons", "Use Monochrome Icons"), &themeSettings.UseMonochromeIcons)) {
		Util::IconLoader::PendingReload() = true;
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("menu.settings.use_monochrome_icons_tooltip", "Uses white monochrome icons that adapt to your theme's text color"));
	}
	ImGui::SameLine();
	if (ImGui::Checkbox(T("menu.settings.use_monochrome_cs_logo", "Use Monochrome CS Logo"), &themeSettings.UseMonochromeLogo)) {
		Util::IconLoader::PendingReload() = true;
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("menu.settings.use_monochrome_cs_logo_tooltip", "Uses monochrome version of the Community Shaders logo"));
	}

	ImGui::Checkbox(T("menu.settings.center_header_title", "Center Header Title"), &themeSettings.CenterHeader);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("menu.settings.center_header_title_tooltip", "Centers the Community Shaders title and logo in the header title bar"));
	}

	SeparatorTextWithFont(T("menu.settings.visual_effects", "Visual Effects"), Menu::FontRole::Subheading);

	if (ImGui::Checkbox(T("menu.settings.background_blur", "Background Blur"), &themeSettings.BackgroundBlurEnabled)) {
		BackgroundBlur::SetEnabled(themeSettings.BackgroundBlurEnabled);
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("menu.settings.background_blur_tooltip", "Applies a blur effect to the background behind the menu window."));
	}

	ImGui::Checkbox(T("menu.settings.menu_fade", "Menu Fade"), &themeSettings.MenuFadeEnabled);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("menu.settings.menu_fade_tooltip", "Fades the menu and its background blur in and out when it is opened or closed."));
	}

	if (ImGui::Checkbox(T("menu.settings.use_custom_cursor", "Use Custom Theme Cursor"), &themeSettings.UseCustomCursor)) {
		globals::menu->pendingCursorReload = true;
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("menu.settings.use_custom_cursor_tooltip",
							  "Loads cursor PNGs from the active theme folder (Themes/<preset>/).\n"
							  "Supported files include cursor.png (arrow), cursor_text.png (typing), cursor_resize_ew.png, cursor_resize_ns.png, and more.\n"
							  "Missing types fall back to the default ImGui cursor. Configure per-type hotspots in theme JSON."));
	}

	if (themeSettings.UseCustomCursor) {
		ImGui::Indent();
		const int loadedCount = Util::CursorLoader::GetLoadedCount();
		ImGui::TextDisabled("%s: %d",
			T("menu.settings.custom_cursor_status", "Custom cursor images loaded"),
			loadedCount);
		ImGui::Unindent();
	}
}

void SettingsTabRenderer::RenderThemesSection()
{
	auto& themeSettings = globals::menu->GetSettings().Theme;

	// Static variables for popup state and new theme creation
	static Util::ConfirmationPopup deleteThemePopup(
		T("menu.settings.delete_theme_title", "Delete Theme"), "",
		T("menu.settings.delete_button", "Delete"),
		T("menu.settings.cancel", "Cancel"));
	static bool showCreateThemePopup = false;
	static char newThemeName[128] = "";
	static char newThemeDisplayName[128] = "";
	static char newThemeDescription[256] = "";
	static bool showValidationError = false;

	// Update feedback tracking
	static bool showUpdateFeedback = false;
	struct ChangedSetting
	{
		std::string path;
		std::string oldValue;
		std::string newValue;
	};
	static std::vector<ChangedSetting> changedSettings;
	static bool updateSuccess = false;

	// Theme Preset Selection
	SeparatorTextWithFont(T("menu.settings.theme_preset", "Theme Preset"), Menu::FontRole::Subheading);

	// Get theme manager
	auto themeManager = ThemeManager::GetSingleton();

	// Get available themes (force discovery if not done)
	if (!themeManager->IsDiscovered()) {
		themeManager->DiscoverThemes();
	}

	const auto& themes = themeManager->GetThemes();

	// Create dropdown items - using static storage to avoid dangling pointers
	static std::vector<std::string> displayNames;
	static std::vector<const char*> items;

	// Clear and rebuild the lists
	displayNames.clear();
	items.clear();

	// Reserve capacity to prevent reallocations that would invalidate pointers
	displayNames.reserve(themes.size());
	items.reserve(themes.size());

	for (const auto& theme : themes) {
		displayNames.push_back(theme.displayName);
		items.push_back(displayNames.back().c_str());
	}

	// Find current selection index - default to "Default" if no theme selected
	int currentItem = 0;  // Default to first theme (Default Dark)
	std::string currentThemePreset = globals::menu->GetSettings().SelectedThemePreset;

	// If no theme is selected, default to "Default"
	if (currentThemePreset.empty()) {
		currentThemePreset = "Default";
		globals::menu->GetSettings().SelectedThemePreset = "Default";
	}

	for (size_t i = 0; i < themes.size(); ++i) {
		if (themes[i].name == currentThemePreset) {
			currentItem = static_cast<int>(i);
			break;
		}
	}

	// Theme preset dropdown
	if (ComboWithFont("##ThemePreset", &currentItem, items.data(), static_cast<int>(items.size()), Menu::FontRole::Body)) {
		std::string selectedTheme = themes[currentItem].name;
		if (selectedTheme != currentThemePreset && globals::menu->LoadThemePreset(selectedTheme)) {
			// Theme loaded successfully, update UI
			currentThemePreset = selectedTheme;
			showUpdateFeedback = false;
		}
	}

	if (Util::Button(T("menu.settings.refresh", "Refresh"))) {
		themeManager->RefreshThemes();
		// Ensure a valid theme is still selected
		const auto* themeInfo = themeManager->GetThemeInfo(currentThemePreset);
		if (!themeInfo) {
			currentThemePreset = "Default";
			globals::menu->GetSettings().SelectedThemePreset = "Default";
		}

		for (size_t i = 0; i < themes.size(); ++i) {
			if (themes[i].name == currentThemePreset) {
				currentItem = static_cast<int>(i);
				break;
			}
		}
	}

	ImGui::SameLine();
	if (Util::Button(T("menu.settings.open_themes_folder", "Open Themes Folder"))) {
		std::filesystem::path themesPath = Util::PathHelpers::GetThemesRealPath();
		ShellExecuteA(NULL, "open", themesPath.string().c_str(), NULL, NULL, SW_SHOWNORMAL);
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("%s", T("menu.settings.open_themes_folder_tooltip", "Opens the Themes folder where you can add custom theme files."));
	}

	// Selected theme section: name + description
	ImGui::Spacing();
	ImGui::Separator();
	if (currentItem >= 0 && currentItem < static_cast<int>(themes.size())) {
		ImGui::Spacing();
		const auto& selectedTheme = themes[currentItem];
		ImGui::Text("%s", T("menu.settings.selected_theme", "Selected Theme: "));
		ImGui::SameLine(0, 0);
		ImGui::TextColored(themeSettings.StatusPalette.InfoColor, "%s", selectedTheme.displayName.c_str());
		if (!selectedTheme.description.empty()) {
			ImGui::TextWrapped("%s", selectedTheme.description.c_str());
		}
	}
	ImGui::Spacing();

	const bool isPreset = IsPresetThemeSelected();
	const auto* currentThemeInfo = themeManager->GetThemeInfo(currentThemePreset);

	if (!isPreset) {
		if (Util::ButtonWithFlash(T("menu.settings.save_theme", "Save Theme"))) {
			if (currentThemeInfo) {
				// Get current settings
				json currentThemeJson;
				globals::menu->SaveTheme(currentThemeJson);

				// Get saved theme settings for comparison
				json savedThemeJson = currentThemeInfo->themeData["Theme"];

				// Compare and collect changed settings (with old/new values)
				changedSettings.clear();
				std::function<void(const std::string&, const json&, const json&)> diffWalker;
				diffWalker = [&](const std::string& path, const json& oldVal, const json& newVal) {
					// Handle objects by recursing through union of keys
					if (oldVal.is_object() && newVal.is_object()) {
						std::set<std::string> keys;
						for (auto& [k, _] : oldVal.items()) keys.insert(k);
						for (auto& [k, _] : newVal.items()) keys.insert(k);
						for (const auto& k : keys) {
							auto nextPath = path.empty() ? k : path + "." + k;
							const json& oldChild = oldVal.contains(k) ? oldVal[k] : json();
							const json& newChild = newVal.contains(k) ? newVal[k] : json();
							diffWalker(nextPath, oldChild, newChild);
						}
						return;
					}

					// For arrays or primitives, record if different
					if (oldVal != newVal) {
						changedSettings.push_back({ path.empty() ? "<root>" : path,
							oldVal.is_null() ? "null" : oldVal.dump(),
							newVal.is_null() ? "null" : newVal.dump() });
					}
				};

				diffWalker("", savedThemeJson, currentThemeJson["Theme"]);

				logger::info("Attempting to update theme: '{}'", currentThemePreset);

				// Overwrite the current theme with updated settings
				if (themeManager->SaveTheme(currentThemePreset, currentThemeJson["Theme"],
						currentThemeInfo->displayName, currentThemeInfo->description)) {
					logger::info("Theme '{}' updated successfully", currentThemePreset);
					updateSuccess = true;
					showUpdateFeedback = true;
				} else {
					logger::error("Failed to update theme: '{}'", currentThemePreset);
					updateSuccess = false;
					showUpdateFeedback = true;
					changedSettings.clear();
				}
			} else {
				logger::warn("Cannot update theme '{}' - theme info not found", currentThemePreset);
				updateSuccess = false;
				showUpdateFeedback = true;
				changedSettings.clear();
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(T("menu.settings.save_theme_tooltip", "Updates the currently selected theme (%s) with your current settings"), currentThemePreset.c_str());
		}

		ImGui::SameLine();
	}

	if (Util::ButtonWithFlash(T("menu.settings.save_as_new_theme", "Save As New Theme"))) {
		showCreateThemePopup = true;
		memset(newThemeName, 0, sizeof(newThemeName));
		memset(newThemeDisplayName, 0, sizeof(newThemeDisplayName));
		memset(newThemeDescription, 0, sizeof(newThemeDescription));
		showValidationError = false;
	}

	if (!isPreset && currentThemeInfo && !currentThemeInfo->filePath.empty()) {
		ImGui::SameLine();
		if (Util::ErrorButtonWithFlash(T("menu.settings.delete_theme", "Delete"))) {
			deleteThemePopup.message =
				std::string(T("menu.settings.delete_theme_confirm_part1",
					"Are you sure you want to delete the theme '")) +
				(currentThemeInfo->displayName.empty() ? currentThemePreset : currentThemeInfo->displayName) +
				T("menu.settings.delete_theme_confirm_part2",
					"'?\n\nThis will permanently remove the theme file. This cannot be undone.");
			deleteThemePopup.Request();
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(T("menu.settings.delete_theme_tooltip", "Delete the theme file for '%s'. This cannot be undone."),
				(currentThemeInfo->displayName.empty() ? currentThemePreset : currentThemeInfo->displayName).c_str());
		}
	}

	// Display update feedback below the buttons
	if (showUpdateFeedback) {
		ImGui::Spacing();
		ImGui::Separator();

		if (updateSuccess) {
			if (changedSettings.empty()) {
				ImGui::TextColored(themeSettings.StatusPalette.SuccessColor, "%s", T("menu.settings.theme_updated_no_changes", "Theme updated successfully - no changes detected"));
			} else {
				ImGui::TextColored(themeSettings.StatusPalette.SuccessColor, "%s", T("menu.settings.theme_updated_with_changes", "Theme updated successfully! Changed settings:"));
				ImGui::Indent();
				for (const auto& change : changedSettings) {
					ImGui::BulletText("%s: %s -> %s", change.path.c_str(), change.oldValue.c_str(), change.newValue.c_str());
				}
				ImGui::Unindent();
			}
		} else {
			ImGui::TextColored(themeSettings.StatusPalette.Error, "%s", T("menu.settings.theme_update_failed", "Failed to update theme"));
		}

		ImGui::Separator();
	}

	// Create Theme Popup
	if (showCreateThemePopup) {
		ImGui::OpenPopup(T("menu.settings.create_new_theme", "Create New Theme"));
	}

	// Popup modal for creating new theme
	if (auto popup = Util::CenteredPopupModal(T("menu.settings.create_new_theme", "Create New Theme"), &showCreateThemePopup)) {
		ImGui::Text("%s", T("menu.settings.create_new_theme_hint", "Create a new theme with your current settings:"));
		ImGui::Separator();

		auto safeNewThemeName = Util::FileHelpers::SanitizeFileName(newThemeName);
		bool isThemeNameEmpty = safeNewThemeName.empty();
		bool isDuplicateName = false;
		bool isDuplicateDisplayName = false;

		for (const auto& t : themes) {
			if (Util::IEquals(t.name, safeNewThemeName))
				isDuplicateName = true;
			if (strlen(newThemeDisplayName) > 0 && Util::IEquals(t.displayName, newThemeDisplayName))
				isDuplicateDisplayName = true;
			if (isDuplicateName && isDuplicateDisplayName)
				break;
		}
		bool isThemeNameError = isThemeNameEmpty || isDuplicateName;

		// Highlight the input field if invalid and validation error is shown
		if (isThemeNameError && showValidationError) {
			ImGui::PushStyleColor(ImGuiCol_Border, themeSettings.StatusPalette.Error);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
		}

		ImGui::InputText(T("menu.settings.theme_name", "Theme Name"), newThemeName, sizeof(newThemeName));

		if (isThemeNameError && showValidationError) {
			ImGui::PopStyleVar();
			ImGui::PopStyleColor();
		}

		// Show inline error message
		if (showValidationError) {
			if (isThemeNameEmpty) {
				ImGui::TextColored(themeSettings.StatusPalette.Error, "%s", T("menu.settings.theme_name_required", "Theme name is required"));
			} else if (isDuplicateName) {
				ImGui::TextColored(themeSettings.StatusPalette.Error, "%s", T("menu.settings.theme_name_duplicate", "A theme with this name already exists"));
			}
		}

		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T("menu.settings.theme_name_tooltip", "File name for the theme (without .json extension)"));
		}

		// Highlight the input field if invalid and validation error is shown
		if (isDuplicateDisplayName && showValidationError) {
			ImGui::PushStyleColor(ImGuiCol_Border, themeSettings.StatusPalette.Error);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
		}

		ImGui::InputText(T("menu.settings.display_name", "Display Name"), newThemeDisplayName, sizeof(newThemeDisplayName));

		if (isDuplicateDisplayName && showValidationError) {
			ImGui::PopStyleVar();
			ImGui::PopStyleColor();
			ImGui::TextColored(themeSettings.StatusPalette.Error, "%s", T("menu.settings.display_name_duplicate", "A theme with this display name already exists"));
		}

		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T("menu.settings.display_name_tooltip", "Human-readable name shown in the dropdown"));
		}

		{
			float scale = Util::GetUIScale();
			ImGui::InputTextMultiline(T("menu.settings.description", "Description"), newThemeDescription, sizeof(newThemeDescription), ImVec2(400 * scale, 80 * scale));
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T("menu.settings.description_tooltip", "Optional description for the theme"));
		}

		ImGui::Separator();

		// Buttons
		if (Util::ButtonWithFlash(T("menu.settings.create_theme", "Create Theme"))) {
			if (!isThemeNameEmpty && !isDuplicateName && !isDuplicateDisplayName) {
				// Valid theme name, reset error state and proceed
				showValidationError = false;

				// Use the existing SaveTheme method to serialize the theme settings
				json currentThemeJson;
				globals::menu->SaveTheme(currentThemeJson);

				std::string displayName = strlen(newThemeDisplayName) > 0 ? std::string(newThemeDisplayName) : std::string(newThemeName);
				std::string description = strlen(newThemeDescription) > 0 ? std::string(newThemeDescription) : "";

				logger::info("Attempting to save new theme: '{}' with display name: '{}'", safeNewThemeName, displayName);

				if (themeManager->SaveTheme(std::string(newThemeName), currentThemeJson["Theme"], displayName, description)) {
					logger::info("Theme saved successfully. Loading theme preset: '{}'", safeNewThemeName);
					// Theme created successfully, load it and exit create mode
					globals::menu->LoadThemePreset(safeNewThemeName);
					showValidationError = false;
					showCreateThemePopup = false;
					ImGui::CloseCurrentPopup();
					logger::info("Theme creation complete. Total themes: {}", themeManager->GetThemes().size());
				} else {
					logger::error("Failed to save theme: '{}'", newThemeName);
				}
			} else {
				// Empty theme name, show validation error
				showValidationError = true;
			}
		}

		ImGui::SameLine();
		if (Util::Button(T("menu.settings.cancel", "Cancel"))) {
			showCreateThemePopup = false;
			ImGui::CloseCurrentPopup();
		}
	}

	if (deleteThemePopup.Draw() && currentThemeInfo && !currentThemeInfo->filePath.empty()) {
		auto result = Util::FileHelpers::SafeDelete(currentThemeInfo->filePath, "Theme '" + currentThemePreset + "'");
		if (result.success) {
			themeManager->RefreshThemes();
			globals::menu->LoadThemePreset("Default");
			currentThemePreset = "Default";
		} else {
			logger::warn("Failed to delete theme '{}': {}", currentThemePreset, result.errorMessage);
		}
	}
}

void SettingsTabRenderer::RenderFontsSection()
{
	auto* menuInstance = globals::menu;
	auto& themeSettings = menuInstance->GetSettings().Theme;

	SeparatorTextWithFont(T("menu.settings.font", "Font"), Menu::FontRole::Subheading);

	bool& useAutoFont = menuInstance->GetSettings().UseResolutionFont;
	if (ImGui::Checkbox(T("menu.settings.use_resolution_based_font_size", "Use resolution-based font size"), &useAutoFont)) {
		if (!useAutoFont) {
			// Seed the fixed-size slider with the current effective size so it doesn't jump
			float effective = ThemeManager::ResolveFontSize(*menuInstance);
			themeSettings.FontSize = std::clamp(effective, ThemeManager::Constants::MIN_FONT_SIZE, ThemeManager::Constants::MAX_FONT_SIZE);
		}
		MenuFonts::GetAtlasState().pendingFontReload = true;
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(T("menu.settings.use_resolution_based_font_size_tooltip", "When enabled, the UI font size scales with your screen resolution. Disable to set a fixed size."));
	}

	ImGui::BeginDisabled(useAutoFont);
	if (ImGui::SliderFloat(T("menu.settings.base_font_size", "Base Font Size"), &themeSettings.FontSize, ThemeManager::Constants::MIN_FONT_SIZE, ThemeManager::Constants::MAX_FONT_SIZE, "%.0f")) {
		MenuFonts::GetAtlasState().pendingFontReload = true;
	}
	ImGui::EndDisabled();

	float effectiveNow = ThemeManager::ResolveFontSize(*menuInstance);
	ImGui::Text(T("menu.settings.effective_size", "Effective size: %.0f px"), std::round(effectiveNow));

	static Util::Fonts::Catalog fontCatalog;
	static bool catalogInitialized = false;
	auto refreshFontCatalog = [&](bool forceRescan = false) {
		fontCatalog = Util::Fonts::DiscoverFontCatalog(forceRescan);
	};

	if (!MenuFonts::GetAtlasState().wantsFontPreviewAtlas) {
		MenuFonts::GetAtlasState().wantsFontPreviewAtlas = true;
		MenuFonts::GetAtlasState().pendingFontReload = true;
	}

	if (!catalogInitialized) {
		refreshFontCatalog(false);
		catalogInitialized = true;
	}

	ImGui::Spacing();
	SeparatorTextWithFont(T("menu.settings.font_roles", "Font Roles"), Menu::FontRole::Subheading);

	if (fontCatalog.families.empty()) {
		Util::Text::Warning("%s", T("menu.settings.no_fonts_found", "No fonts found. Place .ttf files in Interface/CommunityShaders/Fonts/"));
	}

	for (size_t roleIndex = 0; roleIndex < Menu::FontRoleDescriptors.size(); ++roleIndex) {
		auto role = static_cast<Menu::FontRole>(roleIndex);
		auto descriptor = Menu::FontRoleDescriptors[roleIndex];

		ImGui::PushID(static_cast<int>(roleIndex));
		{
			FontRoleGuard headingFont(Menu::FontRole::Subheading);
			ImGui::TextUnformatted(descriptor.displayName.data());
		}

		MenuFonts::Selector::RenderRolePickers(*menuInstance, themeSettings, fontCatalog, role, roleIndex);

		ImGui::Separator();
		ImGui::PopID();
	}

	if (Util::Button(T("menu.settings.refresh_font_families", "Refresh Font Families"))) {
		refreshFontCatalog(true);
		MenuFonts::GetAtlasState().pendingFontReload = true;
	}
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::TextUnformatted(T("menu.settings.refresh_font_families_tooltip", "Rescan the Fonts directory after adding or removing font files."));
	}
}

void SettingsTabRenderer::RenderStylingSection()
{
	auto& themeSettings = globals::menu->GetSettings().Theme;
	auto& style = themeSettings.Style;

	// One tab per group keeps each page short; the raw ImGui values sit behind their own tabs
	if (Util::BeginPillTabBar("##StylingTabs")) {
		if (Util::BeginPillTabItem(T("menu.settings.styling_tab_general", "General"))) {
			if (ImGui::SliderFloat(T("menu.settings.global_scale", "Global Scale"), &themeSettings.GlobalScale, -1.f, 1.f, "%.2f"))
				ImGui::GetStyle().FontScaleMain = exp2(themeSettings.GlobalScale);
			SeparatorTextWithFont(T("menu.settings.scrollbar_opacity", "Scrollbar Opacity"), Menu::FontRole::Subheading);
			ImGui::SliderFloat(T("menu.settings.track_opacity", "Track Opacity"), &themeSettings.ScrollbarOpacity.Background, 0.0f, 1.0f, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("%s", T("menu.settings.track_opacity_tooltip", "Controls the opacity of the scrollbar track/channel (the background area behind the scrollbar)."));
			ImGui::SliderFloat(T("menu.settings.thumb_opacity", "Thumb Opacity"), &themeSettings.ScrollbarOpacity.Thumb, 0.0f, 1.0f, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("%s", T("menu.settings.thumb_opacity_tooltip", "Controls the opacity of the scrollbar thumb (the draggable part)."));
			ImGui::SliderFloat(T("menu.settings.thumb_hovered_opacity", "Thumb Hovered Opacity"), &themeSettings.ScrollbarOpacity.ThumbHovered, 0.0f, 1.0f, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("%s", T("menu.settings.thumb_hovered_opacity_tooltip", "Controls the opacity of the scrollbar thumb when hovered."));
			ImGui::SliderFloat(T("menu.settings.thumb_active_opacity", "Thumb Active Opacity"), &themeSettings.ScrollbarOpacity.ThumbActive, 0.0f, 1.0f, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("%s", T("menu.settings.thumb_active_opacity_tooltip", "Controls the opacity of the scrollbar thumb when being dragged."));
			SeparatorTextWithFont(T("menu.settings.section_scrollbar", "Scrollbar"), Menu::FontRole::Subheading);
			ImGui::SliderFloat(T("menu.settings.scrollbar_size", "Scrollbar Size"), &style.ScrollbarSize, 1.0f, 20.0f, "%.0f");
			ImGui::SliderFloat(T("menu.settings.grab_min_size", "Grab Min Size"), &style.GrabMinSize, 1.0f, 20.0f, "%.0f");
			Util::EndPillTabItem();
		}
		if (Util::BeginPillTabItem(T("menu.settings.styling_tab_spacing", "Spacing"))) {
			ImGui::SliderFloat2(T("menu.settings.window_padding", "Window Padding"), (float*)&style.WindowPadding, 0.0f, 20.0f, "%.0f");
			ImGui::SliderFloat2(T("menu.settings.frame_padding", "Frame Padding"), (float*)&style.FramePadding, 0.0f, 20.0f, "%.0f");
			ImGui::SliderFloat2(T("menu.settings.item_spacing", "Item Spacing"), (float*)&style.ItemSpacing, 0.0f, 20.0f, "%.0f");
			ImGui::SliderFloat2(T("menu.settings.item_inner_spacing", "Item Inner Spacing"), (float*)&style.ItemInnerSpacing, 0.0f, 20.0f, "%.0f");
			ImGui::SliderFloat(T("menu.settings.indent_spacing", "Indent Spacing"), &style.IndentSpacing, 0.0f, 30.0f, "%.0f");
			SeparatorTextWithFont(T("menu.settings.section_tables", "Tables"), Menu::FontRole::Subheading);
			ImGui::SliderFloat2(T("menu.settings.cell_padding", "Cell Padding"), (float*)&style.CellPadding, 0.0f, 20.0f, "%.0f");
			ImGui::SliderAngle(T("menu.settings.table_angled_headers_angle", "Table Angled Headers Angle"), &style.TableAngledHeadersAngle, -50.0f, +50.0f);
			Util::EndPillTabItem();
		}
		if (Util::BeginPillTabItem(T("menu.settings.styling_tab_borders", "Borders"))) {
			ImGui::SliderFloat(T("menu.settings.window_border_size", "Window Border Size"), &style.WindowBorderSize, 0.0f, 5.0f, "%.0f");
			ImGui::SliderFloat(T("menu.settings.child_border_size", "Child Border Size"), &style.ChildBorderSize, 0.0f, 5.0f, "%.0f");
			ImGui::SliderFloat(T("menu.settings.popup_border_size", "Popup Border Size"), &style.PopupBorderSize, 0.0f, 5.0f, "%.0f");
			ImGui::SliderFloat(T("menu.settings.frame_border_size", "Frame Border Size"), &style.FrameBorderSize, 0.0f, 5.0f, "%.0f");
			ImGui::SliderFloat(T("menu.settings.tab_border_size", "Tab Border Size"), &style.TabBorderSize, 0.0f, 5.0f, "%.0f");
			ImGui::SliderFloat(T("menu.settings.tab_bar_border_size", "Tab Bar Border Size"), &style.TabBarBorderSize, 0.0f, 5.0f, "%.0f");
			Util::EndPillTabItem();
		}
		if (Util::BeginPillTabItem(T("menu.settings.styling_tab_rounding", "Rounding"))) {
			ImGui::SliderFloat(T("menu.settings.window_rounding", "Window Rounding"), &style.WindowRounding, 0.0f, 12.0f, "%.0f");
			ImGui::SliderFloat(T("menu.settings.child_rounding", "Child Rounding"), &style.ChildRounding, 0.0f, 12.0f, "%.0f");
			ImGui::SliderFloat(T("menu.settings.frame_rounding", "Frame Rounding"), &style.FrameRounding, 0.0f, 12.0f, "%.0f");
			ImGui::SliderFloat(T("menu.settings.popup_rounding", "Popup Rounding"), &style.PopupRounding, 0.0f, 12.0f, "%.0f");
			ImGui::SliderFloat(T("menu.settings.scrollbar_rounding", "Scrollbar Rounding"), &style.ScrollbarRounding, 0.0f, 12.0f, "%.0f");
			ImGui::SliderFloat(T("menu.settings.grab_rounding", "Grab Rounding"), &style.GrabRounding, 0.0f, 12.0f, "%.0f");
			ImGui::SliderFloat(T("menu.settings.tab_rounding", "Tab Rounding"), &style.TabRounding, 0.0f, 12.0f, "%.0f");
			Util::EndPillTabItem();
		}
		if (Util::BeginPillTabItem(T("menu.settings.styling_tab_widgets", "Widgets"))) {
			{
				FontRoleGuard comboFont(Menu::FontRole::Body);
				const char* colorButtonPositions[] = {
					T("menu.settings.color_button_left", "Left"),
					T("menu.settings.color_button_right", "Right")
				};
				int colorButtonPos = (int)style.ColorButtonPosition;
				if (ImGui::Combo(T("menu.settings.color_button_position", "ColorButtonPosition"), &colorButtonPos, colorButtonPositions, IM_ARRAYSIZE(colorButtonPositions))) {
					style.ColorButtonPosition = static_cast<ImGuiDir>(colorButtonPos);
				}
			}
			ImGui::SliderFloat2(T("menu.settings.button_text_align", "Button Text Align"), (float*)&style.ButtonTextAlign, 0.0f, 1.0f, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("%s", T("menu.settings.button_text_align_tooltip", "Alignment applies when a button is larger than its text content."));
			ImGui::SliderFloat2(T("menu.settings.selectable_text_align", "Selectable Text Align"), (float*)&style.SelectableTextAlign, 0.0f, 1.0f, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("%s", T("menu.settings.selectable_text_align_tooltip", "Alignment applies when a selectable is larger than its text content."));
			ImGui::SliderFloat(T("menu.settings.separator_text_border_size", "Separator Text Border Size"), &style.SeparatorTextBorderSize, 0.0f, 10.0f, "%.0f");
			ImGui::SliderFloat2(T("menu.settings.separator_text_align", "Separator Text Align"), (float*)&style.SeparatorTextAlign, 0.0f, 1.0f, "%.2f");
			ImGui::SliderFloat2(T("menu.settings.separator_text_padding", "Separator Text Padding"), (float*)&style.SeparatorTextPadding, 0.0f, 40.0f, "%.0f");
			ImGui::SliderFloat(T("menu.settings.log_slider_deadzone", "Log Slider Deadzone"), &style.LogSliderDeadzone, 0.0f, 12.0f, "%.0f");
			SeparatorTextWithFont(T("menu.settings.section_docking", "Docking"), Menu::FontRole::Subheading);
			ImGui::SliderFloat(T("menu.settings.docking_splitter_size", "Docking Splitter Size"), &style.DockingSeparatorSize, 0.0f, 12.0f, "%.0f");
			Util::EndPillTabItem();
		}
		if (Util::BeginPillTabItem(T("menu.settings.tab_surfaces", "Surfaces"))) {
			RenderSurfacesSection();
			Util::EndPillTabItem();
		}
		Util::EndPillTabBar();
	}
}

namespace
{
	using SurfaceRole = Util::Surface::Role;

	const char* GetSurfaceRoleTitle(SurfaceRole role)
	{
		switch (role) {
		case SurfaceRole::HomeCard:
			return T("menu.settings.surface_home_card", "Home Cards");
		case SurfaceRole::Toast:
			return T("menu.settings.surface_toast", "Notification Toasts");
		case SurfaceRole::OverlayCard:
			return T("menu.settings.surface_overlay_card", "Status Card");
		case SurfaceRole::Palette:
			return T("menu.settings.surface_palette", "Go To Palette");
		case SurfaceRole::NavSelected:
			return T("menu.settings.surface_nav_selected", "Selected Navigation Row");
		default:
			return "";
		}
	}

	const char* GetSurfaceRoleHint(SurfaceRole role)
	{
		switch (role) {
		case SurfaceRole::HomeCard:
			return T("menu.settings.surface_home_card_tooltip", "The panels on the Home page. When disabled they render as plain sections; the title line and padding stay.");
		case SurfaceRole::Toast:
			return T("menu.settings.surface_toast_tooltip", "The notification cards that appear in the corner of the screen.");
		case SurfaceRole::OverlayCard:
			return T("menu.settings.surface_overlay_card_tooltip", "The card that shows shader compilation progress.");
		case SurfaceRole::Palette:
			return T("menu.settings.surface_palette_tooltip", "The Ctrl+K go-to palette.");
		case SurfaceRole::NavSelected:
			return T("menu.settings.surface_nav_selected_tooltip", "The highlight behind the selected row of the left navigation.");
		default:
			return "";
		}
	}

	/** @brief Draws a small sample of a surface role, so edits are visible next to the controls. */
	void DrawSurfacePreview(SurfaceRole role, const Util::Surface::RoleSettings& s)
	{
		const float scale = Util::GetUIScale();
		const float margin = 14.0f * scale;
		const float height = ImGui::GetTextLineHeightWithSpacing() * 2.0f;
		const float width = std::min(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 18.0f);
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		ImGui::Dummy(ImVec2(width, height + margin * 2.0f));
		const ImVec2 p0(origin.x + margin, origin.y + margin);
		const ImVec2 p1(origin.x + width - margin, origin.y + margin + height);
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		if (role == SurfaceRole::NavSelected) {
			if (s.enabled) {
				const float rounding = ImGui::GetStyle().FrameRounding;
				ImVec4 fill = Util::Colors::GetAccent();
				fill.w = s.fillAlpha;
				drawList->AddRectFilled(p0, p1, ImGui::GetColorU32(fill), rounding);
				const float barWidth = s.accentBarWidth * scale;
				if (s.accentBar && barWidth > 0.0f) {
					const float inset = (p1.y - p0.y) * ThemeManager::Constants::NAV_SELECTED_BAR_INSET;
					drawList->AddRectFilled(ImVec2(p0.x, p0.y + inset), ImVec2(p0.x + barWidth, p1.y - inset),
						ImGui::GetColorU32(Util::Colors::GetAccent()), barWidth * 0.5f);
				}
			}
		} else {
			Util::Surface::Options options;
			options.rounding = role == SurfaceRole::HomeCard ? ImGui::GetStyle().ChildRounding : ImGui::GetStyle().WindowRounding;
			Util::Surface::Draw(drawList, p0, p1, role, options);
		}
		const char* label = s.enabled ? T("menu.settings.surface_preview", "Preview") : T("menu.settings.surface_preview_disabled", "Disabled");
		const ImVec2 textSize = ImGui::CalcTextSize(label);
		drawList->AddText(ImVec2(p0.x + (p1.x - p0.x - textSize.x) * 0.5f, p0.y + (p1.y - p0.y - textSize.y) * 0.5f),
			ImGui::GetColorU32(s.enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled), label);
	}

	/** @brief Slider with a hover tooltip; returns true when the value changed. */
	bool SurfaceSlider(const char* label, const char* tooltip, float* value, float lo, float hi, const char* format)
	{
		const bool changed = ImGui::SliderFloat(label, value, lo, hi, format);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextUnformatted(tooltip);
		return changed;
	}

	/** @brief Checkbox with a hover tooltip; returns true when the value changed. */
	bool SurfaceCheckbox(const char* label, const char* tooltip, bool* value)
	{
		const bool changed = ImGui::Checkbox(label, value);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextUnformatted(tooltip);
		return changed;
	}

	/** @brief The controls of one surface role. */
	void DrawSurfaceRoleControls(SurfaceRole role, Util::Surface::RoleSettings& s)
	{
		SurfaceCheckbox(T("menu.settings.surface_enabled", "Enabled"),
			T("menu.settings.surface_enabled_tooltip", "Draw this surface. When off, the content still shows, just without the backing."), &s.enabled);
		DrawSurfacePreview(role, s);

		Util::DisableGuard disabled(!s.enabled);

		if (role == SurfaceRole::NavSelected) {
			SurfaceSlider(T("menu.settings.surface_nav_fill_opacity", "Fill Opacity"),
				T("menu.settings.surface_nav_fill_opacity_tooltip", "Opacity of the accent colour fill behind the selected row."), &s.fillAlpha, 0.0f, 1.0f, "%.2f");
			SurfaceCheckbox(T("menu.settings.surface_accent_bar", "Accent Bar"),
				T("menu.settings.surface_accent_bar_tooltip", "Draw a short accent bar on the left edge of the selected row."), &s.accentBar);
			{
				Util::DisableGuard barDisabled(!s.accentBar);
				SurfaceSlider(T("menu.settings.surface_accent_bar_width", "Accent Bar Width"),
					T("menu.settings.surface_accent_bar_width_tooltip", "Width of the accent bar in pixels at 1080p."), &s.accentBarWidth, 0.0f, 12.0f, "%.1f");
			}
			return;
		}

		if (role == SurfaceRole::HomeCard) {
			SurfaceSlider(T("menu.settings.surface_padding", "Inner Padding"),
				T("menu.settings.surface_padding_tooltip", "Space between the card edge and its content, in pixels at 1080p. Applies even when the surface is disabled."), &s.padding, 0.0f, 48.0f, "%.0f");
		}

		SeparatorTextWithFont(T("menu.settings.surface_fill", "Fill"), Menu::FontRole::Subheading);
		SurfaceSlider(T("menu.settings.surface_fill_lift", "Fill Lift"),
			T("menu.settings.surface_fill_lift_tooltip", "How far the window colour is lightened toward white to make the surface read above the page."), &s.fillLift, 0.0f, 0.5f, "%.3f");
		SurfaceSlider(T("menu.settings.surface_fill_opacity", "Fill Opacity"),
			T("menu.settings.surface_fill_opacity_tooltip", "Multiplier on the fill opacity."), &s.fillAlpha, 0.0f, 1.0f, "%.2f");

		bool useStyleRounding = s.rounding < 0.0f;
		if (SurfaceCheckbox(T("menu.settings.surface_use_style_rounding", "Use Style Rounding"),
				T("menu.settings.surface_use_style_rounding_tooltip", "Follow the corner rounding from the Styling section instead of a custom radius."), &useStyleRounding))
			s.rounding = useStyleRounding ? -1.0f : std::clamp(ImGui::GetStyle().ChildRounding, 0.0f, 24.0f);
		if (!useStyleRounding)
			SurfaceSlider(T("menu.settings.surface_rounding", "Corner Radius"),
				T("menu.settings.surface_rounding_tooltip", "Corner radius in pixels at 1080p."), &s.rounding, 0.0f, 24.0f, "%.0f");

		SeparatorTextWithFont(T("menu.settings.surface_border", "Border"), Menu::FontRole::Subheading);
		SurfaceCheckbox(T("menu.settings.surface_border_enabled", "Border"),
			T("menu.settings.surface_border_enabled_tooltip", "Draw a hairline border around the surface."), &s.border);
		{
			Util::DisableGuard borderDisabled(!s.border);
			SurfaceSlider(T("menu.settings.surface_border_opacity", "Border Opacity"),
				T("menu.settings.surface_border_opacity_tooltip", "Opacity of the default white hairline."), &s.borderAlpha, 0.0f, 1.0f, "%.2f");
			SurfaceCheckbox(T("menu.settings.surface_border_use_color", "Custom Border Colour"),
				T("menu.settings.surface_border_use_color_tooltip", "Use your own border colour instead of the white hairline. Cards that signal a status keep their status colour."), &s.useBorderColor);
			if (s.useBorderColor) {
				ImGui::ColorEdit4(T("menu.settings.surface_border_color", "Border Colour"), reinterpret_cast<float*>(&s.borderColor));
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::TextUnformatted(T("menu.settings.surface_border_color_tooltip", "Colour and opacity of the border."));
			}
		}

		SeparatorTextWithFont(T("menu.settings.surface_shadow", "Shadow"), Menu::FontRole::Subheading);
		SurfaceCheckbox(T("menu.settings.surface_shadow_enabled", "Shadow"),
			T("menu.settings.surface_shadow_enabled_tooltip", "Draw a soft drop shadow behind the surface."), &s.shadow);
		{
			Util::DisableGuard shadowDisabled(!s.shadow);
			SurfaceSlider(T("menu.settings.surface_shadow_spread", "Shadow Spread"),
				T("menu.settings.surface_shadow_spread_tooltip", "How far the shadow grows past the surface edge, in pixels at 1080p."), &s.shadowSpread, 0.0f, 32.0f, "%.0f");
			SurfaceSlider(T("menu.settings.surface_shadow_opacity", "Shadow Opacity"),
				T("menu.settings.surface_shadow_opacity_tooltip", "Opacity of the black shadow."), &s.shadowAlpha, 0.0f, 1.0f, "%.2f");
			SurfaceSlider(T("menu.settings.surface_shadow_offset", "Shadow Drop"),
				T("menu.settings.surface_shadow_offset_tooltip", "Vertical shadow offset in pixels at 1080p. Positive values drop it below the surface."), &s.shadowOffsetY, -16.0f, 16.0f, "%.0f");
		}
	}
}

void SettingsTabRenderer::RenderSurfacesSection()
{
	auto& surfaces = globals::menu->GetSettings().Theme.Surfaces;

	ImGui::TextWrapped("%s", T("menu.settings.surfaces_intro", "Backing, border and shadow of the menu cards and highlights. Changes apply immediately and are saved with the theme."));
	if (Util::Button(T("menu.settings.reset_surfaces", "Reset surfaces to theme defaults")))
		surfaces = Util::Surface::Settings{};
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::TextUnformatted(T("menu.settings.reset_surfaces_tooltip", "Restores every surface below to its built-in look. Save the theme to keep it."));

	for (size_t i = 0; i < static_cast<size_t>(SurfaceRole::Count); ++i) {
		const auto role = static_cast<SurfaceRole>(i);
		ImGui::PushID(Util::Surface::RoleKey(role));
		const auto label = std::format("{}###Surface{}", GetSurfaceRoleTitle(role), Util::Surface::RoleKey(role));
		const bool open = ImGui::CollapsingHeader(label.c_str(), role == SurfaceRole::HomeCard ? ImGuiTreeNodeFlags_DefaultOpen : 0);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextUnformatted(GetSurfaceRoleHint(role));
		if (open)
			DrawSurfaceRoleControls(role, surfaces.roles[i]);
		ImGui::PopID();
	}
	surfaces.Sanitize();
}

void SettingsTabRenderer::RenderColorsSection()
{
	auto& themeSettings = globals::menu->GetSettings().Theme;
	auto& colors = themeSettings.FullPalette;

	// Color filter at the top with search icon
	static ImGuiTextFilter colorFilter;

	const float scale = Util::GetSearchUIScale();
	const float iconSize = ThemeManager::Constants::SEARCH_ICON_SIZE * scale;
	const float iconSpace = iconSize + ThemeManager::Constants::SEARCH_INPUT_PADDING_EXTRA * scale;
	float availableWidth = ImGui::GetFontSize() * 16;

	// Custom style for filter with icon space
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(iconSpace, ThemeManager::Constants::SEARCH_INPUT_FRAME_PADDING_Y * scale));
	colorFilter.Draw(T("menu.settings.filter_colors", "Filter colors"), availableWidth);
	ImGui::PopStyleVar();

	// Draw search icon
	const ImVec2 filterMin = ImGui::GetItemRectMin();
	const ImVec2 filterSize = ImGui::GetItemRectSize();
	ImVec4 iconColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
	iconColor.w *= ThemeManager::Constants::SEARCH_ICON_ALPHA;
	Icons::DrawCenteredGlyph(ImGui::GetWindowDrawList(), ImVec2(filterMin.x + ThemeManager::Constants::SEARCH_ICON_OFFSET_X * scale, filterMin.y),
		ImVec2(iconSize, filterSize.y), Icons::TI(MenuIcons::kSearch), ImGui::GetColorU32(iconColor));

	ImGui::Spacing();

	// Background & Text
	if (colorFilter.PassFilter("Background"))
		ImGui::ColorEdit4(T("menu.settings.color_background", "Background"), (float*)&themeSettings.Palette.Background);
	if (colorFilter.PassFilter("Text"))
		ImGui::ColorEdit4(T("menu.settings.color_text", "Text"), (float*)&themeSettings.Palette.Text);

	if (ImGui::TreeNodeEx(T("menu.settings.borders_and_separators", "Borders & Separators"), ImGuiTreeNodeFlags_DefaultOpen)) {
		if (colorFilter.PassFilter("Window Border"))
			ImGui::ColorEdit4(T("menu.settings.color_window_border", "Window Border"), (float*)&themeSettings.Palette.WindowBorder);
		if (colorFilter.PassFilter("Slider & Input Background"))
			ImGui::ColorEdit4(T("menu.settings.color_slider_input_bg", "Slider & Input Background"), (float*)&themeSettings.Palette.FrameBorder);
		if (colorFilter.PassFilter("Separator Line"))
			ImGui::ColorEdit4(T("menu.settings.color_separator_line", "Separator Line"), (float*)&themeSettings.Palette.Separator);
		if (colorFilter.PassFilter("Resize Grip"))
			ImGui::ColorEdit4(T("menu.settings.color_resize_grip", "Resize Grip"), (float*)&themeSettings.Palette.ResizeGrip);
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx(T("menu.settings.feature_headings", "Feature Headings"), ImGuiTreeNodeFlags_DefaultOpen)) {
		if (colorFilter.PassFilter("Default"))
			ImGui::ColorEdit4(T("menu.settings.color_default", "Default"), (float*)&themeSettings.FeatureHeading.ColorDefault);
		if (colorFilter.PassFilter("Hovered"))
			ImGui::ColorEdit4(T("menu.settings.color_hovered", "Hovered"), (float*)&themeSettings.FeatureHeading.ColorHovered);
		if (colorFilter.PassFilter("Minimized Transparency"))
			ImGui::SliderFloat(T("menu.settings.color_minimized_transparency", "Minimized Transparency"), &themeSettings.FeatureHeading.MinimizedFactor, 0.0f, 1.0f, "%.2f");
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx(T("menu.settings.status", "Status"), ImGuiTreeNodeFlags_DefaultOpen)) {
		if (colorFilter.PassFilter("Disabled"))
			ImGui::ColorEdit4(T("menu.settings.color_disabled", "Disabled"), (float*)&themeSettings.StatusPalette.Disable);
		if (colorFilter.PassFilter("Error"))
			ImGui::ColorEdit4(T("menu.settings.color_error", "Error"), (float*)&themeSettings.StatusPalette.Error);
		if (colorFilter.PassFilter("Warning"))
			ImGui::ColorEdit4(T("menu.settings.color_warning", "Warning"), (float*)&themeSettings.StatusPalette.Warning);
		if (colorFilter.PassFilter("Restart Needed"))
			ImGui::ColorEdit4(T("menu.settings.color_restart_needed", "Restart Needed"), (float*)&themeSettings.StatusPalette.RestartNeeded);
		if (colorFilter.PassFilter("Current Hotkey"))
			ImGui::ColorEdit4(T("menu.settings.color_current_hotkey", "Current Hotkey"), (float*)&themeSettings.StatusPalette.CurrentHotkey);
		if (colorFilter.PassFilter("Success"))
			ImGui::ColorEdit4(T("menu.settings.color_success", "Success"), (float*)&themeSettings.StatusPalette.SuccessColor);
		if (colorFilter.PassFilter("Info"))
			ImGui::ColorEdit4(T("menu.settings.color_info", "Info"), (float*)&themeSettings.StatusPalette.InfoColor);
		ImGui::TreePop();
	}

	if (ImGui::TreeNode(T("menu.settings.full_palette", "Full Palette"))) {
		ImGui::TextWrapped("%s", T("menu.settings.full_palette_tooltip", "Advanced color controls for detailed customization of all UI elements."));

		for (int i = 0; i < ImGuiCol_COUNT; i++) {
			const char* friendlyName = GetFriendlyColorName(i);
			if (!colorFilter.PassFilter(friendlyName))
				continue;
			ImGui::ColorEdit4(friendlyName, (float*)&colors[i], ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
		}
		ImGui::TreePop();
	}
}
