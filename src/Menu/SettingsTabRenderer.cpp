#include "SettingsTabRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <functional>
#include <set>
#include <string>
#include <vector>
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
		{ Ids::kHeaderEffects, "menu.settings.show_status_bar", "Show Status Bar" },
		{ Ids::kStyling, "menu.settings.auto_hide_feature_list", "Auto-hide Feature List" },
		{ Ids::kStyling, "menu.settings.ui_animations", "UI animations" },
		{ Ids::kStyling, "menu.settings.animation_speed", "Animation speed" },
		{ Ids::kStyling, "menu.settings.feature_list_order", "Feature List Order" },
		{ Ids::kStyling, "menu.settings.require_shift_to_dock", "Require Shift to Dock" },
		{ Ids::kStyling, "menu.settings.tooltip_hover_delay", "Tooltip Hover Delay" },
		{ Ids::kStyling, "menu.settings.warn_forced_settings", "Warn When Settings Are Forced" },
		{ Ids::kStyling, "menu.settings.reset_window_layout", "Reset Window Layout" },
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
		{ Ids::kStyling, "menu.settings.surface_option_card", "Option Cards" },
		{ Ids::kStyling, "menu.settings.surface_section_card", "Section Cards" },
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

	/** @brief Visits every colour value of a theme the Colours page edits, as (data, float count). */
	template <class Theme, class Fn>
	void ForEachColorValue(Theme& theme, Fn&& fn)
	{
		for (auto* color : { &theme.Palette.Background, &theme.Palette.Text, &theme.Palette.WindowBorder, &theme.Palette.FrameBorder,
				 &theme.Palette.Separator, &theme.Palette.ResizeGrip, &theme.StatusPalette.Disable, &theme.StatusPalette.Error,
				 &theme.StatusPalette.Warning, &theme.StatusPalette.RestartNeeded, &theme.StatusPalette.CurrentHotkey,
				 &theme.StatusPalette.SuccessColor, &theme.StatusPalette.InfoColor, &theme.FeatureHeading.ColorDefault,
				 &theme.FeatureHeading.ColorHovered })
			fn(&color->x, 4);
		fn(&theme.FeatureHeading.MinimizedFactor, 1);
		for (auto& color : theme.FullPalette)
			fn(&color.x, 4);
	}

	/** @brief Every colour value of the live theme, flattened: the Colours page's undo snapshot. */
	std::vector<float> CaptureThemeColors()
	{
		std::vector<float> values;
		ForEachColorValue(globals::menu->GetSettings().Theme, [&](const float* data, int count) { values.insert(values.end(), data, data + count); });
		return values;
	}

	/** @brief The Colours page's undo target: every colour of the live theme, as one value. */
	UndoHistory::Target MakeThemeColorsUndoTarget()
	{
		return {
			.key = "theme.colors",
			.label = T("menu.settings.tab_colors", "Colors"),
			.capture = [] { return UndoHistory::Capture(CaptureThemeColors()); },
			.restore = [](const UndoHistory::Snapshot& snapshot) {
				const auto& values = UndoHistory::Get<std::vector<float>>(snapshot);
				size_t i = 0;
				ForEachColorValue(globals::menu->GetSettings().Theme, [&](float* data, int count) {
					for (int c = 0; c < count && i < values.size(); ++c)
						data[c] = values[i++];
				}); },
		};
	}

	/**
	 * @brief The selected theme as its file has it: what the Colours and Fonts resets put a value back to. Re-read when the selection
	 * changes or the page is opened again, so a Save Theme in between is picked up.
	 */
	const Menu::ThemeSettings& GetThemeFileReference()
	{
		static Menu::ThemeSettings reference;
		static std::string referenceName;
		static int lastFrame = -2;
		const std::string& selected = globals::menu->GetSettings().SelectedThemePreset;
		const int frame = ImGui::GetFrameCount();
		if (selected != referenceName || frame - lastFrame > 1) {
			referenceName = selected;
			reference = Menu::ThemeSettings{};
			if (const auto* info = ThemeManager::GetSingleton()->GetThemeInfo(selected); info && info->themeData.contains("Theme")) {
				try {
					const json& theme = info->themeData["Theme"];
					Menu::ThemeFromJson(theme, reference);
				} catch (const std::exception& e) {
					logger::warn("Colors page: could not read theme '{}' for reset: {}", selected, e.what());
					reference = Menu::ThemeSettings{};
				}
			}
		}
		lastFrame = frame;
		return reference;
	}

	/** @brief Whether two colours differ by more than a picker step. */
	bool ColorDiffers(const float* a, const float* b, int count)
	{
		for (int i = 0; i < count; ++i)
			if (std::abs(a[i] - b[i]) > 1.0f / 512.0f)
				return true;
		return false;
	}

	/** @brief One editable colour on the Colours page. */
	struct ColorEntry
	{
		const char* name;
		float* value;
		const float* reference;
		ImGuiColorEditFlags flags = ImGuiColorEditFlags_None;
		const char* note = nullptr;  ///< Hover text explaining a limit on the colour
	};

	/** @brief The Full Palette groups, in display order. */
	enum class PaletteGroup : int
	{
		Text,
		Windows,
		Inputs,
		Buttons,
		Lists,
		Tabs,
		Scrollbar,
		Tables,
		Plots,
		Docking,
		Count
	};

	const char* GetPaletteGroupTitle(PaletteGroup group)
	{
		switch (group) {
		case PaletteGroup::Text:
			return T("menu.settings.palette_group_text", "Text");
		case PaletteGroup::Windows:
			return T("menu.settings.palette_group_windows", "Windows & Popups");
		case PaletteGroup::Inputs:
			return T("menu.settings.palette_group_inputs", "Inputs, Sliders & Checkboxes");
		case PaletteGroup::Buttons:
			return T("menu.settings.palette_group_buttons", "Buttons");
		case PaletteGroup::Lists:
			return T("menu.settings.palette_group_lists", "Headers, Lists & Separators");
		case PaletteGroup::Tabs:
			return T("menu.settings.palette_group_tabs", "Tabs");
		case PaletteGroup::Scrollbar:
			return T("menu.settings.palette_group_scrollbar", "Scrollbar");
		case PaletteGroup::Tables:
			return T("menu.settings.palette_group_tables", "Tables");
		case PaletteGroup::Plots:
			return T("menu.settings.palette_group_plots", "Graphs");
		default:
			return T("menu.settings.palette_group_docking", "Docking & Navigation");
		}
	}

	/** @brief Group of an ImGuiCol, from its name so the mapping survives ImGui adding or reordering colours. */
	PaletteGroup GetPaletteGroup(int colorIndex)
	{
		const std::string_view name = ImGui::GetStyleColorName(colorIndex);
		const auto starts = [&](std::string_view prefix) { return name.starts_with(prefix); };
		if (starts("Text") || starts("InputTextCursor"))
			return PaletteGroup::Text;
		if (starts("Window") || starts("ChildBg") || starts("PopupBg") || starts("Border") || starts("TitleBg") || starts("MenuBarBg") || starts("ModalWindowDimBg"))
			return PaletteGroup::Windows;
		if (starts("FrameBg") || starts("CheckMark") || starts("SliderGrab"))
			return PaletteGroup::Inputs;
		if (starts("Button"))
			return PaletteGroup::Buttons;
		if (starts("Header") || starts("TreeLines") || starts("Separator") || starts("ResizeGrip"))
			return PaletteGroup::Lists;
		if (starts("Tab"))
			return PaletteGroup::Tabs;
		if (starts("Scrollbar"))
			return PaletteGroup::Scrollbar;
		if (starts("Table"))
			return PaletteGroup::Tables;
		if (starts("Plot"))
			return PaletteGroup::Plots;
		return PaletteGroup::Docking;
	}

	/** @brief Full Palette slots the theme fills from elsewhere, so editing them here would do nothing. */
	bool IsPaletteSlotDerived(int colorIndex)
	{
		switch (colorIndex) {
		case ImGuiCol_Text:
		case ImGuiCol_WindowBg:
		case ImGuiCol_Border:
		case ImGuiCol_FrameBg:
		case ImGuiCol_Separator:
		case ImGuiCol_ResizeGrip:
		case ImGuiCol_ResizeGripHovered:
		case ImGuiCol_ResizeGripActive:
			return true;
		default:
			return false;
		}
	}

	/** @brief State of the Colours page's toolbar. UI state only. */
	struct ColorsPageState
	{
		ImGuiTextFilter filter;
		bool changedOnly = false;
	};

	ColorsPageState& GetColorsPageState()
	{
		static ColorsPageState state;
		return state;
	}

	/** @brief Whether an entry passes the search text and the Changed only toggle. */
	bool IsColorEntryShown(const ColorEntry& entry)
	{
		const auto& state = GetColorsPageState();
		if (!state.filter.PassFilter(entry.name))
			return false;
		const int count = (entry.flags & ImGuiColorEditFlags_NoAlpha) ? 3 : 4;
		return !state.changedOnly || ColorDiffers(entry.value, entry.reference, count);
	}

	/**
	 * @brief Draws colours as a grid of swatch + name cells, with a reset button on each one that differs from the
	 * theme file. Clicking a swatch opens the picker; hovering it shows the value.
	 */
	void DrawColorGrid(const char* id, const std::vector<ColorEntry>& entries)
	{
		const float cellMinWidth = ImGui::GetFontSize() * ThemeManager::Constants::COLORS_PAGE_CELL_WIDTH_FONT_SIZES;
		const int columns = std::clamp(static_cast<int>(ImGui::GetContentRegionAvail().x / cellMinWidth), 1, 3);
		if (!ImGui::BeginTable(id, columns, ImGuiTableFlags_SizingStretchSame))
			return;
		for (const auto& entry : entries) {
			ImGui::TableNextColumn();
			ImGui::PushID(entry.name);
			const float cellStartX = ImGui::GetCursorPosX();
			const float cellWidth = ImGui::GetContentRegionAvail().x;
			ImGui::ColorEdit4("##Color", entry.value,
				entry.flags | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
			ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(entry.name);
			if (entry.note)
				Util::HoverTip(entry.note);

			const int count = (entry.flags & ImGuiColorEditFlags_NoAlpha) ? 3 : 4;
			if (ColorDiffers(entry.value, entry.reference, count)) {
				const float buttonWidth = ImGui::GetFrameHeight();
				ImGui::SameLine();
				ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), cellStartX + cellWidth - buttonWidth));
				if (BrowserUI::IconButton("##Reset", Icons::TI(MenuIcons::kRestore), T("menu.settings.color_reset_tooltip", "Reset to the theme's colour")))
					std::copy_n(entry.reference, count, entry.value);
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	/** @brief A SectionCard holding a colour grid; skipped entirely when the toolbar filters every entry out. */
	void DrawColorCard(const char* id, const char* icon, const char* title, const std::vector<ColorEntry>& entries, const std::function<void()>& extra = {})
	{
		std::vector<ColorEntry> shown;
		for (const auto& entry : entries)
			if (IsColorEntryShown(entry))
				shown.push_back(entry);
		if (shown.empty() && !extra)
			return;
		Util::SectionCard card(id, icon, title);
		DrawColorGrid("##Grid", shown);
		if (extra)
			extra();
	}

	/** @brief The Fonts page's undo target: base size, resolution sizing, every role's font and scale, and the feature header scale. */
	UndoHistory::Target MakeFontsUndoTarget()
	{
		return {
			.key = "theme.fonts",
			.label = T("menu.settings.tab_fonts", "Fonts"),
			.capture = [] {
				const auto& settings = globals::menu->GetSettings();
				json state = { { "size", settings.Theme.FontSize }, { "auto", settings.UseResolutionFont }, { "titleScale", settings.Theme.FeatureHeading.FeatureTitleScale } };
				for (const auto& role : settings.Theme.FontRoles)
					state["roles"].push_back({ role.Family, role.Style, role.File, role.SizeScale });
				return UndoHistory::Capture(std::move(state)); },
			.restore = [](const UndoHistory::Snapshot& snapshot) {
				const auto& state = UndoHistory::Get<json>(snapshot);
				auto& settings = globals::menu->GetSettings();
				settings.Theme.FontSize = state["size"].get<float>();
				settings.UseResolutionFont = state["auto"].get<bool>();
				settings.Theme.FeatureHeading.FeatureTitleScale = state["titleScale"].get<float>();
				const auto& roles = state["roles"];
				for (size_t i = 0; i < settings.Theme.FontRoles.size() && i < roles.size(); ++i) {
					auto& role = settings.Theme.FontRoles[i];
					role.Family = roles[i][0].get<std::string>();
					role.Style = roles[i][1].get<std::string>();
					role.File = roles[i][2].get<std::string>();
					role.SizeScale = roles[i][3].get<float>();
				}
				settings.Theme.FontName = settings.Theme.FontRoles[static_cast<size_t>(Menu::FontRole::Body)].File;
				MenuFonts::GetAtlasState().pendingFontReload = true; },
		};
	}

	/** @brief What each font role is used for, under its name on the Fonts page. */
	const char* GetFontRoleDescription(Menu::FontRole role)
	{
		switch (role) {
		case Menu::FontRole::Body:
			return T("menu.settings.font_role_body_desc", "Most of the menu: settings, lists and buttons.");
		case Menu::FontRole::Title:
			return T("menu.settings.font_role_title_desc", "Large titles, such as the menu header and feature names at the top of a page.");
		case Menu::FontRole::Heading:
			return T("menu.settings.font_role_heading_desc", "Section headers, such as card titles and category labels.");
		case Menu::FontRole::Subheading:
			return T("menu.settings.font_role_subheading_desc", "Subsection headers and separators within a page.");
		case Menu::FontRole::Subtext:
		default:
			return T("menu.settings.font_role_subtext_desc", "Smaller secondary text, such as descriptions and hints.");
		}
	}

	/** @brief Whether a role's font and scale match the theme file. */
	bool FontRoleMatches(const Menu::ThemeSettings::FontRoleSettings& a, const Menu::ThemeSettings::FontRoleSettings& b)
	{
		return a.Family == b.Family && a.Style == b.Style && a.File == b.File && std::abs(a.SizeScale - b.SizeScale) < 0.001f;
	}

	/**
	 * @brief One row of the role list: the role's name drawn in its own font, with its family, style and scale
	 * underneath. @return True when clicked.
	 */
	bool FontRoleRow(Menu::FontRole role, const Menu::ThemeSettings::FontRoleSettings& settings, bool selected)
	{
		const auto& descriptor = Menu::FontRoleDescriptors[static_cast<size_t>(role)];
		const std::string detail = std::format("{} {} \u00b7 {:.2f}x", settings.Family, settings.Style, settings.SizeScale);
		float nameHeight;
		ImFont* nameFont;
		float nameSize;
		{
			MenuFonts::FontRoleGuard roleFont(role);
			nameFont = ImGui::GetFont();
			nameSize = ImGui::GetFontSize();
			nameHeight = ImGui::GetTextLineHeight();
		}
		ImFont* detailFont;
		float detailSize;
		{
			MenuFonts::FontRoleGuard subtext(Menu::FontRole::Subtext);
			detailFont = ImGui::GetFont();
			detailSize = ImGui::GetFontSize();
		}
		const ImGuiStyle& style = ImGui::GetStyle();
		const float pad = style.FramePadding.y;
		const float height = pad * 2.0f + nameHeight + detailSize;
		const bool clicked = Util::Selectable("##RoleRow", selected, ImGuiSelectableFlags_None, ImVec2(0.0f, height));
		const ImVec2 min = ImGui::GetItemRectMin();
		const ImVec2 max = ImGui::GetItemRectMax();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->PushClipRect(min, max, true);
		const float x = min.x + style.FramePadding.x;
		drawList->AddText(nameFont, nameSize, ImVec2(x, min.y + pad), ImGui::GetColorU32(ImGuiCol_Text), descriptor.displayName.data());
		drawList->AddText(detailFont, detailSize, ImVec2(x, min.y + pad + nameHeight), ImGui::GetColorU32(ImGuiCol_TextDisabled), detail.c_str());
		drawList->PopClipRect();
		return clicked;
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
	MenuFonts::FontRoleGuard subtextFont(Menu::FontRole::Subtext);
	ImGui::PushStyleColor(ImGuiCol_Text, ts.StatusPalette.InfoColor);
	ImGui::TextWrapped("%s", T("menu.settings.theme_save_model",
								 "Theme edits are saved into the theme file with Save Theme. "
								 "Which theme is selected is saved with Save Settings (Ctrl+S)."));
	ImGui::PopStyleColor();
}

void SettingsTabRenderer::RenderShadersSection()
{
	auto shaderCache = globals::shaderCache;
	auto* i18n = I18n::GetSingleton();

	// Status: the last build at a glance and the cache actions
	{
		Util::SectionCard card("##ShaderCacheStatus", MenuIcons::kShaders, T("menu.settings.card_shader_cache", "Shader Cache"));
		const uint64_t failed = shaderCache->GetFailedTasks();
		if (shaderCache->IsCompiling()) {
			ImGui::TextColored(Util::Colors::GetInfo(), "%s",
				i18n->Format("menu.status.compiling",
						{ { "done", std::to_string(shaderCache->GetCompletedTasks()) }, { "total", std::to_string(shaderCache->GetTotalTasks()) } },
						"Compiling shaders {done}/{total}")
					.c_str());
		} else if (shaderCache->GetTotalTasks() == 0) {
			Util::Text::Disabled("%s", T("menu.home.shaders_none", "No shaders compiled yet"));
		} else {
			ImGui::TextUnformatted(i18n->Format("menu.home.shaders_build_time",
										   { { "time", shaderCache->GetShaderStatsString(true, true) } }, "Last build: {time}")
					.c_str());
			ImGui::SameLine();
			if (failed > 0)
				ImGui::TextColored(Util::Colors::GetError(), "%s",
					i18n->Format("menu.status.failed_shaders", { { "count", std::to_string(failed) } }, "Failed shaders: {count}").c_str());
			else
				Util::Text::Success("%s", T("menu.home.shaders_no_failures", "No failed shaders"));
		}

		if (shaderCache->GetTotalTasks() > 0) {
			ShaderCacheStatusWidget::DrawBreakdownBar();
			ShaderCacheStatusWidget::DrawBreakdownLegend();
			if (globals::state->IsDeveloperMode()) {
				MenuFonts::FontRoleGuard subtext(Menu::FontRole::Subtext);
				ImGui::TextDisabled("Threads: %d compile, %d background, %d pool | P-cores: %d",
					(int)shaderCache->compilationThreadCount,
					(int)shaderCache->backgroundCompilationThreadCount,
					(int)shaderCache->compilationPool.get_thread_count(),
					(int)Util::GetPerformanceCoreCount());
			}
		}

		ImGui::Spacing();
		if (Util::Button(T("menu.advanced.clear_shader_cache", "Clear Shader Cache")))
			shaderCache->Clear();
		Util::HoverTip(T("menu.advanced.clear_shader_cache_tooltip", "Clear all compiled shaders from memory. Forces recompilation of all shaders on next use."));
		ImGui::SameLine();
		if (Util::Button(T("menu.settings.full_recache", "Full Recache...")))
			Util::RequestClearShaderCacheConfirmation();
		Util::HoverTip(T("menu.settings.full_recache_tooltip", "Clears the memory and disk caches, so every shader compiles again at the next start."));
		ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 4.0f);

		auto& menuSettings = globals::menu->GetSettings();
		bool skipConfirmation = menuSettings.SkipClearCacheConfirmation;
		if (Util::CheckboxTip(T("menu.settings.skip_clear_cache_dialogue", "Skip Clear Cache Dialogue"), &skipConfirmation, T("menu.settings.skip_clear_cache_dialogue_tooltip", "When checked, the shader cache will be cleared immediately without asking for confirmation."))) {
			menuSettings.SkipClearCacheConfirmation = skipConfirmation;
		}
	}

	// Shaders and the disk cache side by side: whether CS replaces the game's shaders, and how they are kept
	{
		Util::SectionCard card("##ShaderOptions", ICON_TI_SPARKLES, T("menu.settings.card_shader_options", "Shaders & Disk Cache"));
		Util::SettingColumns columns("##ShaderOptionColumns");

		bool useCustomShaders = shaderCache->IsEnabled();
		if (ImGui::Checkbox(T("menu.settings.use_custom_shaders", "Use Custom Shaders"), &useCustomShaders))
			shaderCache->SetEnabled(useCustomShaders);
		Util::SettingHint(T("menu.settings.use_custom_shaders_tooltip", "Disabling this effectively disables all features."));

		ImGui::Spacing();
		bool useAsync = shaderCache->IsAsync();
		if (ImGui::Checkbox(T("menu.settings.enable_async", "Enable Async"), &useAsync))
			shaderCache->SetAsync(useAsync);
		Util::SettingHint(T("menu.settings.enable_async_hint", "Draws the game's own shader until its replacement is compiled, so startup is much faster."));

		columns.Next();
		bool useDiskCache = shaderCache->IsDiskCache();
		if (ImGui::Checkbox(T("menu.settings.enable_disk_cache", "Enable Disk Cache"), &useDiskCache))
			shaderCache->SetDiskCache(useDiskCache);
		Util::SettingHint(T("menu.settings.enable_disk_cache_hint", "Saves compiled shaders to disk, so later starts load them instead of compiling."));

		ImGui::Spacing();
		bool skipUnchanged = shaderCache->IsSkipUnchangedShaders();
		{
			Util::DisableGuard noDiskCache(!useDiskCache);
			if (ImGui::Checkbox(T("menu.settings.skip_unchanged_shaders", "Skip Unchanged Shaders"), &skipUnchanged))
				shaderCache->SetSkipUnchangedShaders(skipUnchanged);
			Util::HoverTip(T("menu.settings.skip_unchanged_shaders_tooltip",
				"When enabled, each shader is recompiled from source only if its .hlsl file "
				"is newer than the cached .bin on disk. "
				"Shaders whose source has not changed are loaded directly from the disk cache, "
				"avoiding the full startup compilation cost. "
				"Useful for iterative testing: change a shader file and only that shader is rebuilt. "
				"Requires 'Enable Disk Cache' to be active."));
			Util::SettingHint(T("menu.settings.skip_unchanged_shaders_hint", "Rebuilds only shaders whose source changed since they were cached. Needs the disk cache."));
		}
	}
}

void SettingsTabRenderer::RenderKeybindingsSection(SettingsState& state)
{
	auto& settings = globals::menu->GetSettings();
	static const Menu::Settings defaults{};

	{
		Util::SectionCard card("##KeysMenusEditors", ICON_TI_LAYOUT_COLUMNS, T("menu.settings.keys_menus_editors", "Menus & Editors"));
		KeybindingRow(T("menu.settings.toggle_key", "Toggle Key:"), settings.ToggleKey, defaults.ToggleKey, state.settingToggleKey, "Change##toggle");
		KeybindingRow(T("menu.settings.cs_editor_toggle_key", "CS Editor Toggle Key:"), settings.CSEditorToggleKey, defaults.CSEditorToggleKey, state.settingCSEditorToggleKey, "Change##CSEditorToggle");
		KeybindingRow(T("menu.settings.effects11_editor_key", "Effects 11 Editor Key:"), settings.Effects11EditorKey, defaults.Effects11EditorKey, state.settingEffects11EditorKey, "Change##Effects11Editor");
	}

	{
		Util::SectionCard card("##KeysOverlaysCapture", MenuIcons::kProfiling, T("menu.settings.keys_overlays_capture", "Overlays & Capture"));
		KeybindingRow(T("menu.settings.overlay_toggle_key", "Overlay Toggle Key:"), settings.OverlayToggleKey, defaults.OverlayToggleKey, state.settingOverlayToggleKey, "Change##OverlayToggle");
		KeybindingRow(T("menu.settings.screenshot_key", "Screenshot Key:"), settings.ScreenshotKey, defaults.ScreenshotKey, state.settingScreenshotKey, "Change##Screenshot");
	}

	Util::SectionCard card("##KeysDebug", Util::GetCategoryIcon("Utility"), T("menu.settings.keys_debug", "Debug"));
	KeybindingRow(T("menu.settings.effect_toggle_key", "Effect Toggle Key:"), settings.EffectToggleKey, defaults.EffectToggleKey, state.settingsEffectsToggle, "Change##EffectToggle");
	KeybindingRow(T("menu.settings.effects11_toggle_key", "Effects 11 Toggle Key:"), settings.Effects11ToggleKey, defaults.Effects11ToggleKey, state.settingEffects11ToggleKey, "Change##Effects11Toggle");
	KeybindingRow(T("menu.settings.skip_compilation_key", "Skip Compilation Key:"), settings.SkipCompilationKey, defaults.SkipCompilationKey, state.settingSkipCompilationKey, "Change##skip");
	KeybindingRow(T("menu.settings.shader_block_prev_key", "Shader Block Previous Key:"), settings.ShaderBlockPrevKey, defaults.ShaderBlockPrevKey, state.settingShaderBlockPrevKey, "Change##ShaderBlockPrev");
	KeybindingRow(T("menu.settings.shader_block_next_key", "Shader Block Next Key:"), settings.ShaderBlockNextKey, defaults.ShaderBlockNextKey, state.settingShaderBlockNextKey, "Change##ShaderBlockNext");
	if (!settings.EnableShaderBlocking) {
		Util::SettingHint(T("menu.settings.shader_block_keys_hint",
							  "The shader block keys work while Enable Shader Blocking is on (Shader Debug, Developer Mode only)."),
			false);
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
	Util::HoverTip(T("menu.settings.language_tooltip", "Select the display language for the Community Shaders interface."));
}

void SettingsTabRenderer::RenderInterfaceOptions()
{
	auto& themeSettings = globals::menu->GetSettings().Theme;

	SeparatorTextWithFont(T("menu.settings.section_animations", "Animations"), Menu::FontRole::Subheading);

	// The master switch is stored as its opposite (ReduceUIAnimations), so settings files from before the
	// animation speed option keep their meaning
	auto& menuSettings = globals::menu->GetSettings();
	bool animationsOn = !menuSettings.ReduceUIAnimations;
	if (Util::CheckboxTip(T("menu.settings.ui_animations", "UI animations"), &animationsOn, T("menu.settings.ui_animations_tooltip", "Hover fades, sliding highlights and fade-ins in the menu. Turn off to make every change snap instead of easing.")))
		menuSettings.ReduceUIAnimations = !animationsOn;
	{
		Util::DisableGuard disable(!animationsOn);
		Util::SliderFloatTip(T("menu.settings.animation_speed", "Animation speed"), &menuSettings.AnimationSpeed, ThemeManager::Constants::ANIM_SPEED_MIN, ThemeManager::Constants::ANIM_SPEED_MAX, "%.2fx", T("menu.settings.animation_speed_tooltip", "How fast menu animations run: 0.5x is slower, 2x is quicker. 1x is the default."), ImGuiSliderFlags_AlwaysClamp);
	}

	SeparatorTextWithFont(T("menu.settings.section_window_layout", "Window & Layout"), Menu::FontRole::Subheading);

	Util::CheckboxTip(T("menu.settings.auto_hide_feature_list", "Auto-hide Feature List"), &globals::menu->GetSettings().AutoHideFeatureList, T("menu.settings.auto_hide_feature_list_tooltip", "Automatically hides the left feature list panel. Move cursor to the left edge to show it."));

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

	if (Util::Button(T("menu.settings.reset_window_layout", "Reset Window Layout")))
		globals::menu->resetLayout = true;
	Util::HoverTip(T("menu.settings.reset_window_layout_tooltip", "Moves the menu window back to the centre of the screen at its default size."));

	if (Util::CheckboxTip(T("menu.settings.require_shift_to_dock", "Require Shift to Dock"), &globals::menu->GetSettings().RequireShiftToDock, T("menu.settings.require_shift_to_dock_tooltip", "When enabled, you must hold Shift while dragging to dock/snap windows. Prevents accidental docking."))) {
		ImGui::GetIO().ConfigDockingWithShift = globals::menu->GetSettings().RequireShiftToDock;
	}

	SeparatorTextWithFont(T("menu.settings.section_tooltips_warnings", "Tooltips & Warnings"), Menu::FontRole::Subheading);

	Util::SliderFloatTip(T("menu.settings.tooltip_hover_delay", "Tooltip Hover Delay"), &themeSettings.TooltipHoverDelay, 0.0f, 2.0f, "%.2f s", T("menu.settings.tooltip_hover_delay_tooltip", "Time in seconds to wait before a tooltip appears when hovering over an item."), ImGuiSliderFlags_AlwaysClamp);

	bool warnForcedSettings = !globals::menu->GetSettings().SkipConstraintWarning;
	if (Util::CheckboxTip(T("menu.settings.warn_forced_settings", "Warn When Settings Are Forced"), &warnForcedSettings, T("menu.settings.warn_forced_settings_tooltip", "Shows a popup when a setting change makes another feature force one of its settings to a fixed value.")))
		globals::menu->GetSettings().SkipConstraintWarning = !warnForcedSettings;
}

void SettingsTabRenderer::RenderHeaderEffectsSection()
{
	auto& themeSettings = globals::menu->GetSettings().Theme;

	SeparatorTextWithFont(T("menu.settings.section_header", "Header"), Menu::FontRole::Subheading);

	if (Util::CheckboxTip(T("menu.settings.use_monochrome_icons", "Use Monochrome Icons"), &themeSettings.UseMonochromeIcons, T("menu.settings.use_monochrome_icons_tooltip", "Uses white monochrome icons that adapt to your theme's text color"))) {
		Util::IconLoader::PendingReload() = true;
	}
	ImGui::SameLine();
	if (Util::CheckboxTip(T("menu.settings.use_monochrome_cs_logo", "Use Monochrome CS Logo"), &themeSettings.UseMonochromeLogo, T("menu.settings.use_monochrome_cs_logo_tooltip", "Uses monochrome version of the Community Shaders logo"))) {
		Util::IconLoader::PendingReload() = true;
	}

	Util::CheckboxTip(T("menu.settings.center_header_title", "Center Header Title"), &themeSettings.CenterHeader, T("menu.settings.center_header_title_tooltip", "Centers the Community Shaders title and logo in the header title bar"));

	Util::CheckboxTip(T("menu.settings.show_status_bar", "Show Status Bar"), &themeSettings.ShowFooter, T("menu.settings.show_status_bar_tooltip",
																											"Shows a bar at the bottom of the window with unsaved changes, shader compilation, "
																											"failed shaders and feature issues, plus the GPU, game version and swap chain"));

	SeparatorTextWithFont(T("menu.settings.visual_effects", "Visual Effects"), Menu::FontRole::Subheading);

	if (Util::CheckboxTip(T("menu.settings.background_blur", "Background Blur"), &themeSettings.BackgroundBlurEnabled, T("menu.settings.background_blur_tooltip", "Applies a blur effect to the background behind the menu window."))) {
		BackgroundBlur::SetEnabled(themeSettings.BackgroundBlurEnabled);
	}

	Util::CheckboxTip(T("menu.settings.menu_fade", "Menu Fade"), &themeSettings.MenuFadeEnabled, T("menu.settings.menu_fade_tooltip", "Fades the menu and its background blur in and out when it is opened or closed."));

	if (Util::CheckboxTip(T("menu.settings.use_custom_cursor", "Use Custom Theme Cursor"), &themeSettings.UseCustomCursor, T("menu.settings.use_custom_cursor_tooltip",
																															   "Loads cursor PNGs from the active theme folder (Themes/<preset>/).\n"
																															   "Supported files include cursor.png (arrow), cursor_text.png (typing), cursor_resize_ew.png, cursor_resize_ns.png, and more.\n"
																															   "Missing types fall back to the default ImGui cursor. Configure per-type hotspots in theme JSON."))) {
		globals::menu->pendingCursorReload = true;
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

	if (Icons::LabeledButton("##RefreshThemes", Icons::TI(ICON_TI_REFRESH), T("menu.settings.refresh", "Refresh"))) {
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
	if (Icons::LabeledButton("##OpenThemesFolder", Icons::TI(MenuIcons::kFolder), T("menu.settings.open_themes_folder", "Open Themes Folder"))) {
		std::filesystem::path themesPath = Util::PathHelpers::GetThemesRealPath();
		ShellExecuteA(NULL, "open", themesPath.string().c_str(), NULL, NULL, SW_SHOWNORMAL);
	}
	Util::HoverTip(T("menu.settings.open_themes_folder_tooltip", "Opens the Themes folder where you can add custom theme files."));

	// Selected theme section: name + description
	ImGui::Spacing();
	ImGui::Separator();
	if (currentItem >= 0 && currentItem < static_cast<int>(themes.size())) {
		ImGui::Spacing();
		const auto& selectedTheme = themes[currentItem];
		ImGui::PushStyleColor(ImGuiCol_Text, Util::Colors::GetAccent());
		Icons::Text(Icons::TI(MenuIcons::kPresets));
		ImGui::PopStyleColor();
		ImGui::SameLine();
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
		if (Util::ButtonWithFlash("##SaveTheme", MenuIcons::kSave, T("menu.settings.save_theme", "Save Theme"))) {
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

	if (Util::ButtonWithFlash("##SaveAsNewTheme", ICON_TI_PLUS, T("menu.settings.save_as_new_theme", "Save As New Theme"))) {
		showCreateThemePopup = true;
		memset(newThemeName, 0, sizeof(newThemeName));
		memset(newThemeDisplayName, 0, sizeof(newThemeDisplayName));
		memset(newThemeDescription, 0, sizeof(newThemeDescription));
		showValidationError = false;
	}

	if (!isPreset && currentThemeInfo && !currentThemeInfo->filePath.empty()) {
		ImGui::SameLine();
		if (Util::ErrorButtonWithFlash("##DeleteTheme", ICON_TI_TRASH, T("menu.settings.delete_theme", "Delete"))) {
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
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, ThemeManager::Constants::INVALID_INPUT_BORDER_SIZE);
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

		Util::HoverTip(T("menu.settings.theme_name_tooltip", "File name for the theme (without .json extension)"));

		// Highlight the input field if invalid and validation error is shown
		if (isDuplicateDisplayName && showValidationError) {
			ImGui::PushStyleColor(ImGuiCol_Border, themeSettings.StatusPalette.Error);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, ThemeManager::Constants::INVALID_INPUT_BORDER_SIZE);
		}

		ImGui::InputText(T("menu.settings.display_name", "Display Name"), newThemeDisplayName, sizeof(newThemeDisplayName));

		if (isDuplicateDisplayName && showValidationError) {
			ImGui::PopStyleVar();
			ImGui::PopStyleColor();
			ImGui::TextColored(themeSettings.StatusPalette.Error, "%s", T("menu.settings.display_name_duplicate", "A theme with this display name already exists"));
		}

		Util::HoverTip(T("menu.settings.display_name_tooltip", "Human-readable name shown in the dropdown"));

		{
			float scale = Util::GetUIScale();
			ImGui::InputTextMultiline(T("menu.settings.description", "Description"), newThemeDescription, sizeof(newThemeDescription), ImVec2(ThemeManager::Constants::THEME_DESC_BOX_WIDTH * scale, ThemeManager::Constants::THEME_DESC_BOX_HEIGHT * scale));
		}
		Util::HoverTip(T("menu.settings.description_tooltip", "Optional description for the theme"));

		ImGui::Separator();

		// Buttons
		if (Util::ButtonWithFlash("##CreateTheme", ICON_TI_CHECK, T("menu.settings.create_theme", "Create Theme"))) {
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
		if (Icons::LabeledButton("##CancelCreateTheme", Icons::TI(ICON_TI_X), T("menu.settings.cancel", "Cancel"))) {
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
	// Watched before anything draws, so each slider drag, pick or reset undoes as one step
	menuInstance->WatchUndoTarget(MakeFontsUndoTarget());
	const auto& reference = GetThemeFileReference();

	static Util::Fonts::Catalog fontCatalog;
	static bool catalogInitialized = false;
	static size_t selectedRole = 0;
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

	// Size: the base size every role scales from
	{
		Util::SectionCard card("##FontSize", ICON_TI_TEXT_SIZE, T("menu.settings.card_font_size", "Size"));
		bool& useAutoFont = menuInstance->GetSettings().UseResolutionFont;
		if (ImGui::Checkbox(T("menu.settings.use_resolution_based_font_size", "Use resolution-based font size"), &useAutoFont)) {
			if (!useAutoFont) {
				// Seed the fixed-size slider with the current effective size so it doesn't jump
				float effective = ThemeManager::ResolveFontSize(*menuInstance);
				themeSettings.FontSize = std::clamp(effective, ThemeManager::Constants::MIN_FONT_SIZE, ThemeManager::Constants::MAX_FONT_SIZE);
			}
			MenuFonts::GetAtlasState().pendingFontReload = true;
		}
		Util::SettingHint(T("menu.settings.use_resolution_based_font_size_tooltip", "When enabled, the UI font size scales with your screen resolution. Disable to set a fixed size."));

		ImGui::Spacing();
		{
			Util::DisableGuard autoSize(useAutoFont);
			if (ImGui::SliderFloat(T("menu.settings.base_font_size", "Base Font Size"), &themeSettings.FontSize, ThemeManager::Constants::MIN_FONT_SIZE, ThemeManager::Constants::MAX_FONT_SIZE, "%.0f")) {
				MenuFonts::GetAtlasState().pendingFontReload = true;
			}
		}
		const float effectiveNow = ThemeManager::ResolveFontSize(*menuInstance);
		Util::SettingHint(I18n::GetSingleton()->Format("menu.settings.effective_size_hint", { { "size", std::format("{:.0f}", std::round(effectiveNow)) } },
												  "Body text is drawn at {size} px; the other roles scale from it.")
							  .c_str(),
			false);
	}

	// Roles: pick one on the left, edit it on the right
	{
		Util::SectionCard card("##FontRoles", ICON_TI_TYPOGRAPHY, T("menu.settings.font_roles", "Font Roles"));
		if (Icons::LabeledButton("##RefreshFonts", Icons::TI(ICON_TI_REFRESH), T("menu.settings.refresh_font_families", "Refresh Font Families"))) {
			refreshFontCatalog(true);
			MenuFonts::GetAtlasState().pendingFontReload = true;
		}
		Util::HoverTip(T("menu.settings.refresh_font_families_tooltip", "Rescan the Fonts directory after adding or removing font files."));
		if (fontCatalog.families.empty())
			Util::Text::Warning("%s", T("menu.settings.no_fonts_found", "No fonts found. Place .ttf files in Interface/CommunityShaders/Fonts/"));
		ImGui::Spacing();

		selectedRole = std::min(selectedRole, Menu::FontRoleDescriptors.size() - 1);
		const float listWidth = ImGui::GetFontSize() * ThemeManager::Constants::FONTS_PAGE_ROLE_LIST_FONT_SIZES;
		if (ImGui::BeginTable("##FontRoleLayout", 2, ImGuiTableFlags_BordersInnerV)) {
			ImGui::TableSetupColumn("##Roles", ImGuiTableColumnFlags_WidthFixed, listWidth);
			ImGui::TableSetupColumn("##Detail", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableNextRow();

			ImGui::TableNextColumn();
			for (size_t i = 0; i < Menu::FontRoleDescriptors.size(); ++i) {
				ImGui::PushID(static_cast<int>(i));
				if (FontRoleRow(static_cast<Menu::FontRole>(i), themeSettings.FontRoles[i], selectedRole == i))
					selectedRole = i;
				ImGui::PopID();
			}

			ImGui::TableNextColumn();
			const auto role = static_cast<Menu::FontRole>(selectedRole);
			auto& roleSettings = themeSettings.FontRoles[selectedRole];
			const auto& referenceRole = reference.FontRoles[selectedRole];
			ImGui::PushID(static_cast<int>(selectedRole));
			{
				MenuFonts::FontRoleGuard heading(Menu::FontRole::Heading);
				ImGui::TextUnformatted(Menu::FontRoleDescriptors[selectedRole].displayName.data());
			}
			Util::SettingHint(GetFontRoleDescription(role), false);
			ImGui::Spacing();

			MenuFonts::Selector::RenderRolePickers(*menuInstance, themeSettings, fontCatalog, role, selectedRole);

			// Preview in the role's own font
			ImGui::Spacing();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x * 2.0f, ImGui::GetStyle().FramePadding.y * 3.0f));
			if (ImGui::BeginChild("##FontPreview", ImVec2(0.0f, 0.0f), ImGuiChildFlags_FrameStyle | ImGuiChildFlags_AutoResizeY)) {
				MenuFonts::FontRoleGuard sampleFont(role);
				ImGui::TextWrapped("%s", T("menu.settings.font_preview_sample", "The quick brown fox jumps over the lazy dog."));
				ImGui::TextWrapped("%s", "0123456789 ABCDEFGHIJKLM abcdefghijklm");
			}
			ImGui::EndChild();
			ImGui::PopStyleVar();
			Util::SettingHint(Util::PathHelpers::ToUtf8(std::filesystem::path(roleSettings.File).filename()).c_str(), false);

			// Actions for this role
			ImGui::Spacing();
			if (Icons::LabeledButton("##UseEverywhere", Icons::TI(ICON_TI_COPY), T("menu.settings.font_use_everywhere", "Use for All Roles"))) {
				for (size_t i = 0; i < themeSettings.FontRoles.size(); ++i) {
					auto& other = themeSettings.FontRoles[i];
					other.Family = roleSettings.Family;
					other.Style = roleSettings.Style;
					other.File = roleSettings.File;
				}
				themeSettings.FontName = roleSettings.File;
				MenuFonts::GetAtlasState().pendingFontReload = true;
			}
			Util::HoverTip(T("menu.settings.font_use_everywhere_tooltip", "Give every role this family and style. Each role keeps its own scale."));
			if (!FontRoleMatches(roleSettings, referenceRole)) {
				ImGui::SameLine();
				if (Icons::LabeledButton("##ResetRole", Icons::TI(MenuIcons::kRestore), T("menu.settings.font_reset_role", "Reset Role"))) {
					roleSettings = referenceRole;
					if (role == Menu::FontRole::Body)
						themeSettings.FontName = roleSettings.File;
					MenuFonts::GetAtlasState().pendingFontReload = true;
				}
				Util::HoverTip(T("menu.settings.font_reset_role_tooltip", "Put this role's font and scale back to the selected theme's file. Undo brings your edit back."));
			}
			ImGui::PopID();
			ImGui::EndTable();
		}
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
			RenderInterfaceOptions();
			SeparatorTextWithFont(T("menu.settings.scrollbar_opacity", "Scrollbar Opacity"), Menu::FontRole::Subheading);
			Util::SliderFloatTip(T("menu.settings.track_opacity", "Track Opacity"), &themeSettings.ScrollbarOpacity.Background, 0.0f, 1.0f, "%.2f", T("menu.settings.track_opacity_tooltip", "Controls the opacity of the scrollbar track/channel (the background area behind the scrollbar)."));
			Util::SliderFloatTip(T("menu.settings.thumb_opacity", "Thumb Opacity"), &themeSettings.ScrollbarOpacity.Thumb, 0.0f, 1.0f, "%.2f", T("menu.settings.thumb_opacity_tooltip", "Controls the opacity of the scrollbar thumb (the draggable part)."));
			Util::SliderFloatTip(T("menu.settings.thumb_hovered_opacity", "Thumb Hovered Opacity"), &themeSettings.ScrollbarOpacity.ThumbHovered, 0.0f, 1.0f, "%.2f", T("menu.settings.thumb_hovered_opacity_tooltip", "Controls the opacity of the scrollbar thumb when hovered."));
			Util::SliderFloatTip(T("menu.settings.thumb_active_opacity", "Thumb Active Opacity"), &themeSettings.ScrollbarOpacity.ThumbActive, 0.0f, 1.0f, "%.2f", T("menu.settings.thumb_active_opacity_tooltip", "Controls the opacity of the scrollbar thumb when being dragged."));
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
			Util::HoverTip(T("menu.settings.button_text_align_tooltip", "Alignment applies when a button is larger than its text content."));
			ImGui::SliderFloat2(T("menu.settings.selectable_text_align", "Selectable Text Align"), (float*)&style.SelectableTextAlign, 0.0f, 1.0f, "%.2f");
			Util::HoverTip(T("menu.settings.selectable_text_align_tooltip", "Alignment applies when a selectable is larger than its text content."));
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
		case SurfaceRole::OptionCard:
			return T("menu.settings.surface_option_card", "Option Cards");
		case SurfaceRole::SectionCard:
			return T("menu.settings.surface_section_card", "Section Cards");
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
		case SurfaceRole::OptionCard:
			return T("menu.settings.surface_option_card_tooltip", "Clickable cards that pick a tool or mode, such as the tools on the Developer Tools page. The selected card takes the accent colour as its border.");
		case SurfaceRole::SectionCard:
			return T("menu.settings.surface_section_card_tooltip", "Cards that group related settings on a page, such as Shaders & Cache.");
		default:
			return "";
		}
	}

	/** @brief Draws a small sample of a surface role, so edits are visible next to the controls. */
	void DrawSurfacePreview(SurfaceRole role, const Util::Surface::RoleSettings& s)
	{
		const float scale = Util::GetUIScale();
		const float margin = ThemeManager::Constants::SURFACE_PREVIEW_MARGIN * scale;
		const float height = ImGui::GetTextLineHeightWithSpacing() * 2.0f;
		const float width = std::min(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * ThemeManager::Constants::SURFACE_PREVIEW_MAX_WIDTH_FONT_SIZES);
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

	/** @brief The controls of one surface role. */
	void DrawSurfaceRoleControls(SurfaceRole role, Util::Surface::RoleSettings& s)
	{
		Util::CheckboxTip(T("menu.settings.surface_enabled", "Enabled"), &s.enabled, T("menu.settings.surface_enabled_tooltip", "Draw this surface. When off, the content still shows, just without the backing."));
		DrawSurfacePreview(role, s);

		Util::DisableGuard disabled(!s.enabled);

		if (role == SurfaceRole::NavSelected) {
			Util::SliderFloatTip(T("menu.settings.surface_nav_fill_opacity", "Fill Opacity"), &s.fillAlpha, 0.0f, 1.0f, "%.2f", T("menu.settings.surface_nav_fill_opacity_tooltip", "Opacity of the accent colour fill behind the selected row."));
			Util::CheckboxTip(T("menu.settings.surface_accent_bar", "Accent Bar"), &s.accentBar, T("menu.settings.surface_accent_bar_tooltip", "Draw a short accent bar on the left edge of the selected row."));
			{
				Util::DisableGuard barDisabled(!s.accentBar);
				Util::SliderFloatTip(T("menu.settings.surface_accent_bar_width", "Accent Bar Width"), &s.accentBarWidth, 0.0f, 12.0f, "%.1f", T("menu.settings.surface_accent_bar_width_tooltip", "Width of the accent bar in pixels at 1080p."));
			}
			return;
		}

		if (role == SurfaceRole::HomeCard) {
			Util::SliderFloatTip(T("menu.settings.surface_padding", "Inner Padding"), &s.padding, 0.0f, 48.0f, "%.0f", T("menu.settings.surface_padding_tooltip", "Space between the card edge and its content, in pixels at 1080p. Applies even when the surface is disabled."));
		}

		SeparatorTextWithFont(T("menu.settings.surface_fill", "Fill"), Menu::FontRole::Subheading);
		Util::SliderFloatTip(T("menu.settings.surface_fill_lift", "Fill Lift"), &s.fillLift, 0.0f, 0.5f, "%.3f", T("menu.settings.surface_fill_lift_tooltip", "How far the window colour is lightened toward white to make the surface read above the page."));
		Util::SliderFloatTip(T("menu.settings.surface_fill_opacity", "Fill Opacity"), &s.fillAlpha, 0.0f, 1.0f, "%.2f", T("menu.settings.surface_fill_opacity_tooltip", "Multiplier on the fill opacity."));

		bool useStyleRounding = s.rounding < 0.0f;
		if (Util::CheckboxTip(T("menu.settings.surface_use_style_rounding", "Use Style Rounding"), &useStyleRounding, T("menu.settings.surface_use_style_rounding_tooltip", "Follow the corner rounding from the Styling section instead of a custom radius.")))
			s.rounding = useStyleRounding ? -1.0f : std::clamp(ImGui::GetStyle().ChildRounding, 0.0f, 24.0f);
		if (!useStyleRounding)
			Util::SliderFloatTip(T("menu.settings.surface_rounding", "Corner Radius"), &s.rounding, 0.0f, 24.0f, "%.0f", T("menu.settings.surface_rounding_tooltip", "Corner radius in pixels at 1080p."));

		SeparatorTextWithFont(T("menu.settings.surface_border", "Border"), Menu::FontRole::Subheading);
		Util::CheckboxTip(T("menu.settings.surface_border_enabled", "Border"), &s.border, T("menu.settings.surface_border_enabled_tooltip", "Draw a hairline border around the surface."));
		{
			Util::DisableGuard borderDisabled(!s.border);
			Util::SliderFloatTip(T("menu.settings.surface_border_opacity", "Border Opacity"), &s.borderAlpha, 0.0f, 1.0f, "%.2f", T("menu.settings.surface_border_opacity_tooltip", "Opacity of the default white hairline."));
			Util::CheckboxTip(T("menu.settings.surface_border_use_color", "Custom Border Colour"), &s.useBorderColor, T("menu.settings.surface_border_use_color_tooltip", "Use your own border colour instead of the white hairline. Cards that signal a status keep their status colour."));
			if (s.useBorderColor) {
				ImGui::ColorEdit4(T("menu.settings.surface_border_color", "Border Colour"), reinterpret_cast<float*>(&s.borderColor));
				Util::HoverTip(T("menu.settings.surface_border_color_tooltip", "Colour and opacity of the border."));
			}
		}

		SeparatorTextWithFont(T("menu.settings.surface_shadow", "Shadow"), Menu::FontRole::Subheading);
		Util::CheckboxTip(T("menu.settings.surface_shadow_enabled", "Shadow"), &s.shadow, T("menu.settings.surface_shadow_enabled_tooltip", "Draw a soft drop shadow behind the surface."));
		{
			Util::DisableGuard shadowDisabled(!s.shadow);
			Util::SliderFloatTip(T("menu.settings.surface_shadow_spread", "Shadow Spread"), &s.shadowSpread, 0.0f, 32.0f, "%.0f", T("menu.settings.surface_shadow_spread_tooltip", "How far the shadow grows past the surface edge, in pixels at 1080p."));
			Util::SliderFloatTip(T("menu.settings.surface_shadow_opacity", "Shadow Opacity"), &s.shadowAlpha, 0.0f, 1.0f, "%.2f", T("menu.settings.surface_shadow_opacity_tooltip", "Opacity of the black shadow."));
			constexpr float offsetMax = ThemeManager::Constants::SURFACE_SHADOW_OFFSET_MAX;
			Util::SliderFloatTip(T("menu.settings.surface_shadow_offset_x", "Shadow Shift"), &s.shadowOffsetX, -offsetMax, offsetMax, "%.0f", T("menu.settings.surface_shadow_offset_x_tooltip", "Horizontal shadow offset in pixels at 1080p. Positive values move it right of the surface."));
			Util::SliderFloatTip(T("menu.settings.surface_shadow_offset", "Shadow Drop"), &s.shadowOffsetY, -offsetMax, offsetMax, "%.0f", T("menu.settings.surface_shadow_offset_tooltip", "Vertical shadow offset in pixels at 1080p. Positive values drop it below the surface."));
		}
	}
}

void SettingsTabRenderer::RenderSurfacesSection()
{
	auto& surfaces = globals::menu->GetSettings().Theme.Surfaces;

	ImGui::TextWrapped("%s", T("menu.settings.surfaces_intro", "Backing, border and shadow of the menu cards and highlights. Changes apply immediately and are saved with the theme."));
	if (Util::Button(T("menu.settings.reset_surfaces", "Reset surfaces to theme defaults")))
		surfaces = Util::Surface::Settings{};
	Util::HoverTip(T("menu.settings.reset_surfaces_tooltip", "Restores every surface below to its built-in look. Save the theme to keep it."));

	for (size_t i = 0; i < static_cast<size_t>(SurfaceRole::Count); ++i) {
		const auto role = static_cast<SurfaceRole>(i);
		ImGui::PushID(Util::Surface::RoleKey(role));
		const auto label = std::format("{}###Surface{}", GetSurfaceRoleTitle(role), Util::Surface::RoleKey(role));
		const bool open = ImGui::CollapsingHeader(label.c_str(), role == SurfaceRole::HomeCard ? ImGuiTreeNodeFlags_DefaultOpen : 0);
		Util::HoverTip(GetSurfaceRoleHint(role));
		if (open)
			DrawSurfaceRoleControls(role, surfaces.roles[i]);
		ImGui::PopID();
	}
	surfaces.Sanitize();
}

void SettingsTabRenderer::RenderColorsSection()
{
	auto* menu = globals::menu;
	auto& themeSettings = menu->GetSettings().Theme;
	auto& pageState = GetColorsPageState();
	// Watched before anything draws, so a picker drag, a reset and Reset All each undo as one step
	menu->WatchUndoTarget(MakeThemeColorsUndoTarget());
	const auto& reference = GetThemeFileReference();

	// The colour groups. Base colours also fill their Full Palette slots, so those slots are left out below.
	const std::vector<ColorEntry> baseColors = {
		{ T("menu.settings.color_background", "Background"), &themeSettings.Palette.Background.x, &reference.Palette.Background.x },
		{ T("menu.settings.color_text", "Text"), &themeSettings.Palette.Text.x, &reference.Palette.Text.x },
		{ T("menu.settings.color_window_border", "Window Border"), &themeSettings.Palette.WindowBorder.x, &reference.Palette.WindowBorder.x },
		{ T("menu.settings.color_slider_input_bg", "Slider & Input Background"), &themeSettings.Palette.FrameBorder.x, &reference.Palette.FrameBorder.x },
		{ T("menu.settings.color_separator_line", "Separator Line"), &themeSettings.Palette.Separator.x, &reference.Palette.Separator.x },
		{ T("menu.settings.color_resize_grip", "Resize Grip"), &themeSettings.Palette.ResizeGrip.x, &reference.Palette.ResizeGrip.x },
	};
	auto& status = themeSettings.StatusPalette;
	const std::vector<ColorEntry> statusColors = {
		{ T("menu.settings.color_success", "Success"), &status.SuccessColor.x, &reference.StatusPalette.SuccessColor.x },
		{ T("menu.settings.color_info", "Info"), &status.InfoColor.x, &reference.StatusPalette.InfoColor.x },
		{ T("menu.settings.color_warning", "Warning"), &status.Warning.x, &reference.StatusPalette.Warning.x },
		{ T("menu.settings.color_error", "Error"), &status.Error.x, &reference.StatusPalette.Error.x },
		{ T("menu.settings.color_restart_needed", "Restart Needed"), &status.RestartNeeded.x, &reference.StatusPalette.RestartNeeded.x },
		{ T("menu.settings.color_disabled", "Disabled"), &status.Disable.x, &reference.StatusPalette.Disable.x },
		{ T("menu.settings.color_current_hotkey", "Current Hotkey"), &status.CurrentHotkey.x, &reference.StatusPalette.CurrentHotkey.x },
	};
	const std::vector<ColorEntry> headingColors = {
		{ T("menu.settings.color_default", "Default"), &themeSettings.FeatureHeading.ColorDefault.x, &reference.FeatureHeading.ColorDefault.x },
		{ T("menu.settings.color_hovered", "Hovered"), &themeSettings.FeatureHeading.ColorHovered.x, &reference.FeatureHeading.ColorHovered.x },
	};
	std::array<std::vector<ColorEntry>, static_cast<size_t>(PaletteGroup::Count)> paletteGroups;
	for (int i = 0; i < ImGuiCol_COUNT; ++i) {
		if (IsPaletteSlotDerived(i))
			continue;
		ColorEntry entry{ GetFriendlyColorName(i), &themeSettings.FullPalette[i].x, &reference.FullPalette[i].x };
		// The scrollbar's opacities come from Styling, so only the colour is edited here
		if (i == ImGuiCol_ScrollbarBg || i == ImGuiCol_ScrollbarGrab || i == ImGuiCol_ScrollbarGrabHovered || i == ImGuiCol_ScrollbarGrabActive) {
			entry.flags = ImGuiColorEditFlags_NoAlpha;
			entry.note = T("menu.settings.color_scrollbar_opacity_note", "Opacity is set under Styling > Scrollbar.");
		}
		paletteGroups[static_cast<size_t>(GetPaletteGroup(i))].push_back(entry);
	}

	// Count what differs from the theme file, for the toolbar
	int changed = 0;
	const auto countChanged = [&](const std::vector<ColorEntry>& entries) {
		for (const auto& entry : entries)
			changed += ColorDiffers(entry.value, entry.reference, (entry.flags & ImGuiColorEditFlags_NoAlpha) ? 3 : 4) ? 1 : 0;
	};
	countChanged(baseColors);
	countChanged(statusColors);
	countChanged(headingColors);
	for (const auto& group : paletteGroups)
		countChanged(group);
	const bool minimizedChanged = std::abs(themeSettings.FeatureHeading.MinimizedFactor - reference.FeatureHeading.MinimizedFactor) > 0.001f;
	changed += minimizedChanged ? 1 : 0;

	// Toolbar: search, Changed only, then undo / redo / reset on the right
	{
		const float scale = Util::GetSearchUIScale();
		const float iconSize = ThemeManager::Constants::SEARCH_ICON_SIZE * scale;
		const float iconSpace = iconSize + ThemeManager::Constants::SEARCH_INPUT_PADDING_EXTRA * scale;
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(iconSpace, ThemeManager::Constants::SEARCH_INPUT_FRAME_PADDING_Y * scale));
		pageState.filter.Draw(T("menu.settings.filter_colors", "Filter colors"), ImGui::GetFontSize() * 14.0f);
		ImGui::PopStyleVar();
		const ImVec2 filterMin = ImGui::GetItemRectMin();
		const ImVec2 filterSize = ImGui::GetItemRectSize();
		ImVec4 iconColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
		iconColor.w *= ThemeManager::Constants::SEARCH_ICON_ALPHA;
		Icons::DrawCenteredGlyph(ImGui::GetWindowDrawList(), ImVec2(filterMin.x + ThemeManager::Constants::SEARCH_ICON_OFFSET_X * scale, filterMin.y),
			ImVec2(iconSize, filterSize.y), Icons::TI(MenuIcons::kSearch), ImGui::GetColorU32(iconColor));

		ImGui::SameLine();
		Util::CheckboxTip(T("menu.settings.colors_changed_only", "Changed only"), &pageState.changedOnly,
			T("menu.settings.colors_changed_only_tooltip", "List only the colours that differ from the selected theme's file."));

		const char* undoLabel = T("menu.settings.colors_undo", "Undo");
		const char* redoLabel = T("menu.settings.colors_redo", "Redo");
		const char* resetLabel = T("menu.settings.colors_reset_all", "Reset All");
		const float buttonsWidth = Icons::MeasureLabeledButton(Icons::TI(ICON_TI_ARROW_BACK_UP), undoLabel) +
		                           Icons::MeasureLabeledButton(Icons::TI(ICON_TI_ARROW_FORWARD_UP), redoLabel) +
		                           Icons::MeasureLabeledButton(Icons::TI(MenuIcons::kRestore), resetLabel) + ImGui::GetStyle().ItemSpacing.x * 2.0f;
		ImGui::SameLine();
		ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - buttonsWidth));
		{
			Util::DisableGuard noUndo(!menu->CanUndo());
			if (Icons::LabeledButton("##ColorsUndo", Icons::TI(ICON_TI_ARROW_BACK_UP), undoLabel))
				menu->RequestUndo();
		}
		Util::AddTooltip(T("menu.settings.colors_undo_tooltip", "Undo the last change (Ctrl+Z)."), Util::kTooltipWhenDisabled);
		ImGui::SameLine();
		{
			Util::DisableGuard noRedo(!menu->CanRedo());
			if (Icons::LabeledButton("##ColorsRedo", Icons::TI(ICON_TI_ARROW_FORWARD_UP), redoLabel))
				menu->RequestRedo();
		}
		Util::AddTooltip(T("menu.settings.colors_redo_tooltip", "Redo the last undone change (Ctrl+Y)."), Util::kTooltipWhenDisabled);
		ImGui::SameLine();
		{
			Util::DisableGuard nothingChanged(changed == 0);
			if (Icons::LabeledButton("##ColorsResetAll", Icons::TI(MenuIcons::kRestore), resetLabel)) {
				// Copies the theme file's colours back; one undo step undoes it
				std::vector<float> values;
				ForEachColorValue(reference, [&](const float* data, int count) { values.insert(values.end(), data, data + count); });
				size_t i = 0;
				ForEachColorValue(themeSettings, [&](float* data, int count) {
					for (int c = 0; c < count; ++c)
						data[c] = values[i++];
				});
			}
		}
		Util::AddTooltip(T("menu.settings.colors_reset_all_tooltip", "Put every colour back to the selected theme's file. Undo brings your edits back."), Util::kTooltipWhenDisabled);

		MenuFonts::FontRoleGuard subtext(Menu::FontRole::Subtext);
		if (changed > 0)
			ImGui::TextColored(Util::Colors::GetAccent(), "%s",
				I18n::GetSingleton()->Format("menu.settings.colors_changed_count", { { "count", std::to_string(changed) } }, "{count} changed from the theme file").c_str());
		else
			ImGui::TextDisabled("%s", T("menu.settings.colors_none_changed", "Matches the theme file"));
	}
	ImGui::Spacing();

	DrawColorCard("##ColorsBase", MenuIcons::kPresets, T("menu.settings.colors_card_base", "Base"), baseColors);
	DrawColorCard("##ColorsStatus", ICON_TI_ALERT_TRIANGLE, T("menu.settings.status", "Status"), statusColors);

	const bool minimizedShown = pageState.filter.PassFilter("Minimized Transparency") && (!pageState.changedOnly || minimizedChanged);
	DrawColorCard("##ColorsHeadings", ICON_TI_LAYOUT_COLUMNS, T("menu.settings.feature_headings", "Feature Headings"), headingColors,
		minimizedShown ? std::function<void()>([&] {
			ImGui::SliderFloat(T("menu.settings.color_minimized_transparency", "Minimized Transparency"), &themeSettings.FeatureHeading.MinimizedFactor, 0.0f, 1.0f, "%.2f");
			if (minimizedChanged) {
				ImGui::SameLine();
				if (BrowserUI::IconButton("##ResetMinimized", Icons::TI(MenuIcons::kRestore), T("menu.settings.color_reset_tooltip", "Reset to the theme's colour")))
					themeSettings.FeatureHeading.MinimizedFactor = reference.FeatureHeading.MinimizedFactor;
			}
		}) :
						 std::function<void()>{});

	// Full Palette: every other ImGui colour, one card per kind of widget
	{
		MenuFonts::FontRoleGuard heading(Menu::FontRole::Subheading);
		ImGui::SeparatorText(T("menu.settings.full_palette", "Full Palette"));
	}
	Util::SettingHint(T("menu.settings.full_palette_tooltip", "Advanced color controls for detailed customization of all UI elements."), false);
	ImGui::Spacing();
	for (size_t g = 0; g < paletteGroups.size(); ++g) {
		const auto group = static_cast<PaletteGroup>(g);
		const std::string id = std::format("##Palette{}", g);
		DrawColorCard(id.c_str(), nullptr, GetPaletteGroupTitle(group), paletteGroups[g]);
	}
}
