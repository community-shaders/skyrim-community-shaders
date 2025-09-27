#include "SettingsTabRenderer.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <imgui.h>
#include <imgui_internal.h>
#include <windows.h>

#include "Globals.h"
#include "Menu.h"
#include "ShaderCache.h"
#include "ThemeManager.h"
#include "Util.h"

using json = nlohmann::json;

void SettingsTabRenderer::RenderGeneralSettings(
	SettingsState& state,
	const std::function<const char*(uint32_t)>& keyIdToString)
{
	if (ImGui::BeginTabBar("##GeneralTabBar", ImGuiTabBarFlags_None)) {
		RenderShadersTab();
		RenderKeybindingsTab(state, keyIdToString);
		RenderInterfaceTab();
		ImGui::EndTabBar();
	}
}

void SettingsTabRenderer::RenderShadersTab()
{
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
}

void SettingsTabRenderer::RenderKeybindingsTab(
	SettingsState& state,
	const std::function<const char*(uint32_t)>& keyIdToString)
{
	if (ImGui::BeginTabItem("Keybindings")) {
		auto& settings = globals::menu->GetSettings();
		auto& themeSettings = globals::menu->GetSettings().Theme;

		// Toggle Key
		if (state.settingToggleKey) {
			ImGui::Text("Press any key to set as toggle key...");
		} else {
			ImGui::AlignTextToFramePadding();
			ImGui::Text("Toggle Key:");
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			ImGui::TextColored(themeSettings.StatusPalette.CurrentHotkey, "%s", keyIdToString(settings.ToggleKey));

			ImGui::AlignTextToFramePadding();
			ImGui::SameLine();
			if (ImGui::Button("Change##toggle")) {
				state.settingToggleKey = true;
			}
		}

		// Effects Toggle Key
		if (state.settingsEffectsToggle) {
			ImGui::Text("Press any key to set as a toggle key for all effects...");
		} else {
			ImGui::AlignTextToFramePadding();
			ImGui::Text("Effect Toggle Key:");
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			ImGui::TextColored(themeSettings.StatusPalette.CurrentHotkey, "%s", keyIdToString(settings.EffectToggleKey));

			ImGui::AlignTextToFramePadding();
			ImGui::SameLine();
			if (ImGui::Button("Change##EffectToggle")) {
				state.settingsEffectsToggle = true;
			}
		}

		// Skip Compilation Key
		if (state.settingSkipCompilationKey) {
			ImGui::Text("Press any key to set as Skip Compilation Key...");
		} else {
			ImGui::AlignTextToFramePadding();
			ImGui::Text("Skip Compilation Key:");
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			ImGui::TextColored(themeSettings.StatusPalette.CurrentHotkey, "%s", keyIdToString(settings.SkipCompilationKey));

			ImGui::AlignTextToFramePadding();
			ImGui::SameLine();
			if (ImGui::Button("Change##skip")) {
				state.settingSkipCompilationKey = true;
			}
		}

		// Overlay Toggle Key
		if (state.settingOverlayToggleKey) {
			ImGui::Text("Press any key to set as a toggle key for displaying the overlay...");
		} else {
			ImGui::AlignTextToFramePadding();
			ImGui::Text("Overlay Toggle Key:");
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			ImGui::TextColored(themeSettings.StatusPalette.CurrentHotkey, "%s", keyIdToString(settings.OverlayToggleKey));

			ImGui::AlignTextToFramePadding();
			ImGui::SameLine();
			if (ImGui::Button("Change##OverlayToggle")) {
				state.settingOverlayToggleKey = true;
			}
		}

		ImGui::EndTabItem();
	}
}

void SettingsTabRenderer::RenderInterfaceTab()
{
	if (ImGui::BeginTabItem("Interface")) {
		if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_None)) {
			RenderThemesTab();
			RenderStylingTab();
			RenderColorsTab();
			ImGui::EndTabBar();
		}
		ImGui::EndTabItem();
	}
}

void SettingsTabRenderer::RenderThemesTab()
{
	if (ImGui::BeginTabItem("Themes")) {
		auto& themeSettings = globals::menu->GetSettings().Theme;

		// Static variables for popup state and new theme creation
		static bool showCreateThemePopup = false;
		static bool isCreatingNewTheme = false;
		static char newThemeName[128] = "";
		static char newThemeDisplayName[128] = "";
		static char newThemeDescription[256] = "";

		// Theme Preset Selection
		ImGui::SeparatorText("Theme Preset");

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

		// Add "+ Create New" option at the top
		displayNames.push_back("+ Create New");
		items.push_back(displayNames.back().c_str());

		for (const auto& theme : themes) {
			displayNames.push_back(theme.displayName);
			items.push_back(displayNames.back().c_str());
		}

		// Find current selection index - default to "Default" if no theme selected
		// Note: Add 1 to account for "+ Create New" option at index 0
		int currentItem = 1; // Default to first actual theme (Default Dark)
		std::string currentThemePreset = globals::menu->GetSettings().SelectedThemePreset;
		
		// If no theme is selected, default to "Default"
		if (currentThemePreset.empty()) {
			currentThemePreset = "Default";
			globals::menu->GetSettings().SelectedThemePreset = "Default";
		}
		
		// If we're in create new mode, show that as selected
		if (isCreatingNewTheme) {
			currentItem = 0; // "+ Create New"
		} else {
			// Find the theme in the list (skip index 0 which is "+ Create New")
			for (size_t i = 0; i < themes.size(); ++i) {
				if (themes[i].name == currentThemePreset) {
					currentItem = static_cast<int>(i + 1); // +1 for "+ Create New" offset
					break;
				}
			}
		}

		// Theme preset dropdown
		if (ImGui::Combo("##ThemePreset", &currentItem, items.data(), static_cast<int>(items.size()))) {
			if (currentItem == 0) {
				// "+ Create New" selected
				isCreatingNewTheme = true;
				// Keep current theme settings as starting point
			} else if (currentItem >= 1 && currentItem <= static_cast<int>(themes.size())) {
				// Actual theme selected (subtract 1 for "+ Create New" offset)
				isCreatingNewTheme = false;
				std::string selectedTheme = themes[currentItem - 1].name;
				if (globals::menu->LoadThemePreset(selectedTheme)) {
					// Theme loaded successfully, update UI
					themeSettings = globals::menu->GetSettings().Theme;
				}
			}
		}
		
		// Show theme description as tooltip (only for actual themes, not "+ Create New")
		if (currentItem >= 1 && currentItem <= static_cast<int>(themes.size())) {
			const auto& selectedTheme = themes[currentItem - 1]; // -1 for "+ Create New" offset
			if (!selectedTheme.description.empty()) {
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("%s", selectedTheme.description.c_str());
				}
			}
		}

		ImGui::SameLine();
		if (ImGui::Button("Refresh Themes")) {
			themeManager->RefreshThemes();
			// Ensure a valid theme is still selected
			const auto* themeInfo = themeManager->GetThemeInfo(globals::menu->GetSettings().SelectedThemePreset);
			if (!themeInfo) {
				globals::menu->GetSettings().SelectedThemePreset = "Default";
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

		// Save/Update Theme Button (show based on context)
		if (isCreatingNewTheme || (!currentThemePreset.empty() && currentThemePreset != "Default")) {
			ImGui::SameLine();
			
			const char* buttonText = isCreatingNewTheme ? "Save Theme" : "Update Theme";
			if (Util::ButtonWithFlash(buttonText)) {
				if (isCreatingNewTheme) {
					// Show popup for new theme creation
					showCreateThemePopup = true;
					// Clear the input fields
					memset(newThemeName, 0, sizeof(newThemeName));
					memset(newThemeDisplayName, 0, sizeof(newThemeDisplayName));
					memset(newThemeDescription, 0, sizeof(newThemeDescription));
				} else {
					// Update existing theme
					const auto* currentThemeInfo = themeManager->GetThemeInfo(currentThemePreset);
					if (currentThemeInfo) {
						// Use the existing SaveTheme method to serialize the theme settings
						json currentThemeJson;
						globals::menu->SaveTheme(currentThemeJson);
						
						// Overwrite the current theme with updated settings
						if (themeManager->SaveTheme(currentThemePreset, currentThemeJson["Theme"], 
						                           currentThemeInfo->displayName, currentThemeInfo->description)) {
							// Theme updated successfully
						}
					}
				}
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				if (isCreatingNewTheme) {
					ImGui::Text("Create a new theme with current settings");
				} else {
					ImGui::Text("Updates the currently selected theme (%s) with your current settings", currentThemePreset.c_str());
				}
			}
		}



		// Create Theme Popup
		if (showCreateThemePopup) {
			ImGui::OpenPopup("Create New Theme");
		}

		// Popup modal for creating new theme
		if (ImGui::BeginPopupModal("Create New Theme", &showCreateThemePopup, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("Create a new theme with your current settings:");
			ImGui::Separator();

			ImGui::InputText("Theme Name", newThemeName, sizeof(newThemeName));
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("File name for the theme (without .json extension)");
			}
			
			ImGui::InputText("Display Name", newThemeDisplayName, sizeof(newThemeDisplayName));
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Human-readable name shown in the dropdown");
			}
			
			ImGui::InputTextMultiline("Description", newThemeDescription, sizeof(newThemeDescription), ImVec2(400, 80));
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("Optional description for the theme");
			}

			ImGui::Separator();

			// Buttons
			if (Util::ButtonWithFlash("Create Theme") && strlen(newThemeName) > 0) {
				// Use the existing SaveTheme method to serialize the theme settings
				json currentThemeJson;
				globals::menu->SaveTheme(currentThemeJson);
				
				std::string displayName = strlen(newThemeDisplayName) > 0 ? std::string(newThemeDisplayName) : std::string(newThemeName);
				std::string description = strlen(newThemeDescription) > 0 ? std::string(newThemeDescription) : "";
				
				if (themeManager->SaveTheme(std::string(newThemeName), currentThemeJson["Theme"], displayName, description)) {
					// Theme created successfully, load it and exit create mode
					globals::menu->LoadThemePreset(std::string(newThemeName));
					isCreatingNewTheme = false;
					showCreateThemePopup = false;
				}
			}

			ImGui::SameLine();
			if (ImGui::Button("Cancel")) {
				showCreateThemePopup = false;
			}

			ImGui::EndPopup();
		}

		ImGui::SeparatorText("UI Elements");
		ImGui::Checkbox("Use Icon Buttons in Header", &themeSettings.ShowActionIcons);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"When enabled: Shows action buttons (Save, Load, Clear Cache) as icons in the header\n"
				"When disabled: Shows as text buttons below the header");
		}

		ImGui::SliderFloat("Tooltip Hover Delay", &themeSettings.TooltipHoverDelay, 0.0f, 2.0f, "%.2f s", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::TextUnformatted("Time in seconds to wait before a tooltip appears when hovering over an item.");
		}

		ImGui::EndTabItem();
	}
}

void SettingsTabRenderer::RenderStylingTab()
{
	if (ImGui::BeginTabItem("Styling")) {
		auto& themeSettings = globals::menu->GetSettings().Theme;
		auto& style = themeSettings.Style;

		ImGui::SeparatorText("Main");
		if (ImGui::SliderFloat("Global Scale", &themeSettings.GlobalScale, -1.f, 1.f, "%.2f")) {
			float trueScale = exp2(themeSettings.GlobalScale);

			auto& io = ImGui::GetIO();
			io.FontGlobalScale = trueScale;
		}
		
		ImGui::SeparatorText("Font");
		if (ImGui::SliderFloat("Font Size", &themeSettings.FontSize, ThemeManager::Constants::MIN_FONT_SIZE, ThemeManager::Constants::MAX_FONT_SIZE, "%.0f")) {
			// Font size changed, schedule deferred reload
			globals::menu->pendingFontReload = true;
			globals::menu->pendingFontName = themeSettings.FontName;  // Keep current font name
		}
		
		// Font selection dropdown
		static std::vector<std::string> availableFonts;
		static bool fontsDiscovered = false;
		
		auto refreshFontList = [&]() {
			try {
				availableFonts = Util::DiscoverFonts();
			} catch (const std::exception&) {
				// Failed to discover fonts, clear list
				availableFonts.clear();
			}
		};
		
		if (!fontsDiscovered) {
			refreshFontList();
			fontsDiscovered = true;
		}
		
		// Find current font index
		int currentFontIndex = 0;
		for (size_t i = 0; i < availableFonts.size(); ++i) {
			if (availableFonts[i] == themeSettings.FontName) {
				currentFontIndex = static_cast<int>(i);
				break;
			}
		}
		
		// Use ImGui::Combo with safety checks to avoid crashes
		const char* previewText = "None";
		if (!availableFonts.empty() && currentFontIndex >= 0 && currentFontIndex < static_cast<int>(availableFonts.size())) {
			previewText = availableFonts[currentFontIndex].c_str();
		}
		
		if (ImGui::BeginCombo("Font", previewText)) {
			if (availableFonts.empty()) {
				ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No fonts available");
				ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Place .ttf/.otf files in Fonts folder");
			} else {
				for (int i = 0; i < static_cast<int>(availableFonts.size()); ++i) {
					const bool isSelected = (i == currentFontIndex);
					if (ImGui::Selectable(availableFonts[i].c_str(), isSelected)) {
						if (i != currentFontIndex && !availableFonts[i].empty()) {
							// Validate font name before applying
							const std::string& newFontName = availableFonts[i];
							auto fontPath = Util::PathHelpers::GetFontsPath() / newFontName;
							
							if (std::filesystem::exists(fontPath)) {
								// Schedule deferred font reload (safe - will happen between frames)
								globals::menu->pendingFontReload = true;
								globals::menu->pendingFontName = newFontName;
							}
						}
					}
					
					// Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
					if (isSelected) {
						ImGui::SetItemDefaultFocus();
					}
				}
			}
			ImGui::EndCombo();
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Select a custom font file (.ttf/.otf) from the Fonts folder.\nPlace custom fonts in: Interface/CommunityShaders/Fonts/");
		}
		
		if (ImGui::Button("Refresh Font List")) {
			refreshFontList();
			// Reset current font index if it's out of bounds after refresh
			if (currentFontIndex >= static_cast<int>(availableFonts.size())) {
				currentFontIndex = 0;
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Refresh the list of available fonts after adding new font files.");
		}

		ImGui::SeparatorText("Layout");
		ImGui::SliderFloat2("Window Padding", (float*)&style.WindowPadding, 0.0f, 20.0f, "%.0f");
		ImGui::SliderFloat2("Frame Padding", (float*)&style.FramePadding, 0.0f, 20.0f, "%.0f");
		ImGui::SliderFloat2("Item Spacing", (float*)&style.ItemSpacing, 0.0f, 20.0f, "%.0f");
		ImGui::SliderFloat2("Item Inner Spacing", (float*)&style.ItemInnerSpacing, 0.0f, 20.0f, "%.0f");
		ImGui::SliderFloat("Indent Spacing", &style.IndentSpacing, 0.0f, 30.0f, "%.0f");
		ImGui::SliderFloat("Scrollbar Size", &style.ScrollbarSize, 1.0f, 20.0f, "%.0f");
		ImGui::SliderFloat("Grab Min Size", &style.GrabMinSize, 1.0f, 20.0f, "%.0f");

		ImGui::SeparatorText("Scrollbar Opacity");
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

		ImGui::SeparatorText("Borders");
		ImGui::SliderFloat("Window Border Size", &style.WindowBorderSize, 0.0f, 5.0f, "%.0f");
		ImGui::SliderFloat("Child Border Size", &style.ChildBorderSize, 0.0f, 5.0f, "%.0f");
		ImGui::SliderFloat("Popup Border Size", &style.PopupBorderSize, 0.0f, 5.0f, "%.0f");
		ImGui::SliderFloat("Frame Border Size", &style.FrameBorderSize, 0.0f, 5.0f, "%.0f");
		ImGui::SliderFloat("Tab Border Size", &style.TabBorderSize, 0.0f, 5.0f, "%.0f");
		ImGui::SliderFloat("Tab Bar Border Size", &style.TabBarBorderSize, 0.0f, 5.0f, "%.0f");

		ImGui::SeparatorText("Rounding");
		ImGui::SliderFloat("Window Rounding", &style.WindowRounding, 0.0f, 12.0f, "%.0f");
		ImGui::SliderFloat("Child Rounding", &style.ChildRounding, 0.0f, 12.0f, "%.0f");
		ImGui::SliderFloat("Frame Rounding", &style.FrameRounding, 0.0f, 12.0f, "%.0f");
		ImGui::SliderFloat("Popup Rounding", &style.PopupRounding, 0.0f, 12.0f, "%.0f");
		ImGui::SliderFloat("Scrollbar Rounding", &style.ScrollbarRounding, 0.0f, 12.0f, "%.0f");
		ImGui::SliderFloat("Grab Rounding", &style.GrabRounding, 0.0f, 12.0f, "%.0f");
		ImGui::SliderFloat("Tab Rounding", &style.TabRounding, 0.0f, 12.0f, "%.0f");

		ImGui::SeparatorText("Tables");
		ImGui::SliderFloat2("Cell Padding", (float*)&style.CellPadding, 0.0f, 20.0f, "%.0f");
		ImGui::SliderAngle("Table Angled Headers Angle", &style.TableAngledHeadersAngle, -50.0f, +50.0f);

		ImGui::SeparatorText("Widgets");
		ImGui::Combo("ColorButtonPosition", (int*)&style.ColorButtonPosition, "Left\0Right\0");
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

		ImGui::SeparatorText("Docking");
		ImGui::SliderFloat("Docking Splitter Size", &style.DockingSeparatorSize, 0.0f, 12.0f, "%.0f");

		ImGui::EndTabItem();
	}
}

void SettingsTabRenderer::RenderColorsTab()
{
	if (ImGui::BeginTabItem("Colors")) {
		auto& themeSettings = globals::menu->GetSettings().Theme;
		auto& colors = themeSettings.FullPalette;

		ImGui::SeparatorText("Status");

		ImGui::ColorEdit4("Disabled Text", (float*)&themeSettings.StatusPalette.Disable);
		ImGui::ColorEdit4("Error Text", (float*)&themeSettings.StatusPalette.Error);
		ImGui::ColorEdit4("Warning Text", (float*)&themeSettings.StatusPalette.Warning);
		ImGui::ColorEdit4("Restart Needed Text", (float*)&themeSettings.StatusPalette.RestartNeeded);
		ImGui::ColorEdit4("Current Hotkey Text", (float*)&themeSettings.StatusPalette.CurrentHotkey);
		ImGui::ColorEdit4("Success Text", (float*)&themeSettings.StatusPalette.SuccessColor);
		ImGui::ColorEdit4("Info Text", (float*)&themeSettings.StatusPalette.InfoColor);

		ImGui::SeparatorText("Feature Headings");

		ImGui::ColorEdit4("Regular", (float*)&themeSettings.FeatureHeading.ColorDefault);
		ImGui::ColorEdit4("Hovered", (float*)&themeSettings.FeatureHeading.ColorHovered);
		ImGui::SliderFloat("Minimized Alpha Factor", &themeSettings.FeatureHeading.MinimizedFactor, 0.0f, 1.0f, "%.2f");

		ImGui::SeparatorText("Palette");

		// Simple Colors Section - collapsed by default for clean interface
		if (ImGui::CollapsingHeader("Simple", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::ColorEdit4("Background", (float*)&themeSettings.Palette.Background);
			ImGui::ColorEdit4("Text", (float*)&themeSettings.Palette.Text);
			ImGui::ColorEdit4("Border", (float*)&themeSettings.Palette.Border);
		}

		// Advanced Colors Section - collapsed by default to avoid overwhelming users
		if (ImGui::CollapsingHeader("Advanced")) {
			ImGui::TextWrapped("Advanced color controls for detailed customization of all UI elements.");
			
			static ImGuiTextFilter filter;
			filter.Draw("Filter colors", ImGui::GetFontSize() * 16);

			for (int i = 0; i < ImGuiCol_COUNT; i++) {
				const char* name = ImGui::GetStyleColorName(i);
				if (!filter.PassFilter(name))
					continue;
				ImGui::ColorEdit4(name, (float*)&colors[i], ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
			}
		}

		ImGui::EndTabItem();
	}
}