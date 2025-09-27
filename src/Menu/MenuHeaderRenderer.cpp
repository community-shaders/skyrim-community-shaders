#include "MenuHeaderRenderer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include "Globals.h"
#include "Plugin.h"
#include "ShaderCache.h"
#include "State.h"
#include "ThemeManager.h"
#include "Util.h"

void MenuHeaderRenderer::RenderHeader(bool isDocked, bool showLogo, bool canShowIcons, float uiScale, const Menu::UIIcons& uiIcons)
{
	auto title = std::format("Community Shaders {}", Util::GetFormattedVersion(Plugin::VERSION));
	auto actionIcons = BuildActionIcons(canShowIcons, uiIcons);

	if (isDocked) {
		// When docked, draw logo as a background watermark if available
		if (showLogo && uiIcons.logo.texture) {
			RenderWatermarkLogo(uiIcons);
		}

		// Draw action icons in the title bar area
		RenderDockedIcons(actionIcons, uiScale);
	} else {
		// When not docked, show the custom header
		if ((showLogo || canShowIcons) && ImGui::BeginTable("##HeaderLayout", 2, ImGuiTableFlags_SizingStretchProp)) {
			ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Buttons", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableNextColumn();  // Title on the left with logo

			// Determine scaling based on GlobalScale setting and font size
			const float currentFontSize = ImGui::GetFontSize();
			const float baseTextScale = ThemeManager::Constants::HEADER_BASE_TEXT_SCALE;
			const float baseIconSize = currentFontSize * ThemeManager::Constants::HEADER_BASE_ICON_MULTIPLIER;

			// Apply UI scale to the base scaling factors
			const float textScaleFactor = baseTextScale * uiScale;
			const float logoSize = baseIconSize * uiScale;  // Match action icon size

			// Always display logo if texture is available
			if (showLogo) {
				float logoAspectRatio = uiIcons.logo.size.x / uiIcons.logo.size.y;
				ImVec2 logoSizeVec(logoSize * logoAspectRatio, logoSize);

				// Add a bit of padding before the logo and text
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ThemeManager::Constants::CURSOR_POSITION_PADDING);

				// Use our helper to render aligned logo and text with perfect vertical alignment
				Util::DrawAlignedTextWithLogo(
					uiIcons.logo.texture,
					logoSizeVec,
					title.c_str(),
					textScaleFactor);
			} else {
				// No logo, just render the text with proper alignment
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ThemeManager::Constants::CURSOR_POSITION_PADDING);
				Util::DrawSharpText(title.c_str(), true, textScaleFactor);
				ImGui::PopStyleVar();
			}

			// Buttons on the right
			ImGui::TableNextColumn();
			RenderUndockedIcons(actionIcons, uiScale);

			ImGui::EndTable();
		} else if (!(showLogo || canShowIcons)) {
			// No icons available - show just the title without the table layout
			const float baseTextScale = ThemeManager::Constants::HEADER_FALLBACK_TEXT_SCALE;
			const float textScaleFactor = baseTextScale * uiScale;  // Apply UI scale

			ImGui::SetWindowFontScale(textScaleFactor);
			ImGui::TextUnformatted(title.c_str());
			ImGui::SetWindowFontScale(1.0f);
		}
	}

	// Add separators - no separator needed for docked mode since icons are in title bar
	if (!isDocked) {
		// First separator - always shown when not docked
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal, ThemeManager::Constants::SEPARATOR_THICKNESS);
		ImGui::Spacing();
	}

	// If icons are disabled or missing, show action buttons as text between separators (only when not docked)
	auto shaderCache = globals::shaderCache;
	if (!canShowIcons && !isDocked) {
		if (ImGui::BeginTable("##ActionButtons", 4, ImGuiTableFlags_SizingStretchSame)) {
			// Save Settings Button
			ImGui::TableNextColumn();
			if (Util::ButtonWithFlash("Save Settings", { -1, 0 })) {
				globals::state->Save();
				globals::state->SaveTheme();
			}

			// Restore Saved Settings Button
			ImGui::TableNextColumn();
			if (ImGui::Button("Restore Saved Settings", { -1, 0 })) {
				globals::state->Load();
			}

			// Clear Shader Cache Button
			ImGui::TableNextColumn();
			if (ImGui::Button("Clear Shader Cache", { -1, 0 })) {
				shaderCache->Clear();
				if (shaderCache->IsDiskCache()) {
					shaderCache->DeleteDiskCache();
				}
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text(
					"Clears the shader cache and disk cache (if enabled). "
					"The Shader Cache is the collection of compiled shaders which replace the vanilla shaders at runtime. "
					"The Disk Cache is a collection of compiled shaders on disk. "
					"Clearing will mean that shaders are recompiled only when the game re-encounters them. ");
			}

			// Error message toggle if needed
			if (shaderCache->GetFailedTasks()) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				if (ImGui::Button("Toggle Error Message", { -1, 0 })) {
					shaderCache->ToggleErrorMessages();
				}
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text(
						"Hide or show the shader failure message. "
						"Your installation is broken and will likely see errors in game. "
						"Please double check you have updated all features and that your load order is correct. "
						"See CommunityShaders.log for details and check the Nexus Mods page or Discord server. ");
				}
			}

			ImGui::EndTable();
		}

		// Second separator - only shown if icons are disabled/missing or if there are failed tasks (and not docked)
		if (!isDocked) {
			ImGui::Spacing();
			ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal, ThemeManager::Constants::SEPARATOR_THICKNESS);
			ImGui::Spacing();
		}
	} else if (shaderCache->GetFailedTasks() && !isDocked) {
		// If icons are enabled but there are failed tasks, show error toggle button
		// and add the second separator (only when not docked)
		if (ImGui::Button("Toggle Error Message", { -1, 0 })) {
			shaderCache->ToggleErrorMessages();
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text(
				"Hide or show the shader failure message. "
				"Your installation is broken and will likely see errors in game. "
				"Please double check you have updated all features and that your load order is correct. "
				"See CommunityShaders.log for details and check the Nexus Mods page or Discord server. ");
		}

		// Add second separator when showing error button
		ImGui::Spacing();
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal, ThemeManager::Constants::SEPARATOR_THICKNESS);
		ImGui::Spacing();
	}
}

std::vector<MenuHeaderRenderer::ActionIcon> MenuHeaderRenderer::BuildActionIcons(bool canShowIcons, const Menu::UIIcons& uiIcons)
{
	std::vector<ActionIcon> actionIcons;

	if (!canShowIcons) {
		return actionIcons;
	}

	// Build list of available action icons (in display order)
	if (uiIcons.saveSettings.texture) {
		actionIcons.push_back({ uiIcons.saveSettings.texture,
			"Save Settings",
			[]() {
				globals::state->Save();
				globals::state->SaveTheme();
			} });
	}
	if (uiIcons.loadSettings.texture) {
		actionIcons.push_back({ uiIcons.loadSettings.texture,
			"Restore Saved Settings",
			[]() {
				globals::state->Load();
			} });
	}
	if (uiIcons.clearCache.texture) {
		auto shaderCache = globals::shaderCache;
		actionIcons.push_back({ uiIcons.clearCache.texture,
			"Clear Shader Cache\n\n"
			"Clears the shader cache and disk cache (if enabled).\n"
			"The Shader Cache is the collection of compiled shaders which replace\n"
			"the vanilla shaders at runtime. The Disk Cache is a collection of\n"
			"compiled shaders on disk. Clearing will mean that shaders are\n"
			"recompiled only when the game re-encounters them.",
			[shaderCache]() {
				shaderCache->Clear();
				if (shaderCache->IsDiskCache()) {
					shaderCache->DeleteDiskCache();
				}
			} });
	}

	return actionIcons;
}

void MenuHeaderRenderer::RenderActionIcons(const std::vector<ActionIcon>& actionIcons, bool isDocked, float uiScale)
{
	if (isDocked) {
		RenderDockedIcons(actionIcons, uiScale);
	} else {
		RenderUndockedIcons(actionIcons, uiScale);
	}
}

void MenuHeaderRenderer::RenderDockedIcons(const std::vector<ActionIcon>& actionIcons, float uiScale)
{
	if (actionIcons.empty())
		return;

	// Docked: Draw larger icons in the title bar using foreground draw list
	const float currentFontSize = ImGui::GetFontSize();
	const float iconSize = currentFontSize * ThemeManager::Constants::DOCKED_ICON_SIZE_MULTIPLIER * uiScale;
	const float iconSpacing = ThemeManager::Constants::DOCKED_ICON_SPACING * uiScale;
	const float rightMargin = ThemeManager::Constants::DOCKED_RIGHT_MARGIN * uiScale;

	// Get window position and calculate title bar area
	ImVec2 windowPos = ImGui::GetWindowPos();
	ImVec2 windowSize = ImGui::GetWindowSize();
	float titleBarHeight = ImGui::GetFrameHeight();

	// Use foreground draw list to draw over the title bar
	ImDrawList* fgDrawList = ImGui::GetForegroundDrawList();

	// Calculate icon positions (right to left from close button)
	float iconX = windowPos.x + windowSize.x - rightMargin;
	float iconY = windowPos.y + (titleBarHeight - iconSize) * 0.5f;

	// Draw icons from right to left
	for (auto it = actionIcons.rbegin(); it != actionIcons.rend(); ++it) {
		iconX -= iconSize + iconSpacing;

		// Slightly reduce the icon rendering area to minimize any transparent padding
		const float paddingReduction = ThemeManager::Constants::DOCKED_ICON_PADDING_REDUCTION * uiScale;
		ImVec2 iconMin(iconX + paddingReduction, iconY + paddingReduction);
		ImVec2 iconMax(iconX + iconSize - paddingReduction, iconY + iconSize - paddingReduction);

		// Use the full area for mouse interaction (including padding)
		ImVec2 interactionMin(iconX, iconY);
		ImVec2 interactionMax(iconX + iconSize, iconY + iconSize);

		// Check mouse interaction against full area
		ImVec2 mousePos = ImGui::GetMousePos();
		bool isHovered = mousePos.x >= interactionMin.x && mousePos.x <= interactionMax.x &&
		                 mousePos.y >= interactionMin.y && mousePos.y <= interactionMax.y;

		// Draw icon with hover effect, using reduced area to minimize padding
		ImU32 tintColor = isHovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(220, 220, 220, 220);
		fgDrawList->AddImage(it->texture, iconMin, iconMax, ImVec2(0, 0), ImVec2(1, 1), tintColor);

		// Handle interaction
		if (isHovered) {
			// Draw subtle background for hovered icon using interaction area
			fgDrawList->AddRectFilled(interactionMin, interactionMax, IM_COL32(255, 255, 255, 40));

			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				it->callback();
			}

			// Set tooltip manually since we're drawing outside normal ImGui flow
			ImGui::SetTooltip("%s", it->tooltip);
		}
	}
}

void MenuHeaderRenderer::RenderUndockedIcons(const std::vector<ActionIcon>& actionIcons, float uiScale)
{
	if (actionIcons.empty())
		return;

	// Undocked: Draw icons as ImageButtons in a table column
	const float currentFontSize = ImGui::GetFontSize();
	const float baseIconSize = currentFontSize * ThemeManager::Constants::HEADER_BASE_ICON_MULTIPLIER;
	const float iconSize = baseIconSize * uiScale;
	const float paddingReduction = ThemeManager::Constants::UNDOCKED_ICON_PADDING_REDUCTION * uiScale;
	const ImVec2 buttonSize(iconSize, iconSize);
	const ImVec2 imageSize(iconSize - paddingReduction, iconSize - paddingReduction);

	// Setup button styling for transparent background with hover effects
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ThemeManager::Constants::UNDOCKED_ICON_ITEM_SPACING, 0.0f));  // Slightly increased spacing
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);                                                           // Remove button borders
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));                                                         // Transparent button background
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.8f, 0.8f, 0.25f));                                     // Slightly more visible hover effect

	// Draw action icons as ImageButtons
	for (size_t i = 0; i < actionIcons.size(); ++i) {
		const auto& icon = actionIcons[i];
		std::string buttonId = std::format("##ActionBtn{}", i);

		// Use ImageButton with reduced image size to minimize padding
		if (ImGui::ImageButton(buttonId.c_str(), icon.texture, imageSize)) {
			icon.callback();
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", icon.tooltip);
		}

		// Add SameLine except for the last button
		if (i < actionIcons.size() - 1) {
			ImGui::SameLine();
		}
	}

	// Restore default style
	ImGui::PopStyleVar(2);    // Pop both style variables: ItemSpacing and FrameBorderSize
	ImGui::PopStyleColor(2);  // Pop both style colors: Button and ButtonHovered
}

void MenuHeaderRenderer::RenderWatermarkLogo(const Menu::UIIcons& uiIcons)
{
	// Get current window's drawable area (excluding title bar)
	ImVec2 windowPos = ImGui::GetWindowPos();
	ImVec2 windowSize = ImGui::GetWindowSize();
	float titleBarHeight = ImGui::GetFrameHeight();

	// Calculate content area (below title bar)
	ImVec2 contentPos(windowPos.x, windowPos.y + titleBarHeight);
	ImVec2 contentSize(windowSize.x, windowSize.y - titleBarHeight);

	// Calculate watermark logo size - base it on height for consistent sizing
	const float watermarkHeightPercent = ThemeManager::Constants::WATERMARK_HEIGHT_PERCENT;
	float watermarkHeight = contentSize.y * watermarkHeightPercent;
	float logoAspectRatio = uiIcons.logo.size.x / uiIcons.logo.size.y;
	float watermarkWidth = watermarkHeight * logoAspectRatio;

	// Position watermark in the center of the content area
	float logoX = contentPos.x + (contentSize.x - watermarkWidth) * 0.5f;   // Horizontally centered
	float logoY = contentPos.y + (contentSize.y - watermarkHeight) * 0.5f;  // Vertically centered

	// Draw watermark logo with transparency and blending
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImVec2 logoMin(logoX, logoY);
	ImVec2 logoMax(logoX + watermarkWidth, logoY + watermarkHeight);

	// Use very low alpha for subtle watermark effect
	ImU32 watermarkColor = IM_COL32(255, 255, 255, 45);
	drawList->AddImage(uiIcons.logo.texture, logoMin, logoMax, ImVec2(0, 0), ImVec2(1, 1), watermarkColor);
}