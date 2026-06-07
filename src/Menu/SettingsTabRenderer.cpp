#include "SettingsTabRenderer.h"

#include <format>
#include <set>
#include <string>
#include <windows.h>

#include "BackgroundBlur.h"
#include "Features/ScreenshotFeature.h"
#include "Fonts.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "CursorLoader.h"
#include "IconLoader.h"
#include "Menu.h"
#include "ShaderCache.h"
#include "State.h"
#include "ThemeManager.h"
#include "Util.h"

using json = nlohmann::json;

namespace
{
	using FontRoleGuard = MenuFonts::FontRoleGuard;  // Convenience alias

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

	bool BeginTabItemWithFont(const char* label, Menu::FontRole role, ImGuiTabItemFlags flags = ImGuiTabItemFlags_None)
	{
		return MenuFonts::BeginTabItemWithFont(label, role, flags);
	}

	bool ComboWithFont(const char* label, int* currentItem, const char* const items[], int itemCount, Menu::FontRole role)
	{
		FontRoleGuard guard(role);
		return ImGui::Combo(label, currentItem, items, itemCount);
	}

	bool IsPresetThemeSelected()
	{
		std::string selected = globals::menu->GetSettings().SelectedThemePreset;
		return !selected.empty() && ThemeManager::GetSingleton()->IsPresetTheme(selected);
	}

	void RenderSaveInfoText()
	{
		auto& ts = globals::menu->GetSettings().Theme;
		ImGui::PushStyleColor(ImGuiCol_Text, ts.StatusPalette.InfoColor);
		ImGui::TextWrapped("%s", T("menu.settings.theme_save_info",
									 "Theme changes are not saved with the global \"Save Settings\" button. Use the Themes tab to save changes to this theme."));
		ImGui::PopStyleColor();
		ImGui::Spacing();
	}
}

void SettingsTabRenderer::RenderGeneralSettings(SettingsState& state)
{
	MenuFonts::TabBarPaddingGuard tabPaddingGuard(Menu::FontRole::Heading);
	if (ImGui::BeginTabBar("##GeneralTabBar", ImGuiTabBarFlags_None)) {
		RenderShadersTab();
		RenderKeybindingsTab(state);
		RenderInterfaceTab();
		ImGui::EndTabBar();
	}
}

void SettingsTabRenderer::RenderShadersTab()
{
	auto tabLabel = std::format("{}##{}", T("menu.settings.tab_shaders", "Shaders"), "GeneralShadersTab");
	if (BeginTabItemWithFont(tabLabel.c_str(), Menu::FontRole::Heading)) {
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

			// Stacked bar showing compilation breakdown
			{
				uint64_t total = shaderCache->GetTotalTasks();
				uint64_t completed = shaderCache->GetCompletedTasks();
				uint64_t failed = shaderCache->GetFailedTasks();
				uint64_t cacheHits = shaderCache->GetCachedHitTasks();
				uint64_t diskHits = shaderCache->GetDiskHitTasks();
				uint64_t slow = shaderCache->GetSlowTasks();
				uint64_t verySlow = shaderCache->GetVerySlowTasks();
				// Compiled = tasks that actually went through compilation (excluding disk hits).
				// Cache hits are separate (returned early without queueing).
				uint64_t compiled = completed > diskHits ? completed - diskHits : 0;
				uint64_t fast = compiled > slow ? compiled - slow : 0;
				uint64_t medium = slow > verySlow ? slow - verySlow : 0;  // 2-8s

				struct Segment
				{
					uint64_t count;
					ImU32 color;
					const char* label;
				};
				Segment segments[] = {
					{ cacheHits, IM_COL32(120, 120, 120, 255), T("menu.settings.shader_deduplicated", "Deduplicated") },
					{ diskHits, IM_COL32(70, 130, 200, 255), T("menu.settings.shader_disk_cache", "Disk cache") },
					{ fast, IM_COL32(80, 180, 80, 255), T("menu.settings.shader_fast", "Fast (<2s)") },
					{ medium, IM_COL32(220, 180, 50, 255), T("menu.settings.shader_slow", "Slow (2-8s)") },
					{ verySlow, IM_COL32(220, 60, 60, 255), T("menu.settings.shader_very_slow", "Very slow (>=8s)") },
					{ failed, IM_COL32(160, 30, 30, 255), T("menu.settings.shader_failed", "Failed") },
				};

				float barHeight = 14.0f * Util::GetUIScale();
				float barWidth = ImGui::GetContentRegionAvail().x;
				ImVec2 cursor = ImGui::GetCursorScreenPos();
				ImDrawList* drawList = ImGui::GetWindowDrawList();

				// Background
				drawList->AddRectFilled(cursor, ImVec2(cursor.x + barWidth, cursor.y + barHeight), IM_COL32(40, 40, 40, 255));

				// Draw segments
				float x = cursor.x;
				for (auto& seg : segments) {
					if (seg.count == 0 || total == 0)
						continue;
					float segWidth = (static_cast<float>(seg.count) / static_cast<float>(total)) * barWidth;
					if (segWidth < 1.0f)
						segWidth = 1.0f;
					drawList->AddRectFilled(ImVec2(x, cursor.y), ImVec2(x + segWidth, cursor.y + barHeight), seg.color);
					x += segWidth;
				}

				// Reserve space and handle tooltip
				ImGui::Dummy(ImVec2(barWidth, barHeight));
				if (ImGui::IsItemHovered()) {
					ImGui::BeginTooltip();
					for (auto& seg : segments) {
						if (seg.count == 0)
							continue;
						float pct = total > 0 ? 100.0f * static_cast<float>(seg.count) / static_cast<float>(total) : 0.0f;
						ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(seg.color), "%s: %llu (%.1f%%)", seg.label, seg.count, pct);
					}
					ImGui::EndTooltip();
				}
			}

			auto state = globals::state;
			if (state->IsDeveloperMode()) {
				ImGui::Text("Threads: %d compile, %d background, %d pool | P-cores: %d",
					(int)shaderCache->compilationThreadCount,
					(int)shaderCache->backgroundCompilationThreadCount,
					(int)shaderCache->compilationPool.get_thread_count(),
					(int)Util::GetPerformanceCoreCount());
			}
		}

		ImGui::EndTabItem();
	}
}

void SettingsTabRenderer::RenderKeybindingsTab(
	SettingsState& state)
{
	auto tabLabel = std::format("{}##{}", T("menu.settings.tab_keybindings", "Keybindings"), "GeneralKeybindingsTab");
	if (BeginTabItemWithFont(tabLabel.c_str(), Menu::FontRole::Heading)) {
		auto& settings = globals::menu->GetSettings();

		Util::InputComboWidget(
			T("menu.settings.toggle_key", "Toggle Key:"),
			settings.ToggleKey,
			state.settingToggleKey,
			"Change##toggle");

		Util::InputComboWidget(
			T("menu.settings.effect_toggle_key", "Effect Toggle Key:"),
			settings.EffectToggleKey,
			state.settingsEffectsToggle,
			"Change##EffectToggle");

		Util::InputComboWidget(
			T("menu.settings.skip_compilation_key", "Skip Compilation Key:"),
			settings.SkipCompilationKey,
			state.settingSkipCompilationKey,
			"Change##skip");

		Util::InputComboWidget(
			T("menu.settings.overlay_toggle_key", "Overlay Toggle Key:"),
			settings.OverlayToggleKey,
			state.settingOverlayToggleKey,
			"Change##OverlayToggle");

		Util::InputComboWidget(
			T("menu.settings.cs_editor_toggle_key", "CS Editor Toggle Key:"),
			settings.CSEditorToggleKey,
			state.settingCSEditorToggleKey,
			"Change##CSEditorToggle");

		Util::InputComboWidget(
			T("menu.settings.screenshot_key", "Screenshot Key:"),
			settings.ScreenshotKey,
			state.settingScreenshotKey,
			"Change##Screenshot");

		ImGui::EndTabItem();
	}
}

void SettingsTabRenderer::RenderInterfaceTab()
{
	auto tabLabel = std::format("{}##{}", T("menu.settings.tab_interface", "Interface"), "GeneralInterfaceTab");
	if (BeginTabItemWithFont(tabLabel.c_str(), Menu::FontRole::Heading)) {
		MenuFonts::TabBarPaddingGuard tabPaddingGuard(Menu::FontRole::Subheading);
		if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_None)) {
			RenderBehaviorTab();
			RenderThemesTab();
			RenderFontsTab();
			RenderStylingTab();
			RenderColorsTab();
			ImGui::EndTabBar();
		}
		ImGui::EndTabItem();
	}
}

void SettingsTabRenderer::RenderBehaviorTab()
{
	auto tabLabel = std::format("{}##{}", T("menu.settings.tab_behavior", "Behavior"), "InterfaceBehaviorTab");
	if (BeginTabItemWithFont(tabLabel.c_str(), Menu::FontRole::Heading)) {
		auto& themeSettings = globals::menu->GetSettings().Theme;
		RenderSaveInfoText();

		SeparatorTextWithFont(T("menu.settings.section_language", "Language"), Menu::FontRole::Subheading);

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
					if (ImGui::Selectable(displayName.c_str(), isSelected)) {
						i18n->SetLocale(code);
						globals::menu->pendingFontReload = true;
					}
					if (isSelected) {
						ImGui::SetItemDefaultFocus();
					}
					ImGui::PopID();
				}
				ImGui::EndCombo();
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("%s", T("menu.settings.language_tooltip", "Select the display language for the Community Shaders interface."));
			}
		}

		SeparatorTextWithFont(T("menu.settings.ui_behavior", "UI Behavior"), Menu::FontRole::Subheading);

		ImGui::Checkbox(T("menu.settings.show_icon_buttons_in_header", "Show Icon Buttons in Header"), &themeSettings.ShowActionIcons);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T("menu.settings.show_icon_buttons_in_header_tooltip",
								  "When enabled: Shows action buttons (Save, Load, Clear Cache) as icons in the header\n"
								  "When disabled: Shows as text buttons below the header"));
		}

		if (themeSettings.ShowActionIcons) {
			ImGui::Indent();
			if (ImGui::Checkbox(T("menu.settings.use_monochrome_icons", "Use Monochrome Icons"), &themeSettings.UseMonochromeIcons)) {
				globals::menu->pendingIconReload = true;
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("%s", T("menu.settings.use_monochrome_icons_tooltip", "Uses white monochrome icons that adapt to your theme's text color"));
			}
			ImGui::SameLine();
			if (ImGui::Checkbox(T("menu.settings.use_monochrome_cs_logo", "Use Monochrome CS Logo"), &themeSettings.UseMonochromeLogo)) {
				globals::menu->pendingIconReload = true;
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("%s", T("menu.settings.use_monochrome_cs_logo_tooltip", "Uses monochrome version of the Community Shaders logo"));
			}
			ImGui::Unindent();
		}

		ImGui::Checkbox(T("menu.settings.show_footer", "Show Footer"), &themeSettings.ShowFooter);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T("menu.settings.show_footer_tooltip", "Shows the footer with game version, swap chain, and GPU information at the bottom of the window"));
		}

		ImGui::Checkbox(T("menu.settings.center_header_title", "Center Header Title"), &themeSettings.CenterHeader);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T("menu.settings.center_header_title_tooltip", "Centers the Community Shaders title and logo in the header title bar"));
		}

		ImGui::Checkbox(T("menu.settings.auto_hide_feature_list", "Auto-hide Feature List"), &globals::menu->GetSettings().AutoHideFeatureList);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T("menu.settings.auto_hide_feature_list_tooltip", "Automatically hides the left feature list panel. Move cursor to the left edge to show it."));
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

		SeparatorTextWithFont(T("menu.settings.visual_effects", "Visual Effects"), Menu::FontRole::Subheading);

		if (ImGui::Checkbox(T("menu.settings.background_blur", "Background Blur"), &themeSettings.BackgroundBlurEnabled)) {
			BackgroundBlur::SetEnabled(themeSettings.BackgroundBlurEnabled);
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T("menu.settings.background_blur_tooltip", "Applies a blur effect to the background behind the menu window."));
		}

		ImGui::EndTabItem();
	}
}

void SettingsTabRenderer::RenderThemesTab()
{
	auto tabLabel = std::format("{}##{}", T("menu.settings.tab_themes", "Themes"), "InterfaceThemesTab");
	if (BeginTabItemWithFont(tabLabel.c_str(), Menu::FontRole::Heading)) {
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

		if (ImGui::Button(T("menu.settings.refresh", "Refresh"))) {
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
		if (ImGui::Button(T("menu.settings.open_themes_folder", "Open Themes Folder"))) {
			std::filesystem::path themesPath = Util::PathHelpers::GetThemesRealPath();
			ShellExecuteA(NULL, "open", themesPath.string().c_str(), NULL, NULL, SW_SHOWNORMAL);
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T("menu.settings.open_themes_folder_tooltip", "Opens the Themes folder where you can add custom theme files."));
		}

		ImGui::Spacing();
		ImGui::PushStyleColor(ImGuiCol_Text, themeSettings.StatusPalette.InfoColor);
		ImGui::TextWrapped("%s", T("menu.settings.theme_save_reminder", "If you changed the theme above, save your selection using the global \"Save Settings\" button."));
		ImGui::PopStyleColor();

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
			if (Util::ButtonWithFlash(T("menu.settings.save_theme_button", "Save"))) {
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
			if (ImGui::Button(T("menu.settings.cancel", "Cancel"))) {
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

		ImGui::EndTabItem();
	}
}

void SettingsTabRenderer::RenderFontsTab()
{
	auto tabLabel = std::format("{}##{}", T("menu.settings.tab_fonts", "Fonts"), "InterfaceFontsTab");
	if (BeginTabItemWithFont(tabLabel.c_str(), Menu::FontRole::Heading)) {
		auto* menuInstance = globals::menu;
		auto& themeSettings = menuInstance->GetSettings().Theme;
		RenderSaveInfoText();

		SeparatorTextWithFont(T("menu.settings.font", "Font"), Menu::FontRole::Subheading);

		bool& useAutoFont = menuInstance->GetSettings().UseResolutionFont;
		if (ImGui::Checkbox(T("menu.settings.use_resolution_based_font_size", "Use resolution-based font size"), &useAutoFont)) {
			if (!useAutoFont) {
				// Seed the fixed-size slider with the current effective size so it doesn't jump
				float effective = ThemeManager::ResolveFontSize(*menuInstance);
				themeSettings.FontSize = std::clamp(effective, ThemeManager::Constants::MIN_FONT_SIZE, ThemeManager::Constants::MAX_FONT_SIZE);
			}
			menuInstance->pendingFontReload = true;
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T("menu.settings.use_resolution_based_font_size_tooltip", "When enabled, the UI font size scales with your screen resolution. Disable to set a fixed size."));
		}

		ImGui::BeginDisabled(useAutoFont);
		if (ImGui::SliderFloat(T("menu.settings.base_font_size", "Base Font Size"), &themeSettings.FontSize, ThemeManager::Constants::MIN_FONT_SIZE, ThemeManager::Constants::MAX_FONT_SIZE, "%.0f")) {
			menuInstance->pendingFontReload = true;
		}
		ImGui::EndDisabled();

		float effectiveNow = ThemeManager::ResolveFontSize(*menuInstance);
		ImGui::Text(T("menu.settings.effective_size", "Effective size: %.0f px"), std::round(effectiveNow));

		static Util::Fonts::Catalog fontCatalog;
		static bool catalogInitialized = false;
		auto refreshFontCatalog = [&]() {
			fontCatalog = Util::Fonts::DiscoverFontCatalog();
		};

		if (!catalogInitialized) {
			refreshFontCatalog();
			catalogInitialized = true;
		}

		ImGui::Spacing();
		SeparatorTextWithFont(T("menu.settings.font_roles", "Font Roles"), Menu::FontRole::Subheading);

		if (fontCatalog.families.empty()) {
			ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "%s", T("menu.settings.no_fonts_found", "No fonts found. Place .ttf files in Interface/CommunityShaders/Fonts/"));
		}

		for (size_t roleIndex = 0; roleIndex < Menu::FontRoleDescriptors.size(); ++roleIndex) {
			auto role = static_cast<Menu::FontRole>(roleIndex);
			auto descriptor = Menu::FontRoleDescriptors[roleIndex];
			auto& roleSettings = themeSettings.FontRoles[roleIndex];

			ImGui::PushID(static_cast<int>(roleIndex));
			{
				FontRoleGuard headingFont(Menu::FontRole::Subheading);
				ImGui::TextUnformatted(descriptor.displayName.data());
			}

			int familyIndex = 0;
			if (!fontCatalog.families.empty()) {
				for (size_t i = 0; i < fontCatalog.families.size(); ++i) {
					if (Util::IEquals(fontCatalog.families[i].name, roleSettings.Family)) {
						familyIndex = static_cast<int>(i);
						break;
					}
				}
				if (familyIndex >= static_cast<int>(fontCatalog.families.size())) {
					familyIndex = 0;
				}
			}

			const char* familyPreview = fontCatalog.families.empty() ? T("menu.settings.no_families", "No families") : fontCatalog.families[familyIndex].displayName.c_str();
			std::string familyLabel = std::format("{} Family##{}", descriptor.displayName, roleIndex);
			{
				FontRoleGuard familyComboFont(Menu::FontRole::Body);
				if (ImGui::BeginCombo(familyLabel.c_str(), familyPreview)) {
					if (fontCatalog.families.empty()) {
						ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", T("menu.settings.no_font_families_available", "No font families available"));
					} else {
						for (int i = 0; i < static_cast<int>(fontCatalog.families.size()); ++i) {
							bool isSelected = (i == familyIndex);
							if (ImGui::Selectable(fontCatalog.families[i].displayName.c_str(), isSelected)) {
								familyIndex = i;
								if (!isSelected) {
									const auto& newFamily = fontCatalog.families[i];
									roleSettings.Family = newFamily.name;
									if (!newFamily.styles.empty()) {
										const auto& firstStyle = newFamily.styles.front();
										roleSettings.Style = firstStyle.style;
										roleSettings.File = firstStyle.file;
									} else {
										roleSettings.Style.clear();
										roleSettings.File.clear();
									}
									if (role == Menu::FontRole::Body) {
										themeSettings.FontName = roleSettings.File;
									}
									menuInstance->pendingFontReload = true;
								}
							}
							if (isSelected) {
								ImGui::SetItemDefaultFocus();
							}
						}
					}
					ImGui::EndCombo();
				}
			}

			const Util::Fonts::FamilyInfo* selectedFamily = (fontCatalog.families.empty()) ? nullptr : &fontCatalog.families[familyIndex];
			if (selectedFamily && selectedFamily->styles.empty()) {
				ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "%s", T("menu.settings.no_style_variants", "No style variants found for this family."));
			} else if (selectedFamily) {
				int styleIndex = 0;
				for (size_t s = 0; s < selectedFamily->styles.size(); ++s) {
					if (Util::IEquals(selectedFamily->styles[s].style, roleSettings.Style)) {
						styleIndex = static_cast<int>(s);
						break;
					}
				}
				if (styleIndex >= static_cast<int>(selectedFamily->styles.size())) {
					styleIndex = 0;
				}
				const char* stylePreview = selectedFamily->styles.empty() ? T("menu.settings.no_styles", "No styles") : selectedFamily->styles[styleIndex].displayName.c_str();
				std::string styleLabel = std::format("{} Style##{}", descriptor.displayName, roleIndex);
				{
					FontRoleGuard styleComboFont(Menu::FontRole::Body);
					if (ImGui::BeginCombo(styleLabel.c_str(), stylePreview)) {
						for (int s = 0; s < static_cast<int>(selectedFamily->styles.size()); ++s) {
							bool isSelected = (s == styleIndex);
							if (ImGui::Selectable(selectedFamily->styles[s].displayName.c_str(), isSelected)) {
								if (!isSelected) {
									const auto& chosen = selectedFamily->styles[s];
									roleSettings.Style = chosen.style;
									roleSettings.File = chosen.file;
									roleSettings.Family = selectedFamily->name;
									if (role == Menu::FontRole::Body) {
										themeSettings.FontName = roleSettings.File;
									}
									menuInstance->pendingFontReload = true;
								}
							}
							if (isSelected) {
								ImGui::SetItemDefaultFocus();
							}
						}
						ImGui::EndCombo();
					}
				}
			}

			ImGui::TextDisabled(T("menu.settings.file_label", "File: %s"), roleSettings.File.c_str());

			std::string scaleLabel = std::format("{} Scale##{}", descriptor.displayName, roleIndex);
			if (ImGui::SliderFloat(scaleLabel.c_str(), &roleSettings.SizeScale, 0.5f, 2.5f, "%.2fx", ImGuiSliderFlags_AlwaysClamp)) {
				menuInstance->pendingFontReload = true;
			}
			ImGui::SameLine();
			std::string resetLabel = std::format("Reset##Scale{}", roleIndex);
			if (ImGui::Button(resetLabel.c_str())) {
				roleSettings.SizeScale = Menu::GetFontRoleDefaultScale(role);
				menuInstance->pendingFontReload = true;
			}

			// Add Feature Title Scale slider under Title font role
			if (role == Menu::FontRole::Title) {
				ImGui::SliderFloat(T("menu.settings.feature_header_scale", "Feature Header Scale"), &themeSettings.FeatureHeading.FeatureTitleScale, 1.0f, 3.0f, "%.1fx", ImGuiSliderFlags_AlwaysClamp);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("%s", T("menu.settings.feature_header_scale_tooltip", "Scale multiplier for feature title text in the Settings tab."));
				}
				ImGui::SameLine();
				{
					std::string resetBtnLabel = std::string(T("menu.settings.reset", "Reset")) + "##FeatureHeaderScale";
					if (ImGui::Button(resetBtnLabel.c_str())) {
						themeSettings.FeatureHeading.FeatureTitleScale = ThemeManager::Constants::DEFAULT_FEATURE_TITLE_SCALE;
					}
				}
			}

			ImGui::Separator();
			ImGui::PopID();
		}

		if (ImGui::Button(T("menu.settings.refresh_font_families", "Refresh Font Families"))) {
			refreshFontCatalog();
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted(T("menu.settings.refresh_font_families_tooltip", "Rescan the Fonts directory after adding or removing font files."));
		}

		ImGui::EndTabItem();
	}
}

void SettingsTabRenderer::RenderStylingTab()
{
	auto tabLabel = std::format("{}##{}", T("menu.settings.tab_styling", "Styling"), "InterfaceStylingTab");
	if (BeginTabItemWithFont(tabLabel.c_str(), Menu::FontRole::Heading)) {
		auto& themeSettings = globals::menu->GetSettings().Theme;
		auto& style = themeSettings.Style;
		RenderSaveInfoText();

		SeparatorTextWithFont(T("menu.settings.section_main", "Main"), Menu::FontRole::Subheading);
		if (ImGui::SliderFloat(T("menu.settings.global_scale", "Global Scale"), &themeSettings.GlobalScale, -1.f, 1.f, "%.2f")) {
			float trueScale = exp2(themeSettings.GlobalScale);

			ImGui::GetStyle().FontScaleMain = trueScale;
		}

		SeparatorTextWithFont(T("menu.settings.section_layout", "Layout"), Menu::FontRole::Subheading);

		ImGui::SliderFloat2(T("menu.settings.window_padding", "Window Padding"), (float*)&style.WindowPadding, 0.0f, 20.0f, "%.0f");
		ImGui::SliderFloat2(T("menu.settings.frame_padding", "Frame Padding"), (float*)&style.FramePadding, 0.0f, 20.0f, "%.0f");
		ImGui::SliderFloat2(T("menu.settings.item_spacing", "Item Spacing"), (float*)&style.ItemSpacing, 0.0f, 20.0f, "%.0f");
		ImGui::SliderFloat2(T("menu.settings.item_inner_spacing", "Item Inner Spacing"), (float*)&style.ItemInnerSpacing, 0.0f, 20.0f, "%.0f");
		ImGui::SliderFloat(T("menu.settings.indent_spacing", "Indent Spacing"), &style.IndentSpacing, 0.0f, 30.0f, "%.0f");
		ImGui::SliderFloat(T("menu.settings.scrollbar_size", "Scrollbar Size"), &style.ScrollbarSize, 1.0f, 20.0f, "%.0f");
		ImGui::SliderFloat(T("menu.settings.grab_min_size", "Grab Min Size"), &style.GrabMinSize, 1.0f, 20.0f, "%.0f");

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

		SeparatorTextWithFont(T("menu.settings.section_borders", "Borders"), Menu::FontRole::Subheading);
		ImGui::SliderFloat(T("menu.settings.window_border_size", "Window Border Size"), &style.WindowBorderSize, 0.0f, 5.0f, "%.0f");
		ImGui::SliderFloat(T("menu.settings.child_border_size", "Child Border Size"), &style.ChildBorderSize, 0.0f, 5.0f, "%.0f");
		ImGui::SliderFloat(T("menu.settings.popup_border_size", "Popup Border Size"), &style.PopupBorderSize, 0.0f, 5.0f, "%.0f");
		ImGui::SliderFloat(T("menu.settings.frame_border_size", "Frame Border Size"), &style.FrameBorderSize, 0.0f, 5.0f, "%.0f");
		ImGui::SliderFloat(T("menu.settings.tab_border_size", "Tab Border Size"), &style.TabBorderSize, 0.0f, 5.0f, "%.0f");
		ImGui::SliderFloat(T("menu.settings.tab_bar_border_size", "Tab Bar Border Size"), &style.TabBarBorderSize, 0.0f, 5.0f, "%.0f");

		SeparatorTextWithFont(T("menu.settings.section_rounding", "Rounding"), Menu::FontRole::Subheading);
		ImGui::SliderFloat(T("menu.settings.window_rounding", "Window Rounding"), &style.WindowRounding, 0.0f, 12.0f, "%.0f");
		ImGui::SliderFloat(T("menu.settings.child_rounding", "Child Rounding"), &style.ChildRounding, 0.0f, 12.0f, "%.0f");
		ImGui::SliderFloat(T("menu.settings.frame_rounding", "Frame Rounding"), &style.FrameRounding, 0.0f, 12.0f, "%.0f");
		ImGui::SliderFloat(T("menu.settings.popup_rounding", "Popup Rounding"), &style.PopupRounding, 0.0f, 12.0f, "%.0f");
		ImGui::SliderFloat(T("menu.settings.scrollbar_rounding", "Scrollbar Rounding"), &style.ScrollbarRounding, 0.0f, 12.0f, "%.0f");
		ImGui::SliderFloat(T("menu.settings.grab_rounding", "Grab Rounding"), &style.GrabRounding, 0.0f, 12.0f, "%.0f");
		ImGui::SliderFloat(T("menu.settings.tab_rounding", "Tab Rounding"), &style.TabRounding, 0.0f, 12.0f, "%.0f");

		SeparatorTextWithFont(T("menu.settings.section_tables", "Tables"), Menu::FontRole::Subheading);
		ImGui::SliderFloat2(T("menu.settings.cell_padding", "Cell Padding"), (float*)&style.CellPadding, 0.0f, 20.0f, "%.0f");
		ImGui::SliderAngle(T("menu.settings.table_angled_headers_angle", "Table Angled Headers Angle"), &style.TableAngledHeadersAngle, -50.0f, +50.0f);

		SeparatorTextWithFont(T("menu.settings.section_widgets", "Widgets"), Menu::FontRole::Subheading);
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

		ImGui::EndTabItem();
	}
}

void SettingsTabRenderer::RenderColorsTab()
{
	auto tabLabel = std::format("{}##{}", T("menu.settings.tab_colors", "Colors"), "InterfaceColorsTab");
	if (BeginTabItemWithFont(tabLabel.c_str(), Menu::FontRole::Heading)) {
		auto& themeSettings = globals::menu->GetSettings().Theme;
		auto& colors = themeSettings.FullPalette;
		RenderSaveInfoText();

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
		ImVec2 iconPos = ImVec2(filterMin.x + ThemeManager::Constants::SEARCH_ICON_OFFSET_X * scale, filterMin.y + (filterSize.y - iconSize) * 0.5f);
		Util::DrawSearchIcon(iconPos, iconSize, ThemeManager::Constants::SEARCH_ICON_ALPHA);

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

		ImGui::EndTabItem();
	}
}
