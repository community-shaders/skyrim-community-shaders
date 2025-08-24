#include "SettingsTabRenderer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include "Globals.h"
#include "Menu.h"
#include "ShaderCache.h"
#include "Util.h"

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
			RenderUIOptionsTab();
			RenderSizesTab();
			RenderColorsTab();
			ImGui::EndTabBar();
		}
		ImGui::EndTabItem();
	}
}

void SettingsTabRenderer::RenderUIOptionsTab()
{
	if (ImGui::BeginTabItem("UI Options")) {
		auto& themeSettings = globals::menu->GetSettings().Theme;

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

void SettingsTabRenderer::RenderSizesTab()
{
	if (ImGui::BeginTabItem("Sizes")) {
		auto& themeSettings = globals::menu->GetSettings().Theme;
		auto& style = themeSettings.Style;

		ImGui::SeparatorText("Main");
		if (ImGui::SliderFloat("Global Scale", &themeSettings.GlobalScale, -1.f, 1.f, "%.2f")) {
			float trueScale = exp2(themeSettings.GlobalScale);

			auto& io = ImGui::GetIO();
			io.FontGlobalScale = trueScale;
		}
		ImGui::SliderFloat("Font Size", &themeSettings.FontSize, ThemeManager::Constants::MIN_FONT_SIZE, ThemeManager::Constants::MAX_FONT_SIZE, "%.0f");
		ImGui::SliderFloat2("Window Padding", (float*)&style.WindowPadding, 0.0f, 20.0f, "%.0f");
		ImGui::SliderFloat2("Frame Padding", (float*)&style.FramePadding, 0.0f, 20.0f, "%.0f");
		ImGui::SliderFloat2("Item Spacing", (float*)&style.ItemSpacing, 0.0f, 20.0f, "%.0f");
		ImGui::SliderFloat2("Item Inner Spacing", (float*)&style.ItemInnerSpacing, 0.0f, 20.0f, "%.0f");
		ImGui::SliderFloat("Indent Spacing", &style.IndentSpacing, 0.0f, 30.0f, "%.0f");
		ImGui::SliderFloat("Scrollbar Size", &style.ScrollbarSize, 1.0f, 20.0f, "%.0f");
		ImGui::SliderFloat("Grab Min Size", &style.GrabMinSize, 1.0f, 20.0f, "%.0f");

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

		if (ImGui::RadioButton("Simple Palette", themeSettings.UseSimplePalette))
			themeSettings.UseSimplePalette = true;
		ImGui::SameLine();
		if (ImGui::RadioButton("Full Palette", !themeSettings.UseSimplePalette))
			themeSettings.UseSimplePalette = false;

		if (themeSettings.UseSimplePalette) {
			ImGui::ColorEdit4("Background", (float*)&themeSettings.Palette.Background);
			ImGui::ColorEdit4("Text", (float*)&themeSettings.Palette.Text);
			ImGui::ColorEdit4("Border", (float*)&themeSettings.Palette.Border);
		} else {
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