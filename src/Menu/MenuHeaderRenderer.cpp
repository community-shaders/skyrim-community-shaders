#include "MenuHeaderRenderer.h"
#include "Menu/IconLoader.h"

#include <imgui.h>
#include <imgui_internal.h>

#include "Features/Upscaling.h"
#include "Fonts.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "IconsTabler.h"
#include "Menu.h"
#include "Menu/GoToPalette.h"
#include "Menu/HomePageRenderer.h"
#include "Menu/MenuLinks.h"
#include "Plugin.h"
#include "SaveManager.h"
#include "ShaderCache.h"
#include "State.h"
#include "ThemeManager.h"
#include "Util.h"

namespace
{
	using RoleFontGuard = MenuFonts::FontRoleGuard;

	constexpr const char* kHelpPopupId = "##MenuHelpPopover";
	constexpr const char* kFaqModalId = "###MenuFaqModal";

	/** @brief Draws an icon glyph centred in a square, sized to match the texture icons. */
	void DrawIconGlyph(ImDrawList* drawList, ImVec2 min, float size, Icons::GlyphRef glyph, ImU32 color)
	{
		ImFont* font = Icons::GetFont(glyph);
		if (!font || !glyph)
			return;
		// PushFont takes a size before the style and window font scales; undo them so the glyph matches the textures
		const ImGuiContext& context = *ImGui::GetCurrentContext();
		const float fontScale = context.FontSizeBase > 0.0f ? context.FontSize / context.FontSizeBase : 1.0f;
		ImGui::PushFont(font, size * ThemeManager::Constants::HEADER_GLYPH_ICON_SCALE / std::max(fontScale, 0.01f));
		drawList->AddText(Icons::GetCenteredGlyphPos(min, ImVec2(size, size), glyph.utf8), color, glyph.utf8);
		ImGui::PopFont();
	}
}

std::string MenuHeaderRenderer::GetTitleText()
{
	return std::format("Community Shaders {}", Util::GetFormattedVersion(Plugin::VERSION));
}

std::string MenuHeaderRenderer::GetBuildTagText()
{
	const auto expectedTag = std::format("v{}", Util::GetFormattedVersion(Plugin::VERSION));
	return Plugin::BUILD_DESCRIBE != expectedTag ? Util::GetBuildLabel() : std::string();
}

std::string MenuHeaderRenderer::GetGameVersionText()
{
	return I18n::GetSingleton()->Format("menu.footer.game_version",
		{ { "runtime", std::string(magic_enum::enum_name(REL::Module::GetRuntime())) },
			{ "version", Util::GetFormattedVersion(REL::Module::get().version()) } },
		"Game Version: {runtime} {version}");
}

std::string MenuHeaderRenderer::GetSwapChainText()
{
	return I18n::GetSingleton()->Format("menu.footer.d3d12_swap_chain",
		{ { "status", globals::features::upscaling.d3d12SwapChainActive ? std::string(T("common.active", "Active")) : std::string(T("common.inactive", "Inactive")) } },
		"D3D12 Swap Chain: {status}");
}

std::string MenuHeaderRenderer::GetGpuText()
{
	return I18n::GetSingleton()->Format("menu.footer.gpu", { { "name", globals::state->adapterDescription } }, "GPU: {name}");
}

void MenuHeaderRenderer::RenderHeader(bool isDocked, bool showLogo, float uiScale, const Util::IconLoader::UIIcons& uiIcons,
	const std::vector<MenuShortcuts::Shortcut>& shortcuts, HelpState& help)
{
	if (!globals::menu) {
		logger::error("MenuHeaderRenderer::RenderHeader: globals::menu is null, cannot render header");
		return;
	}

	const auto title = GetTitleText();
	auto actionIcons = BuildActionIcons(uiIcons, shortcuts, help);

	// Dev builds: muted build label after the title
	const auto buildTag = GetBuildTagText();
	const bool showBuildTag = !buildTag.empty();
	const float buildTagWidth = showBuildTag ? ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize(buildTag.c_str()).x : 0.0f;
	auto drawBuildTag = [&]() {
		if (!showBuildTag)
			return;
		const float titleHeight = ImGui::GetItemRectSize().y;
		ImGui::SameLine();
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (titleHeight - ImGui::GetTextLineHeight()) * 0.5f);
		ImGui::TextDisabled("%s", buildTag.c_str());
	};

	if (isDocked) {
		// When docked, draw logo as a background watermark if available
		if (showLogo && uiIcons.logo.texture) {
			RenderWatermarkLogo(uiIcons);
		}

		// Draw action icons in the title bar area
		const float iconsLeft = RenderDockedIcons(actionIcons, uiScale);

		// The tab's width is the title text plus its padding and the dock and close buttons
		const ImVec2 windowPos = ImGui::GetWindowPos();
		const float titleBarHeight = ImGui::GetFrameHeight();
		const float titleRight = windowPos.x + ImGui::CalcTextSize(title.c_str()).x + titleBarHeight * 2.0f + ImGui::GetStyle().FramePadding.x * 2.0f;
		RenderSearchPill(true, titleRight, iconsLeft, windowPos.x + ImGui::GetWindowSize().x * 0.5f, windowPos.y, titleBarHeight, uiScale,
			MenuShortcuts::GetHint(shortcuts, MenuShortcuts::Ids::kGoTo));
	} else {
		// When not docked, show the custom header
		bool centerHeader = globals::menu->GetTheme().CenterHeader;
		const ImVec2 headerTop = ImGui::GetCursorScreenPos();
		float titleRight = headerTop.x;
		float iconsLeft = headerTop.x + ImGui::GetContentRegionAvail().x;
		float rowHeight = 0.0f;

		if (ImGui::BeginTable("##HeaderLayout", 2, ImGuiTableFlags_SizingStretchProp)) {
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
				contentWidth += buildTagWidth;

				// Slide left rather than clip into the icon column
				float offset = std::min(Util::GetCenterOffsetForContent(contentWidth), ImGui::GetContentRegionAvail().x - contentWidth);
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
			drawBuildTag();
			titleRight = ImGui::GetItemRectMax().x;
			rowHeight = std::max(ImGui::GetItemRectSize().y, logoSize);

			// Buttons on the right
			ImGui::TableNextColumn();
			iconsLeft = ImGui::GetCursorScreenPos().x;
			RenderUndockedIcons(actionIcons, uiScale);

			ImGui::EndTable();
			RenderSearchPill(false, titleRight, iconsLeft, ImGui::GetWindowPos().x + ImGui::GetWindowSize().x * 0.5f,
				headerTop.y + ImGui::GetStyle().CellPadding.y, rowHeight, uiScale,
				MenuShortcuts::GetHint(shortcuts, MenuShortcuts::Ids::kGoTo));
		}
	}

	// No separator needed for docked mode since icons are in title bar
	if (!isDocked) {
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal, ThemeManager::Constants::SEPARATOR_THICKNESS);
		ImGui::Spacing();
	}

	// At window level, where the ? icon and F1 both open it
	if (std::exchange(help.openPopover, false))
		ImGui::OpenPopup(kHelpPopupId);
	RenderHelpPopover(shortcuts, help);
	RenderFaqModal(help);
}

std::vector<MenuHeaderRenderer::ActionIcon> MenuHeaderRenderer::BuildActionIcons(const Util::IconLoader::UIIcons& uiIcons,
	const std::vector<MenuShortcuts::Shortcut>& shortcuts, HelpState& help)
{
	// In display order. An icon whose texture failed to load falls back to its glyph.
	std::vector<ActionIcon> actionIcons;

	// Tooltips name the action's shortcut, e.g. "Save settings (Ctrl+S)"
	auto* i18n = I18n::GetSingleton();
	const bool unsaved = globals::state->HasUnsavedChanges();
	const std::unordered_map<std::string, std::string> saveHint{ { "shortcut", MenuShortcuts::GetHint(shortcuts, MenuShortcuts::Ids::kSave) } };
	actionIcons.push_back({ uiIcons.saveSettings.texture, Icons::TI(ICON_TI_DEVICE_FLOPPY),
		unsaved ? i18n->Format("menu.header.save_unsaved_tooltip", saveHint, "Save unsaved changes ({shortcut})") :
				  i18n->Format("menu.header.save_tooltip", saveHint, "Save settings ({shortcut})"),
		[]() {
			SaveManager::SaveAll();
		},
		unsaved });
	actionIcons.push_back({ uiIcons.loadSettings.texture, Icons::TI(ICON_TI_REFRESH),
		T("menu.restore_settings", "Restore Saved Settings"),
		[]() {
			Util::RequestRestoreSavedSettingsConfirmation();
		} });
	actionIcons.push_back({ uiIcons.clearCache.texture, Icons::TI(ICON_TI_TRASH),
		T("menu.clear_shader_cache", "Clear Shader Cache"),  // the confirmation dialog explains what is cleared
		[]() {
			Util::RequestClearShaderCacheConfirmation();
		} });
	actionIcons.push_back({ nullptr, Icons::TI(ICON_TI_HELP),
		i18n->Format("menu.header.help_tooltip", { { "shortcut", MenuShortcuts::GetHint(shortcuts, MenuShortcuts::Ids::kHelp) } }, "Help, shortcuts and FAQ ({shortcut})"),
		[&help]() {
			help.openPopover = true;
		} });

	return actionIcons;
}

float MenuHeaderRenderer::RenderDockedIcons(const std::vector<ActionIcon>& actionIcons, float uiScale)
{
	if (actionIcons.empty())
		return ImGui::GetWindowPos().x + ImGui::GetWindowSize().x;

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
		if (it->unsaved)
			tintColor = ImGui::GetColorU32(Util::Colors::GetUnsaved());
		if (it->texture)
			fgDrawList->AddImage(it->texture, iconMin, iconMax, ImVec2(0, 0), ImVec2(1, 1), tintColor);
		else
			DrawIconGlyph(fgDrawList, iconMin, iconMax.x - iconMin.x, it->glyph, tintColor);

		// Handle interaction
		if (isHovered) {
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				it->callback();
			}

			// Set tooltip manually since we're drawing outside normal ImGui flow
			ImGui::SetTooltip("%s", it->tooltip.c_str());
		}
	}
	return iconX;
}

void MenuHeaderRenderer::RenderSearchPill(bool docked, float left, float right, float centerX, float top, float rowHeight, float uiScale,
	const std::string& shortcutHint)
{
	using Constants = ThemeManager::Constants;
	const char* label = T("ui.search", "Search...");
	const float height = std::min(rowHeight, ImGui::GetFrameHeight() * Constants::PALETTE_PILL_HEIGHT_SCALE * uiScale);
	if (height <= 0.0f)
		return;

	const float pad = Constants::PALETTE_PILL_PAD_X * uiScale;
	const float gap = Constants::PALETTE_PILL_GAP * uiScale;
	const float glyphSize = height * 0.5f;
	const float labelWidth = ImGui::CalcTextSize(label).x;
	const float hintWidth = shortcutHint.empty() ? 0.0f : ImGui::CalcTextSize(shortcutHint.c_str()).x;
	const float fullWidth = pad + glyphSize + pad * 0.5f + labelWidth + (hintWidth > 0.0f ? gap + hintWidth : 0.0f) + pad;

	// Centred when it fits; nudged out of the title's and icons' way; icon-only when tight; dropped when even that does not fit
	const float roomLeft = left + gap;
	const float roomRight = right - gap;
	const float room = roomRight - roomLeft;
	const bool iconOnly = room < fullWidth;
	const float width = iconOnly ? height : fullWidth;
	if (room < width)
		return;
	const float x = std::clamp(centerX - width * 0.5f, roomLeft, roomRight - width);
	const ImVec2 min(x, top + (rowHeight - height) * 0.5f);
	const ImVec2 max(min.x + width, min.y + height);

	bool hovered;
	bool down;
	bool clicked;
	if (docked) {
		// Drawn over the title bar like the icons, so the hit test is manual; the palette's own modal blocks it
		hovered = !GoToPalette::IsOpen() && ImGui::IsMouseHoveringRect(min, max, false);
		down = hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);
		clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
	} else {
		const ImVec2 saved = ImGui::GetCursorScreenPos();
		ImGui::SetCursorScreenPos(min);
		clicked = ImGui::InvisibleButton("##GoToPill", ImVec2(width, height));
		hovered = ImGui::IsItemHovered();
		down = ImGui::IsItemActive();
		ImGui::SetCursorScreenPos(saved);
	}

	ImDrawList* drawList = docked ? ImGui::GetForegroundDrawList() : ImGui::GetWindowDrawList();
	ImVec4 fill = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
	fill.w *= Constants::PALETTE_PILL_FILL_ALPHA;
	drawList->AddRectFilled(min, max, ImGui::GetColorU32(fill), height * 0.5f);
	drawList->AddRect(min, max, ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_Border, down ? 1.0f : 0.6f), height * 0.5f);

	const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
	if (iconOnly) {
		Icons::DrawCenteredGlyph(drawList, min, ImVec2(width, height), Icons::TI(ICON_TI_SEARCH), ImGui::GetColorU32(ImGuiCol_Text));
	} else {
		Icons::DrawCenteredGlyph(drawList, ImVec2(min.x + pad, min.y), ImVec2(glyphSize, height), Icons::TI(ICON_TI_SEARCH), textColor);
		const float textY = min.y + (height - ImGui::GetTextLineHeight()) * 0.5f;
		const float labelX = min.x + pad + glyphSize + pad * 0.5f;
		drawList->AddText(ImVec2(labelX, textY), textColor, label);
		if (hintWidth > 0.0f)
			drawList->AddText(ImVec2(max.x - pad - hintWidth, textY), textColor, shortcutHint.c_str());
	}

	if (hovered) {
		const std::string tooltip = shortcutHint.empty() ? std::string(T("menu.palette.pill_tooltip", "Go to a page, feature or setting")) :
		                                                   std::format("{} ({})", T("menu.palette.pill_tooltip", "Go to a page, feature or setting"), shortcutHint);
		ImGui::SetTooltip("%s", tooltip.c_str());
	}
	if (clicked)
		GoToPalette::Open();
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

	// Same size as an ImageButton, which pads the image by FramePadding on each side
	const ImVec2 framePadding = ImGui::GetStyle().FramePadding;
	const ImVec2 glyphButtonSize(imageSize.x + framePadding.x * 2.0f, imageSize.y + framePadding.y * 2.0f);

	// Draw action icons as ImageButtons, or glyph buttons where the texture is missing
	for (size_t i = 0; i < actionIcons.size(); ++i) {
		const auto& icon = actionIcons[i];
		const ImVec4 iconTint = icon.unsaved ? Util::Colors::GetUnsaved() : tintColor;

		std::string buttonId = std::format("##ActionBtn{}", i);

		bool clicked;
		if (icon.texture) {
			// Use ImageButton with reduced image size to minimize padding
			clicked = ImGui::ImageButton(buttonId.c_str(), icon.texture, imageSize, ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), iconTint);
		} else {
			clicked = ImGui::Button(buttonId.c_str(), glyphButtonSize);
			const ImVec2 min = ImGui::GetItemRectMin();
			DrawIconGlyph(ImGui::GetWindowDrawList(), ImVec2(min.x + framePadding.x, min.y + framePadding.y), imageSize.x, icon.glyph,
				ImGui::GetColorU32(iconTint));
		}
		if (clicked) {
			icon.callback();
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("%s", icon.tooltip.c_str());
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

void MenuHeaderRenderer::RenderWatermarkLogo(const Util::IconLoader::UIIcons& uiIcons)
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

void MenuHeaderRenderer::RenderHelpPopover(const std::vector<MenuShortcuts::Shortcut>& shortcuts, HelpState& help)
{
	if (!ImGui::BeginPopup(kHelpPopupId))
		return;

	// Escape closes the popover, not the menu
	if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
		globals::menu->ConsumeEscape();
		ImGui::CloseCurrentPopup();
	}

	{
		RoleFontGuard heading(Menu::FontRole::Subheading);
		ImGui::SeparatorText(T("menu.help.shortcuts", "Keyboard Shortcuts"));
	}
	if (ImGui::BeginTable("##HelpShortcuts", 2, ImGuiTableFlags_SizingFixedFit)) {
		for (const auto& row : MenuShortcuts::BuildHelpRows(shortcuts)) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(row.label);
			ImGui::TableNextColumn();
			ImGui::TextDisabled("%s", row.chords.c_str());
		}
		ImGui::EndTable();
	}

	{
		RoleFontGuard heading(Menu::FontRole::Subheading);
		ImGui::SeparatorText(T("menu.help.help", "Help"));
	}
	if (ImGui::Button(T("menu.help.faq", "FAQ..."))) {
		help.openFaq = true;
		ImGui::CloseCurrentPopup();
	}
	ImGui::Spacing();
	for (size_t i = 0; i < MenuLinks::kAll.size(); ++i) {
		const auto link = MenuLinks::kAll[i];
		if (i > 0) {
			ImGui::SameLine();
			ImGui::TextDisabled("%s", "\xC2\xB7");  // middle dot
			ImGui::SameLine();
		}
		ImGui::PushID(static_cast<int>(link));
		if (ImGui::TextLink(MenuLinks::GetLabel(link)))
			MenuLinks::Open(link);
		ImGui::PopID();
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextUnformatted(MenuLinks::GetUrl(link));
	}

	{
		RoleFontGuard heading(Menu::FontRole::Subheading);
		ImGui::SeparatorText(T("menu.help.about", "About"));
	}
	const auto buildTag = GetBuildTagText();
	ImGui::TextUnformatted(buildTag.empty() ? GetTitleText().c_str() : std::format("{} {}", GetTitleText(), buildTag).c_str());
	ImGui::TextUnformatted(GetGameVersionText().c_str());
	ImGui::TextUnformatted(GetGpuText().c_str());
	ImGui::TextUnformatted(GetSwapChainText().c_str());

	ImGui::EndPopup();
}

void MenuHeaderRenderer::RenderFaqModal(HelpState& help)
{
	if (std::exchange(help.openFaq, false)) {
		ImGui::OpenPopup(kFaqModalId);
		help.faqOpen = true;
	}

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(viewport->Size.x * ThemeManager::Constants::FAQ_MODAL_WIDTH_RATIO,
								 viewport->Size.y * ThemeManager::Constants::FAQ_MODAL_HEIGHT_RATIO),
		ImGuiCond_Appearing);

	const auto title = std::format("{}{}", T("menu.faq.title", "Frequently Asked Questions"), kFaqModalId);
	if (!Util::BeginPopupModalWithRoundedClose(title.c_str(), &help.faqOpen))
		return;

	// Escape closes the FAQ, not the menu
	if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
		globals::menu->ConsumeEscape();
		help.faqOpen = false;
		ImGui::CloseCurrentPopup();
	}
	HomePageRenderer::RenderFAQEntries();
	ImGui::EndPopup();
}
