#include "MenuHeaderRenderer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include "Fonts.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "Plugin.h"
#include "ShaderCache.h"
#include "State.h"
#include "ThemeManager.h"
#include "Util.h"

namespace
{
	using RoleFontGuard = MenuFonts::FontRoleGuard;
}

void MenuHeaderRenderer::RenderHeader(bool isDocked, bool showLogo, bool canShowIcons, float uiScale, const Menu::UIIcons& uiIcons)
{
	if (!globals::menu) {
		logger::error("MenuHeaderRenderer::RenderHeader: globals::menu is null, cannot render header");
		return;
	}

	auto versionStr = Util::GetFormattedVersion(Plugin::VERSION);
	auto expectedTag = std::format("v{}", versionStr);
	auto title = Plugin::BUILD_DESCRIBE == expectedTag ? std::format("Community Shaders {}", versionStr) : std::format("Community Shaders {} [{}]", versionStr, Plugin::BUILD_DESCRIBE);
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
		bool centerHeader = globals::menu->GetTheme().CenterHeader;

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

			if (centerHeader) {
				// Calculate the width of the content
				float contentWidth = 0.0f;

				if (showLogo) {
					float logoAspectRatio = uiIcons.logo.size.x / uiIcons.logo.size.y;
					contentWidth = (logoSize * logoAspectRatio) + ImGui::GetStyle().ItemSpacing.x;
				}

				// Calculate text width
				{
					RoleFontGuard titleFont(Menu::FontRole::Title);
					ImGui::SetWindowFontScale(textScaleFactor);
					contentWidth += ImGui::CalcTextSize(title.c_str()).x;
					ImGui::SetWindowFontScale(1.0f);
				}

				float offset = Util::GetCenterOffsetForContent(contentWidth);
				if (offset > 0.0f) {
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
				}
			} else {
				// Add padding for left-aligned layout
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ThemeManager::Constants::CURSOR_POSITION_PADDING);
			}

			// Always display logo if texture is available
			if (showLogo) {
				float logoAspectRatio = uiIcons.logo.size.x / uiIcons.logo.size.y;
				ImVec2 logoSizeVec(logoSize * logoAspectRatio, logoSize);

				// Determine tint color for logo
				ImU32 logoTint = IM_COL32_WHITE;
				if (globals::menu->GetSettings().Theme.UseMonochromeLogo) {
					ImVec4 textColor = globals::menu->GetSettings().Theme.Palette.Text;
					logoTint = ImGui::GetColorU32(textColor);
				}

				// Use our helper to render aligned logo and text with perfect vertical alignment
				{
					RoleFontGuard titleFont(Menu::FontRole::Title);
					Util::DrawAlignedTextWithLogo(
						uiIcons.logo.texture,
						logoSizeVec,
						title.c_str(),
						textScaleFactor,
						logoTint);
				}
			} else {
				// No logo, just render the text with proper alignment
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
				{
					RoleFontGuard titleFont(Menu::FontRole::Title);
					Util::DrawSharpText(title.c_str(), true, textScaleFactor);
				}
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

			if (centerHeader) {
				// Calculate text width for centering
				float textWidth = 0.0f;
				{
					RoleFontGuard titleFont(Menu::FontRole::Title);
					ImGui::SetWindowFontScale(textScaleFactor);
					textWidth = ImGui::CalcTextSize(title.c_str()).x;
					ImGui::SetWindowFontScale(1.0f);
				}

				// Use helper to get centering offset
				float offset = Util::GetCenterOffsetForContent(textWidth);
				if (offset > 0.0f) {
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
				}
			}

			ImGui::SetWindowFontScale(textScaleFactor);
			{
				RoleFontGuard titleFont(Menu::FontRole::Title);
				ImGui::TextUnformatted(title.c_str());
			}
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
			if (Util::ButtonWithFlash(T("menu.save_settings", "Save Settings"), { -1, 0 })) {
				globals::state->Save();
				globals::state->SaveTheme();
			}

			// Restore Saved Settings Button
			ImGui::TableNextColumn();
			if (ImGui::Button(T("menu.restore_settings", "Restore Saved Settings"), { -1, 0 })) {
				globals::state->Load();
			}

			// Clear Shader Cache Button
			ImGui::TableNextColumn();
			if (ImGui::Button(T("menu.clear_shader_cache", "Clear Shader Cache"), { -1, 0 })) {
				Util::RequestClearShaderCacheConfirmation();
			}
			if (auto _tt = Util::HoverTooltipWrapper()) {
				ImGui::Text("%s", T("menu.clear_shader_cache_tooltip",
									  "Clears the shader cache and disk cache (if enabled). "
									  "The Shader Cache is the collection of compiled shaders which replace the vanilla shaders at runtime. "
									  "The Disk Cache is a collection of compiled shaders on disk. "
									  "Clearing will mean that shaders are recompiled only when the game re-encounters them."));
			}

			// Error message toggle if needed
			if (shaderCache->GetFailedTasks()) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				if (ImGui::Button(T("menu.toggle_error_message", "Toggle Error Message"), { -1, 0 })) {
					shaderCache->ToggleErrorMessages();
				}
				if (auto _tt = Util::HoverTooltipWrapper()) {
					ImGui::Text("%s", T("menu.toggle_error_message_tooltip",
										  "Hide or show the shader failure message. "
										  "Your installation is broken and will likely see errors in game. "
										  "Please double check you have updated all features and that your load order is correct. "
										  "See CommunityShaders.log for details and check the Nexus Mods page or Discord server."));
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
		if (ImGui::Button(T("menu.toggle_error_message", "Toggle Error Message"), { -1, 0 })) {
			shaderCache->ToggleErrorMessages();
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", T("menu.toggle_error_message_tooltip",
								  "Hide or show the shader failure message. "
								  "Your installation is broken and will likely see errors in game. "
								  "Please double check you have updated all features and that your load order is correct. "
								  "See CommunityShaders.log for details and check the Nexus Mods page or Discord server."));
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
			T("menu.save_settings", "Save Settings"),
			[]() {
				globals::state->Save();
				globals::state->SaveTheme();
			} });
	}
	if (uiIcons.loadSettings.texture) {
		actionIcons.push_back({ uiIcons.loadSettings.texture,
			T("menu.restore_settings", "Restore Saved Settings"),
			[]() {
				globals::state->Load();
			} });
	}
	if (uiIcons.clearCache.texture) {
		actionIcons.push_back({ uiIcons.clearCache.texture,
			T("menu.clear_shader_cache_tooltip",
				"Clears the shader cache and disk cache (if enabled). "
				"The Shader Cache is the collection of compiled shaders which replace the vanilla shaders at runtime. "
				"The Disk Cache is a collection of compiled shaders on disk. "
				"Clearing will mean that shaders are recompiled only when the game re-encounters them."),
			[]() {
				Util::RequestClearShaderCacheConfirmation();
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
		ImRect interactionRect({ iconX, iconY }, { iconX + iconSize, iconY + iconSize });

		// Check mouse interaction against full area
		const bool isHovered = ImGui::IsMouseHoveringRect(interactionRect.Min, interactionRect.Max, false);
		Util::DrawRoundedButtonHighlight(interactionRect, isHovered, isHovered && ImGui::IsMouseDown(ImGuiMouseButton_Left), fgDrawList);

		// Only render if texture is valid
		if (it->texture) {
			// Draw icon with hover effect, using reduced area to minimize padding
			ImU32 tintColor;
			if (globals::menu->GetSettings().Theme.UseMonochromeIcons) {
				// Use theme text color for monochrome icons
				ImVec4 textColor = globals::menu->GetSettings().Theme.Palette.Text;
				if (!isHovered) {
					textColor.w *= 0.85f;  // Slightly reduce alpha when not hovered
				}
				tintColor = ImGui::GetColorU32(textColor);
			} else {
				// Use white/gray tint for colored icons
				tintColor = isHovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(220, 220, 220, 220);
			}
			fgDrawList->AddImage(it->texture, iconMin, iconMax, ImVec2(0, 0), ImVec2(1, 1), tintColor);
		}

		// Handle interaction
		if (isHovered) {
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

	// Get tint color for monochrome icons
	ImVec4 tintColor = ImVec4(1, 1, 1, 1);
	if (globals::menu->GetSettings().Theme.UseMonochromeIcons) {
		tintColor = globals::menu->GetSettings().Theme.Palette.Text;
	}

	// Draw action icons as ImageButtons
	for (size_t i = 0; i < actionIcons.size(); ++i) {
		const auto& icon = actionIcons[i];

		// Skip if texture is null
		if (!icon.texture) {
			continue;
		}

		std::string buttonId = std::format("##ActionBtn{}", i);

		// Use ImageButton with reduced image size to minimize padding
		if (ImGui::ImageButton(buttonId.c_str(), icon.texture, imageSize, ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), tintColor)) {
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

	// Determine watermark color based on monochrome logo setting
	ImU32 watermarkColor;
	if (globals::menu->GetSettings().Theme.UseMonochromeLogo) {
		ImVec4 textColor = globals::menu->GetSettings().Theme.Palette.Text;
		textColor.w = 0.18f;  // Very low alpha for subtle watermark effect
		watermarkColor = ImGui::GetColorU32(textColor);
	} else {
		watermarkColor = IM_COL32(255, 255, 255, 45);
	}

	drawList->AddImage(uiIcons.logo.texture, logoMin, logoMax, ImVec2(0, 0), ImVec2(1, 1), watermarkColor);
}
