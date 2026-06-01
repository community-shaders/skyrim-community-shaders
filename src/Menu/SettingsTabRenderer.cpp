#include "SettingsTabRenderer.h"

#include <set>
#include <string>
#include <windows.h>

#include "BackgroundBlur.h"
#include "Features/ScreenshotFeature.h"
#include "Features/VR.h"
#include "Fonts.h"
#include "Globals.h"
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

	// Convert ImGui internal color names to user-friendly display names
	const char* GetFriendlyColorName(int colorIndex)
	{
		switch (colorIndex) {
		case ImGuiCol_Text:
			return "Text";
		case ImGuiCol_TextDisabled:
			return "Text (Disabled)";
		case ImGuiCol_WindowBg:
			return "Window Background";
		case ImGuiCol_ChildBg:
			return "Child Window Background";
		case ImGuiCol_PopupBg:
			return "Popup Background";
		case ImGuiCol_Border:
			return "Border";
		case ImGuiCol_BorderShadow:
			return "Border Shadow";
		case ImGuiCol_FrameBg:
			return "Frame Background";
		case ImGuiCol_FrameBgHovered:
			return "Frame Background (Hovered)";
		case ImGuiCol_FrameBgActive:
			return "Frame Background (Active)";
		case ImGuiCol_TitleBg:
			return "Title Bar Background";
		case ImGuiCol_TitleBgActive:
			return "Title Bar Background (Active)";
		case ImGuiCol_TitleBgCollapsed:
			return "Title Bar Background (Collapsed)";
		case ImGuiCol_MenuBarBg:
			return "Menu Bar Background";
		case ImGuiCol_ScrollbarBg:
			return "Scrollbar Background";
		case ImGuiCol_ScrollbarGrab:
			return "Scrollbar Grab";
		case ImGuiCol_ScrollbarGrabHovered:
			return "Scrollbar Grab (Hovered)";
		case ImGuiCol_ScrollbarGrabActive:
			return "Scrollbar Grab (Active)";
		case ImGuiCol_CheckMark:
			return "Checkbox Checkmark";
		case ImGuiCol_SliderGrab:
			return "Slider Grab";
		case ImGuiCol_SliderGrabActive:
			return "Slider Grab (Active)";
		case ImGuiCol_Button:
			return "Button";
		case ImGuiCol_ButtonHovered:
			return "Button (Hovered)";
		case ImGuiCol_ButtonActive:
			return "Button (Active)";
		case ImGuiCol_Header:
			return "Header";
		case ImGuiCol_HeaderHovered:
			return "Header (Hovered)";
		case ImGuiCol_HeaderActive:
			return "Header (Active)";
		case ImGuiCol_Separator:
			return "Separator";
		case ImGuiCol_SeparatorHovered:
			return "Separator (Hovered)";
		case ImGuiCol_SeparatorActive:
			return "Separator (Active)";
		case ImGuiCol_ResizeGrip:
			return "Resize Grip";
		case ImGuiCol_ResizeGripHovered:
			return "Resize Grip (Hovered)";
		case ImGuiCol_ResizeGripActive:
			return "Resize Grip (Active)";
		case ImGuiCol_InputTextCursor:
			return "Input Text Cursor";
		case ImGuiCol_Tab:
			return "Tab";
		case ImGuiCol_TabHovered:
			return "Tab (Hovered)";
		case ImGuiCol_TabSelected:
			return "Tab (Selected)";
		case ImGuiCol_TabSelectedOverline:
			return "Tab Selected Overline";
		case ImGuiCol_TabDimmed:
			return "Tab (Dimmed)";
		case ImGuiCol_TabDimmedSelected:
			return "Tab (Dimmed Selected)";
		case ImGuiCol_TabDimmedSelectedOverline:
			return "Tab Dimmed Selected Overline";
		case ImGuiCol_DockingPreview:
			return "Docking Preview";
		case ImGuiCol_DockingEmptyBg:
			return "Docking Empty Background";
		case ImGuiCol_PlotLines:
			return "Plot Lines";
		case ImGuiCol_PlotLinesHovered:
			return "Plot Lines (Hovered)";
		case ImGuiCol_PlotHistogram:
			return "Plot Histogram";
		case ImGuiCol_PlotHistogramHovered:
			return "Plot Histogram (Hovered)";
		case ImGuiCol_TableHeaderBg:
			return "Table Header Background";
		case ImGuiCol_TableBorderStrong:
			return "Table Border (Strong)";
		case ImGuiCol_TableBorderLight:
			return "Table Border (Light)";
		case ImGuiCol_TableRowBg:
			return "Table Row Background";
		case ImGuiCol_TableRowBgAlt:
			return "Table Row Background (Alternate)";
		case ImGuiCol_TextLink:
			return "Text Link";
		case ImGuiCol_TextSelectedBg:
			return "Text Selection Background";
		case ImGuiCol_TreeLines:
			return "Tree Lines";
		case ImGuiCol_DragDropTarget:
			return "Drag & Drop Target";
		case ImGuiCol_DragDropTargetBg:
			return "Drag & Drop Target Background";
		case ImGuiCol_UnsavedMarker:
			return "Unsaved Marker";
		case ImGuiCol_NavCursor:
			return "Navigation Cursor";
		case ImGuiCol_NavWindowingHighlight:
			return "Window Navigation Highlight";
		case ImGuiCol_NavWindowingDimBg:
			return "Window Navigation Dim Background";
		case ImGuiCol_ModalWindowDimBg:
			return "Modal Window Dim Background";
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
		ImGui::TextWrapped("Theme changes are not saved with the global \"Save Settings\" button. Use the Themes tab to save changes to this theme.");
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
	if (BeginTabItemWithFont("Shaders", Menu::FontRole::Heading)) {
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
				"avoiding the full startup compilation cost. "
				"Useful for iterative testing: change a shader file and only that shader is rebuilt. "
				"Requires 'Enable Disk Cache' to be active.");
		}

		bool useAsync = shaderCache->IsAsync();
		if (ImGui::Checkbox("Enable Async", &useAsync)) {
			shaderCache->SetAsync(useAsync);
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Skips a shader being replaced if it hasn't been compiled yet. Also makes compilation blazingly fast!");
		}

		// Skip confirmation when clearing shader cache
		auto& menuSettings = globals::menu->GetSettings();
		bool skipConfirmation = menuSettings.SkipClearCacheConfirmation;
		if (ImGui::Checkbox("Skip Clear Cache Dialogue", &skipConfirmation)) {
			menuSettings.SkipClearCacheConfirmation = skipConfirmation;
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("When checked, the shader cache will be cleared immediately without asking for confirmation.");
		}

		if (shaderCache->GetTotalTasks() > 0) {
			ImGui::Text("Last shader cache build duration: %s",
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
					{ cacheHits, IM_COL32(120, 120, 120, 255), "Deduplicated" },
					{ diskHits, IM_COL32(70, 130, 200, 255), "Disk cache" },
					{ fast, IM_COL32(80, 180, 80, 255), "Fast (<2s)" },
					{ medium, IM_COL32(220, 180, 50, 255), "Slow (2-8s)" },
					{ verySlow, IM_COL32(220, 60, 60, 255), "Very slow (>=8s)" },
					{ failed, IM_COL32(160, 30, 30, 255), "Failed" },
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
	if (BeginTabItemWithFont("Keybindings", Menu::FontRole::Heading)) {
		auto& settings = globals::menu->GetSettings();

		Util::InputComboWidget(
			"Toggle Key:",
			settings.ToggleKey,
			state.settingToggleKey,
			"Change##toggle");

		Util::InputComboWidget(
			"Effect Toggle Key:",
			settings.EffectToggleKey,
			state.settingsEffectsToggle,
			"Change##EffectToggle");

		Util::InputComboWidget(
			"Skip Compilation Key:",
			settings.SkipCompilationKey,
			state.settingSkipCompilationKey,
			"Change##skip");

		Util::InputComboWidget(
			"Overlay Toggle Key:",
			settings.OverlayToggleKey,
			state.settingOverlayToggleKey,
			"Change##OverlayToggle");

		Util::InputComboWidget(
			"CS Editor Toggle Key:",
			settings.CSEditorToggleKey,
			state.settingCSEditorToggleKey,
			"Change##CSEditorToggle");

		Util::InputComboWidget(
			"Screenshot Key:",
			settings.ScreenshotKey,
			state.settingScreenshotKey,
			"Change##Screenshot");

		ImGui::EndTabItem();
	}
}

void SettingsTabRenderer::RenderInterfaceTab()
{
	if (BeginTabItemWithFont("Interface", Menu::FontRole::Heading)) {
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
	if (BeginTabItemWithFont("Behavior", Menu::FontRole::Heading)) {
		auto& themeSettings = globals::menu->GetSettings().Theme;
		RenderSaveInfoText();

		SeparatorTextWithFont("UI Behavior", Menu::FontRole::Subheading);

		ImGui::Checkbox("Show Icon Buttons in Header", &themeSettings.ShowActionIcons);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"When enabled: Shows action buttons (Save, Load, Clear Cache) as icons in the header\n"
				"When disabled: Shows as text buttons below the header");
		}

		if (themeSettings.ShowActionIcons) {
			ImGui::Indent();
			if (ImGui::Checkbox("Use Monochrome Icons", &themeSettings.UseMonochromeIcons)) {
				globals::menu->pendingIconReload = true;
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Uses white monochrome icons that adapt to your theme's text color");
			}
			ImGui::SameLine();
			if (ImGui::Checkbox("Use Monochrome CS Logo", &themeSettings.UseMonochromeLogo)) {
				globals::menu->pendingIconReload = true;
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Uses monochrome version of the Community Shaders logo");
			}
			ImGui::Unindent();
		}

		ImGui::Checkbox("Show Footer", &themeSettings.ShowFooter);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Shows the footer with game version, swap chain, and GPU information at the bottom of the window");
		}

		ImGui::Checkbox("Center Header Title", &themeSettings.CenterHeader);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Centers the Community Shaders title and logo in the header title bar");
		}

		ImGui::Checkbox("Auto-hide Feature List", &globals::menu->GetSettings().AutoHideFeatureList);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Automatically hides the left feature list panel. Move cursor to the left edge to show it.");
		}

		if (ImGui::Checkbox("Require Shift to Dock", &globals::menu->GetSettings().RequireShiftToDock)) {
			ImGui::GetIO().ConfigDockingWithShift = globals::menu->GetSettings().RequireShiftToDock;
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("When enabled, you must hold Shift while dragging to dock/snap windows. Prevents accidental docking.");
		}

		ImGui::SliderFloat("Tooltip Hover Delay", &themeSettings.TooltipHoverDelay, 0.0f, 2.0f, "%.2f s", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Time in seconds to wait before a tooltip appears when hovering over an item.");
		}

		SeparatorTextWithFont("Visual Effects", Menu::FontRole::Subheading);

		if (ImGui::Checkbox("Background Blur", &themeSettings.BackgroundBlurEnabled)) {
			BackgroundBlur::SetEnabled(themeSettings.BackgroundBlurEnabled);
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Applies a blur effect to the background behind the menu window.");
		}

		ImGui::EndTabItem();
	}
}

void SettingsTabRenderer::RenderThemesTab()
{
	if (BeginTabItemWithFont("Themes", Menu::FontRole::Heading)) {
		auto& themeSettings = globals::menu->GetSettings().Theme;

		// Static variables for popup state and new theme creation
		static Util::ConfirmationPopup deleteThemePopup("Delete Theme", "", "Delete", "Cancel");
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
		SeparatorTextWithFont("Theme Preset", Menu::FontRole::Subheading);

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

		if (ImGui::Button("Refresh")) {
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
		if (ImGui::Button("Open Themes Folder")) {
			std::filesystem::path themesPath = Util::PathHelpers::GetThemesRealPath();
			ShellExecuteA(NULL, "open", themesPath.string().c_str(), NULL, NULL, SW_SHOWNORMAL);
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Opens the Themes folder where you can add custom theme files.");
		}

		ImGui::Spacing();
		ImGui::PushStyleColor(ImGuiCol_Text, themeSettings.StatusPalette.InfoColor);
		ImGui::TextWrapped("If you changed the theme above, save your selection using the global \"Save Settings\" button.");
		ImGui::PopStyleColor();

		// Selected theme section: name + description
		ImGui::Spacing();
		ImGui::Separator();
		if (currentItem >= 0 && currentItem < static_cast<int>(themes.size())) {
			ImGui::Spacing();
			const auto& selectedTheme = themes[currentItem];
			ImGui::Text("Selected Theme: ");
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
			if (Util::ButtonWithFlash("Save")) {
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
				ImGui::Text("Updates the currently selected theme (%s) with your current settings", currentThemePreset.c_str());
			}

			ImGui::SameLine();
		}

		if (Util::ButtonWithFlash("Save As New Theme")) {
			showCreateThemePopup = true;
			memset(newThemeName, 0, sizeof(newThemeName));
			memset(newThemeDisplayName, 0, sizeof(newThemeDisplayName));
			memset(newThemeDescription, 0, sizeof(newThemeDescription));
			showValidationError = false;
		}

		if (!isPreset && currentThemeInfo && !currentThemeInfo->filePath.empty()) {
			ImGui::SameLine();
			if (Util::ErrorButtonWithFlash("Delete")) {
				deleteThemePopup.message =
					"Are you sure you want to delete the theme '" +
					(currentThemeInfo->displayName.empty() ? currentThemePreset : currentThemeInfo->displayName) +
					"'?\n\nThis will permanently remove the theme file. This cannot be undone.";
				deleteThemePopup.Request();
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Delete the theme file for '%s'. This cannot be undone.",
					(currentThemeInfo->displayName.empty() ? currentThemePreset : currentThemeInfo->displayName).c_str());
			}
		}

		// Display update feedback below the buttons
		if (showUpdateFeedback) {
			ImGui::Spacing();
			ImGui::Separator();

			if (updateSuccess) {
				if (changedSettings.empty()) {
					ImGui::TextColored(themeSettings.StatusPalette.SuccessColor, "Theme updated successfully - no changes detected");
				} else {
					ImGui::TextColored(themeSettings.StatusPalette.SuccessColor, "Theme updated successfully! Changed settings:");
					ImGui::Indent();
					for (const auto& change : changedSettings) {
						ImGui::BulletText("%s: %s -> %s", change.path.c_str(), change.oldValue.c_str(), change.newValue.c_str());
					}
					ImGui::Unindent();
				}
			} else {
				ImGui::TextColored(themeSettings.StatusPalette.Error, "Failed to update theme");
			}

			ImGui::Separator();
		}

		// Create Theme Popup
		if (showCreateThemePopup) {
			ImGui::OpenPopup("Create New Theme");
		}

		// Popup modal for creating new theme
		if (auto popup = Util::CenteredPopupModal("Create New Theme", &showCreateThemePopup)) {
			ImGui::Text("Create a new theme with your current settings:");
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

			ImGui::InputText("Theme Name", newThemeName, sizeof(newThemeName));

			if (isThemeNameError && showValidationError) {
				ImGui::PopStyleVar();
				ImGui::PopStyleColor();
			}

			// Show inline error message
			if (showValidationError) {
				if (isThemeNameEmpty) {
					ImGui::TextColored(themeSettings.StatusPalette.Error, "Theme name is required");
				} else if (isDuplicateName) {
					ImGui::TextColored(themeSettings.StatusPalette.Error, "A theme with this name already exists");
				}
			}

			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("File name for the theme (without .json extension)");
			}

			// Highlight the input field if invalid and validation error is shown
			if (isDuplicateDisplayName && showValidationError) {
				ImGui::PushStyleColor(ImGuiCol_Border, themeSettings.StatusPalette.Error);
				ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
			}

			ImGui::InputText("Display Name", newThemeDisplayName, sizeof(newThemeDisplayName));

			if (isDuplicateDisplayName && showValidationError) {
				ImGui::PopStyleVar();
				ImGui::PopStyleColor();
				ImGui::TextColored(themeSettings.StatusPalette.Error, "A theme with this display name already exists");
			}

			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Human-readable name shown in the dropdown");
			}

			{
				float scale = Util::GetUIScale();
				ImGui::InputTextMultiline("Description", newThemeDescription, sizeof(newThemeDescription), ImVec2(400 * scale, 80 * scale));
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Optional description for the theme");
			}

			ImGui::Separator();

			// Buttons
			if (Util::ButtonWithFlash("Create Theme")) {
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
			if (ImGui::Button("Cancel")) {
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
	if (BeginTabItemWithFont("Fonts", Menu::FontRole::Heading)) {
		auto* menuInstance = globals::menu;
		auto& themeSettings = menuInstance->GetSettings().Theme;
		RenderSaveInfoText();

		SeparatorTextWithFont("Font", Menu::FontRole::Subheading);

		bool& useAutoFont = menuInstance->GetSettings().UseResolutionFont;
		if (ImGui::Checkbox("Use resolution-based font size", &useAutoFont)) {
			if (!useAutoFont) {
				// Seed the fixed-size slider with the current effective size so it doesn't jump
				float effective = ThemeManager::ResolveFontSize(*menuInstance);
				themeSettings.FontSize = std::clamp(effective, ThemeManager::Constants::MIN_FONT_SIZE, ThemeManager::Constants::MAX_FONT_SIZE);
			}
			menuInstance->pendingFontReload = true;
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("When enabled, the UI font size scales with your screen resolution. Disable to set a fixed size.");
		}

		ImGui::BeginDisabled(useAutoFont);
		if (ImGui::SliderFloat("Base Font Size", &themeSettings.FontSize, ThemeManager::Constants::MIN_FONT_SIZE, ThemeManager::Constants::MAX_FONT_SIZE, "%.0f")) {
			menuInstance->pendingFontReload = true;
		}
		ImGui::EndDisabled();

		float effectiveNow = ThemeManager::ResolveFontSize(*menuInstance);
		ImGui::Text("Effective size: %.0f px", std::round(effectiveNow));

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
		SeparatorTextWithFont("Font Roles", Menu::FontRole::Subheading);

		if (fontCatalog.families.empty()) {
			ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "No fonts found. Place .ttf files in Interface/CommunityShaders/Fonts/");
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

			const char* familyPreview = fontCatalog.families.empty() ? "No families" : fontCatalog.families[familyIndex].displayName.c_str();
			std::string familyLabel = std::format("{} Family##{}", descriptor.displayName, roleIndex);
			{
				FontRoleGuard familyComboFont(Menu::FontRole::Body);
				if (ImGui::BeginCombo(familyLabel.c_str(), familyPreview)) {
					if (fontCatalog.families.empty()) {
						ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No font families available");
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
				ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "No style variants found for this family.");
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
				const char* stylePreview = selectedFamily->styles.empty() ? "No styles" : selectedFamily->styles[styleIndex].displayName.c_str();
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

			ImGui::TextDisabled("File: %s", roleSettings.File.c_str());

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
				ImGui::SliderFloat("Feature Header Scale", &themeSettings.FeatureHeading.FeatureTitleScale, 1.0f, 3.0f, "%.1fx", ImGuiSliderFlags_AlwaysClamp);
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("Scale multiplier for feature title text in the Settings tab.");
				}
				ImGui::SameLine();
				if (ImGui::Button("Reset##FeatureHeaderScale")) {
					themeSettings.FeatureHeading.FeatureTitleScale = ThemeManager::Constants::DEFAULT_FEATURE_TITLE_SCALE;
				}
			}

			ImGui::Separator();
			ImGui::PopID();
		}

		if (ImGui::Button("Refresh Font Families")) {
			refreshFontCatalog();
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Rescan the Fonts directory after adding or removing font files.");
		}

		ImGui::EndTabItem();
	}
}

void SettingsTabRenderer::RenderStylingTab()
{
	if (BeginTabItemWithFont("Styling", Menu::FontRole::Heading)) {
		auto& themeSettings = globals::menu->GetSettings().Theme;
		auto& style = themeSettings.Style;
		RenderSaveInfoText();

		SeparatorTextWithFont("Main", Menu::FontRole::Subheading);
		if (ImGui::SliderFloat("Global Scale", &themeSettings.GlobalScale, -1.f, 1.f, "%.2f")) {
			float trueScale = exp2(themeSettings.GlobalScale);

			ImGui::GetStyle().FontScaleMain = trueScale;
		}

		SeparatorTextWithFont("Layout", Menu::FontRole::Subheading);

		ImGui::SliderFloat2("Window Padding", (float*)&style.WindowPadding, 0.0f, 20.0f, "%.0f");
		ImGui::SliderFloat2("Frame Padding", (float*)&style.FramePadding, 0.0f, 20.0f, "%.0f");
		ImGui::SliderFloat2("Item Spacing", (float*)&style.ItemSpacing, 0.0f, 20.0f, "%.0f");
		ImGui::SliderFloat2("Item Inner Spacing", (float*)&style.ItemInnerSpacing, 0.0f, 20.0f, "%.0f");
		ImGui::SliderFloat("Indent Spacing", &style.IndentSpacing, 0.0f, 30.0f, "%.0f");
		ImGui::SliderFloat("Scrollbar Size", &style.ScrollbarSize, 1.0f, 20.0f, "%.0f");
		ImGui::SliderFloat("Grab Min Size", &style.GrabMinSize, 1.0f, 20.0f, "%.0f");

		SeparatorTextWithFont("Scrollbar Opacity", Menu::FontRole::Subheading);
		ImGui::SliderFloat("Track Opacity", &themeSettings.ScrollbarOpacity.Background, 0.0f, 1.0f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Controls the opacity of the scrollbar track/channel (the background area behind the scrollbar).");
		ImGui::SliderFloat("Thumb Opacity", &themeSettings.ScrollbarOpacity.Thumb, 0.0f, 1.0f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Controls the opacity of the scrollbar thumb (the draggable part).");
		ImGui::SliderFloat("Thumb Hovered Opacity", &themeSettings.ScrollbarOpacity.ThumbHovered, 0.0f, 1.0f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Controls the opacity of the scrollbar thumb when hovered.");
		ImGui::SliderFloat("Thumb Active Opacity", &themeSettings.ScrollbarOpacity.ThumbActive, 0.0f, 1.0f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Controls the opacity of the scrollbar thumb when being dragged.");

		SeparatorTextWithFont("Borders", Menu::FontRole::Subheading);
		ImGui::SliderFloat("Window Border Size", &style.WindowBorderSize, 0.0f, 5.0f, "%.0f");
		ImGui::SliderFloat("Child Border Size", &style.ChildBorderSize, 0.0f, 5.0f, "%.0f");
		ImGui::SliderFloat("Popup Border Size", &style.PopupBorderSize, 0.0f, 5.0f, "%.0f");
		ImGui::SliderFloat("Frame Border Size", &style.FrameBorderSize, 0.0f, 5.0f, "%.0f");
		ImGui::SliderFloat("Tab Border Size", &style.TabBorderSize, 0.0f, 5.0f, "%.0f");
		ImGui::SliderFloat("Tab Bar Border Size", &style.TabBarBorderSize, 0.0f, 5.0f, "%.0f");

		SeparatorTextWithFont("Rounding", Menu::FontRole::Subheading);
		ImGui::SliderFloat("Window Rounding", &style.WindowRounding, 0.0f, 12.0f, "%.0f");
		ImGui::SliderFloat("Child Rounding", &style.ChildRounding, 0.0f, 12.0f, "%.0f");
		ImGui::SliderFloat("Frame Rounding", &style.FrameRounding, 0.0f, 12.0f, "%.0f");
		ImGui::SliderFloat("Popup Rounding", &style.PopupRounding, 0.0f, 12.0f, "%.0f");
		ImGui::SliderFloat("Scrollbar Rounding", &style.ScrollbarRounding, 0.0f, 12.0f, "%.0f");
		ImGui::SliderFloat("Grab Rounding", &style.GrabRounding, 0.0f, 12.0f, "%.0f");
		ImGui::SliderFloat("Tab Rounding", &style.TabRounding, 0.0f, 12.0f, "%.0f");

		SeparatorTextWithFont("Tables", Menu::FontRole::Subheading);
		ImGui::SliderFloat2("Cell Padding", (float*)&style.CellPadding, 0.0f, 20.0f, "%.0f");
		ImGui::SliderAngle("Table Angled Headers Angle", &style.TableAngledHeadersAngle, -50.0f, +50.0f);

		SeparatorTextWithFont("Widgets", Menu::FontRole::Subheading);
		{
			FontRoleGuard comboFont(Menu::FontRole::Body);
			ImGui::Combo("ColorButtonPosition", (int*)&style.ColorButtonPosition, "Left\0Right\0");
		}
		ImGui::SliderFloat2("Button Text Align", (float*)&style.ButtonTextAlign, 0.0f, 1.0f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Alignment applies when a button is larger than its text content.");
		ImGui::SliderFloat2("Selectable Text Align", (float*)&style.SelectableTextAlign, 0.0f, 1.0f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Alignment applies when a selectable is larger than its text content.");
		ImGui::SliderFloat("Separator Text Border Size", &style.SeparatorTextBorderSize, 0.0f, 10.0f, "%.0f");
		ImGui::SliderFloat2("Separator Text Align", (float*)&style.SeparatorTextAlign, 0.0f, 1.0f, "%.2f");
		ImGui::SliderFloat2("Separator Text Padding", (float*)&style.SeparatorTextPadding, 0.0f, 40.0f, "%.0f");
		ImGui::SliderFloat("Log Slider Deadzone", &style.LogSliderDeadzone, 0.0f, 12.0f, "%.0f");

		SeparatorTextWithFont("Docking", Menu::FontRole::Subheading);
		ImGui::SliderFloat("Docking Splitter Size", &style.DockingSeparatorSize, 0.0f, 12.0f, "%.0f");

		ImGui::EndTabItem();
	}
}

void SettingsTabRenderer::RenderColorsTab()
{
	if (BeginTabItemWithFont("Colors", Menu::FontRole::Heading)) {
		auto& themeSettings = globals::menu->GetSettings().Theme;
		auto& colors = themeSettings.FullPalette;
		RenderSaveInfoText();

		// Color filter at the top with search icon
		static ImGuiTextFilter colorFilter;

		float iconSize = 20.0f;
		float iconSpace = iconSize + 14.0f;
		ImVec2 cursorPos = ImGui::GetCursorScreenPos();
		float availableWidth = ImGui::GetFontSize() * 16;
		float frameHeight = ImGui::GetFrameHeight();

		// Custom style for filter with icon space
		float scale = Util::GetUIScale();
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(iconSpace, 6.0f * scale));
		colorFilter.Draw("Filter colors", availableWidth);
		ImGui::PopStyleVar();

		// Draw search icon
		ImVec2 iconPos = ImVec2(cursorPos.x + 8.0f * scale, cursorPos.y + (frameHeight - iconSize) * 0.5f);
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		ImVec2 center = ImVec2(iconPos.x + iconSize * 0.46f, iconPos.y + iconSize * 0.5f);
		float radius = iconSize * 0.3f;

		auto& palette = globals::menu->GetTheme().Palette;
		ImVec4 iconColor = palette.Text;
		iconColor.w *= 0.7f;
		ImU32 iconColorU32 = ImGui::GetColorU32(iconColor);

		drawList->AddCircle(center, radius, iconColorU32, 12, 2.2f);
		ImVec2 handleStart = ImVec2(center.x + radius * 0.81f, center.y + radius * 0.81f);
		ImVec2 handleEnd = ImVec2(handleStart.x + iconSize * 0.29f, handleStart.y + iconSize * 0.29f);
		drawList->AddLine(handleStart, handleEnd, iconColorU32, 2.1f);

		ImGui::Spacing();

		// Background & Text
		if (colorFilter.PassFilter("Background"))
			ImGui::ColorEdit4("Background", (float*)&themeSettings.Palette.Background);
		if (colorFilter.PassFilter("Text"))
			ImGui::ColorEdit4("Text", (float*)&themeSettings.Palette.Text);

		if (ImGui::TreeNodeEx("Borders & Separators", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (colorFilter.PassFilter("Window Border"))
				ImGui::ColorEdit4("Window Border", (float*)&themeSettings.Palette.WindowBorder);
			if (colorFilter.PassFilter("Slider & Input Background"))
				ImGui::ColorEdit4("Slider & Input Background", (float*)&themeSettings.Palette.FrameBorder);
			if (colorFilter.PassFilter("Separator Line"))
				ImGui::ColorEdit4("Separator Line", (float*)&themeSettings.Palette.Separator);
			if (colorFilter.PassFilter("Resize Grip"))
				ImGui::ColorEdit4("Resize Grip", (float*)&themeSettings.Palette.ResizeGrip);
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Feature Headings", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (colorFilter.PassFilter("Default"))
				ImGui::ColorEdit4("Default", (float*)&themeSettings.FeatureHeading.ColorDefault);
			if (colorFilter.PassFilter("Hovered"))
				ImGui::ColorEdit4("Hovered", (float*)&themeSettings.FeatureHeading.ColorHovered);
			if (colorFilter.PassFilter("Minimized Transparency"))
				ImGui::SliderFloat("Minimized Transparency", &themeSettings.FeatureHeading.MinimizedFactor, 0.0f, 1.0f, "%.2f");
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Status", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (colorFilter.PassFilter("Disabled"))
				ImGui::ColorEdit4("Disabled", (float*)&themeSettings.StatusPalette.Disable);
			if (colorFilter.PassFilter("Error"))
				ImGui::ColorEdit4("Error", (float*)&themeSettings.StatusPalette.Error);
			if (colorFilter.PassFilter("Warning"))
				ImGui::ColorEdit4("Warning", (float*)&themeSettings.StatusPalette.Warning);
			if (colorFilter.PassFilter("Restart Needed"))
				ImGui::ColorEdit4("Restart Needed", (float*)&themeSettings.StatusPalette.RestartNeeded);
			if (colorFilter.PassFilter("Current Hotkey"))
				ImGui::ColorEdit4("Current Hotkey", (float*)&themeSettings.StatusPalette.CurrentHotkey);
			if (colorFilter.PassFilter("Success"))
				ImGui::ColorEdit4("Success", (float*)&themeSettings.StatusPalette.SuccessColor);
			if (colorFilter.PassFilter("Info"))
				ImGui::ColorEdit4("Info", (float*)&themeSettings.StatusPalette.InfoColor);
			ImGui::TreePop();
		}

		if (ImGui::TreeNode("Full Palette")) {
			ImGui::TextWrapped("Advanced color controls for detailed customization of all UI elements.");

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
