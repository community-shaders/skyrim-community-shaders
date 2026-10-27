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
#include "Menu/FeatureListRenderer.h"
#include "Menu/GoToPalette.h"
#include "Menu/HomePageRenderer.h"
#include "Menu/IssuesPageRenderer.h"
#include "Menu/MenuIcons.h"
#include "Menu/MenuLinks.h"
#include "Plugin.h"
#include "SaveManager.h"
#include "ShaderCache.h"
#include "State.h"
#include "ThemeManager.h"
#include "Util.h"
#include "Utils/UIAnim.h"

namespace
{
	using RoleFontGuard = MenuFonts::FontRoleGuard;

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

bool MenuHeaderRenderer::GlyphIconButton(const char* id, Icons::GlyphRef glyph, float glyphBox, const char* tooltip, bool active)
{
	const ImVec2 framePadding = ImGui::GetStyle().FramePadding;
	const ImVec2 buttonSize(glyphBox + framePadding.x * 2.0f, glyphBox + framePadding.y * 2.0f);

	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
	// The button itself is invisible; its fill is drawn from the eased hover / press / active blend
	ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32_BLACK_TRANS);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32_BLACK_TRANS);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32_BLACK_TRANS);
	const bool clicked = ImGui::Button(id, buttonSize);
	const ImVec2 min = ImGui::GetItemRectMin();
	const ImVec2 max = ImGui::GetItemRectMax();
	ImGui::PopStyleColor(3);
	ImGui::PopStyleVar();
	const auto blend = Util::Anim::ItemBlend(ImGui::GetItemID(), ImGui::IsItemHovered(), ImGui::IsItemActive(), active);
	if (const float fill = std::max(blend.hover * ThemeManager::Constants::HEADER_ICON_HOVER_ALPHA, blend.selected); fill > 0.0f)
		ImGui::GetWindowDrawList()->AddRectFilled(min, max,
			Util::Anim::ScaleAlphaU32(ImGui::GetColorU32(blend.selected > blend.hover ? ImGuiCol_ButtonHovered : ImGuiCol_FrameBgHovered), fill),
			ImGui::GetStyle().FrameRounding);

	ImVec4 tint = globals::menu->GetSettings().Theme.UseMonochromeIcons ? globals::menu->GetSettings().Theme.Palette.Text : ImVec4(1, 1, 1, 1);
	tint = Util::Anim::Lerp(tint, Util::Colors::GetAccent(), blend.selected);
	DrawIconGlyph(ImGui::GetWindowDrawList(), ImVec2(min.x + framePadding.x, min.y + framePadding.y), glyphBox, glyph, ImGui::GetColorU32(tint));
	if (tooltip) {
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::TextUnformatted(tooltip);
	}
	return clicked;
}

bool MenuHeaderRenderer::RenderNavButton(bool docked, ImVec2 min, float size, const char* id, Icons::GlyphRef glyph, const char* tooltip,
	bool current, const ImVec4* tintOverride, const std::function<void()>& onClick)
{
	const ImRect rect(min, ImVec2(min.x + size, min.y + size));

	bool hovered;
	bool down;
	bool clicked;
	if (docked) {
		// Drawn over the title bar like the action icons, so the hit test is manual; the palette's own modal blocks it
		hovered = !GoToPalette::IsOpen() && ImGui::IsMouseHoveringRect(rect.Min, rect.Max, false);
		down = hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);
		clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
	} else {
		const ImVec2 saved = ImGui::GetCursorScreenPos();
		ImGui::SetCursorScreenPos(min);
		clicked = ImGui::InvisibleButton(id, ImVec2(size, size));
		hovered = ImGui::IsItemHovered();
		down = ImGui::IsItemActive();
		ImGui::SetCursorScreenPos(saved);
	}

	ImDrawList* drawList = docked ? ImGui::GetForegroundDrawList() : ImGui::GetWindowDrawList();
	Util::DrawAnimatedButtonHighlight(rect, ImGui::GetID(id), hovered, down, current, drawList);

	ImVec4 tint = globals::menu->GetSettings().Theme.UseMonochromeIcons ? globals::menu->GetSettings().Theme.Palette.Text : ImVec4(1, 1, 1, 1);
	if (tintOverride)
		tint = *tintOverride;
	else if (current)
		tint = Util::Colors::GetAccent();
	if (!current && !hovered)
		tint.w *= ThemeManager::Constants::HEADER_HOME_IDLE_ALPHA;
	if (tintOverride)  // The issues icon breathes gently to draw the eye
		tint.w *= 1.0f - ThemeManager::Constants::ANIM_PULSE_AMPLITUDE * Util::Anim::Pulse();
	const float pad = ThemeManager::Constants::DOCKED_ICON_PADDING_REDUCTION;
	DrawIconGlyph(drawList, ImVec2(min.x + pad, min.y + pad), size - pad * 2.0f, glyph, ImGui::GetColorU32(tint));

	if (hovered)
		ImGui::SetTooltip("%s", tooltip);
	if (clicked)
		onClick();
	return clicked;
}

float MenuHeaderRenderer::RenderNavButtons(bool docked, ImVec2 min, float size, float gap)
{
	float x = min.x;
	RenderNavButton(docked, ImVec2(x, min.y), size, "##HeaderHome", Icons::TI(MenuIcons::kHome), T("menu.header.home_tooltip", "Home"),
		HomePageRenderer::IsShowing() && !HomePageRenderer::IsAttentionFocused(), nullptr,
		[]() { globals::menu->SelectFeatureMenu(std::string(MenuPageIds::kHome)); });
	x += size + gap;

	if (const auto issues = IssuesPageRenderer::GetFeatureIssueCount()) {
		const ImVec4 error = Util::Colors::GetError();
		const auto tooltip = I18n::GetSingleton()->Format("menu.header.issues_tooltip", { { "count", std::to_string(issues) } },
			"{count} feature issue(s) - click to view");
		RenderNavButton(docked, ImVec2(x, min.y), size, "##HeaderIssues", Icons::TI(MenuIcons::kIssues), tooltip.c_str(), false, &error,
			[]() { HomePageRenderer::FocusNeedsAttention(); });
		x += size + gap;
	}
	return x - min.x;
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
	const std::vector<MenuShortcuts::Shortcut>& shortcuts)
{
	if (!globals::menu) {
		logger::error("MenuHeaderRenderer::RenderHeader: globals::menu is null, cannot render header");
		return;
	}

	const auto title = GetTitleText();
	auto actionIcons = BuildActionIcons(uiIcons, shortcuts);

	// Dev builds: muted build label after the title
	const auto buildTag = GetBuildTagText();
	const bool showBuildTag = !buildTag.empty();
	const float buildTagWidth = showBuildTag ? ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize(buildTag.c_str()).x : 0.0f;
	float titleInkScreenY = 0.0f;  // Screen y of the title's ink centre; the build label centres on it
	auto drawBuildTag = [&]() {
		if (!showBuildTag)
			return;
		ImGui::SameLine();
		ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, titleInkScreenY - Util::GetTextInkCenterY()));
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
		float titleRight = windowPos.x + ImGui::CalcTextSize(title.c_str()).x + titleBarHeight * 2.0f + ImGui::GetStyle().FramePadding.x * 2.0f;
		// The tab hugs the window's left edge, so Home sits right after it, then the search pill
		const float homeSize = GetHeaderIconSize(true, uiScale);
		const float homeLeft = titleRight + ThemeManager::Constants::DOCKED_ICON_SPACING * uiScale;
		titleRight = homeLeft + RenderNavButtons(true, ImVec2(homeLeft, windowPos.y + (titleBarHeight - homeSize) * 0.5f), homeSize,
									ThemeManager::Constants::DOCKED_ICON_SPACING * uiScale);
		RenderSearchPill(true, titleRight, iconsLeft, windowPos.y, titleBarHeight, uiScale,
			MenuShortcuts::GetHint(shortcuts, MenuShortcuts::Ids::kGoTo));
	} else {
		// When not docked, show the custom header
		bool centerHeader = globals::menu->GetTheme().CenterHeader;

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

			// Home button at the column's left edge, centred on the title row; the title makes room for it
			const float homeSize = GetHeaderIconSize(false, uiScale);
			const float navGap = ImGui::GetStyle().ItemSpacing.x;
			const size_t navCount = IssuesPageRenderer::GetFeatureIssueCount() > 0 ? 2 : 1;
			const float homeReserve = static_cast<float>(navCount) * (homeSize + navGap);
			const ImVec2 columnStart = ImGui::GetCursorScreenPos();
			const float estimatedRowHeight = std::max(logoSize, ImGui::GetFontSize() * textScaleFactor);
			RenderNavButtons(false, ImVec2(columnStart.x, columnStart.y + (estimatedRowHeight - homeSize) * 0.5f), homeSize, navGap);

			// Width of the title block (logo, text and build label)
			float contentWidth = 0.0f;
			if (showLogo) {
				float logoAspectRatio = uiIcons.logo.size.x / uiIcons.logo.size.y;
				contentWidth = (logoSize * logoAspectRatio) + ImGui::GetStyle().ItemSpacing.x;
			}
			{
				RoleFontGuard titleFont(Menu::FontRole::Title);
				ImGui::SetWindowFontScale(textScaleFactor);
				contentWidth += ImGui::CalcTextSize(title.c_str()).x;
				ImGui::SetWindowFontScale(1.0f);
			}
			contentWidth += buildTagWidth;

			// The search pill follows the nav buttons; the title takes the room that is left
			const float columnWidth = ImGui::GetContentRegionAvail().x;
			const float pillEnd = RenderSearchPill(false, columnStart.x + homeReserve, columnStart.x + columnWidth - contentWidth, columnStart.y,
				estimatedRowHeight, uiScale, MenuShortcuts::GetHint(shortcuts, MenuShortcuts::Ids::kGoTo));
			const float reserve = std::max(homeReserve, pillEnd - columnStart.x);

			if (centerHeader) {
				// Centred in the space right of the pill; slides left rather than clip into the icon column
				const float offset = reserve + std::max(0.0f, (columnWidth - reserve - contentWidth) * 0.5f);
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
			} else {
				// Add padding for left-aligned layout
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + reserve + ThemeManager::Constants::CURSOR_POSITION_PADDING);
			}

			// Centre the title's ink (not its line box) on the row the nav buttons and pill are centred on
			{
				RoleFontGuard titleFont(Menu::FontRole::Title);
				ImGui::SetWindowFontScale(textScaleFactor);
				ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, Util::CenterTextTopY(columnStart.y, estimatedRowHeight)));
				titleInkScreenY = ImGui::GetCursorScreenPos().y + Util::GetTextInkCenterY();
				ImGui::SetWindowFontScale(1.0f);
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

			// Buttons on the right
			ImGui::TableNextColumn();
			RenderUndockedIcons(actionIcons, uiScale);

			ImGui::EndTable();
		}
	}

	// No separator needed for docked mode since icons are in title bar
	if (!isDocked) {
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal, ThemeManager::Constants::SEPARATOR_THICKNESS);
		ImGui::Spacing();
	}
}

std::vector<MenuHeaderRenderer::ActionIcon> MenuHeaderRenderer::BuildActionIcons(const Util::IconLoader::UIIcons& uiIcons,
	const std::vector<MenuShortcuts::Shortcut>& shortcuts)
{
	// In display order. An icon whose texture failed to load falls back to its glyph.
	std::vector<ActionIcon> actionIcons;

	// Tooltips name the action's shortcut, e.g. "Save settings (Ctrl+S)"
	auto* i18n = I18n::GetSingleton();
	const bool unsaved = globals::state->HasUnsavedChanges();
	const std::unordered_map<std::string, std::string> saveHint{ { "shortcut", MenuShortcuts::GetHint(shortcuts, MenuShortcuts::Ids::kSave) } };
	actionIcons.push_back({ uiIcons.saveSettings.texture, Icons::TI(MenuIcons::kSave),
		unsaved ? i18n->Format("menu.header.save_unsaved_tooltip", saveHint, "Save unsaved changes ({shortcut})") :
				  i18n->Format("menu.header.save_tooltip", saveHint, "Save settings ({shortcut})"),
		[]() {
			SaveManager::SaveAll();
		},
		unsaved });
	actionIcons.push_back({ uiIcons.loadSettings.texture, Icons::TI(MenuIcons::kRestore),
		T("menu.restore_settings", "Restore Saved Settings"),
		[]() {
			Util::RequestRestoreSavedSettingsConfirmation();
		} });
	actionIcons.push_back({ uiIcons.clearCache.texture, Icons::TI(MenuIcons::kClearCache),
		T("menu.clear_shader_cache", "Clear Shader Cache"),  // the confirmation dialog explains what is cleared
		[]() {
			Util::RequestClearShaderCacheConfirmation();
		} });
	return actionIcons;
}

float MenuHeaderRenderer::GetHeaderIconSize(bool docked, float uiScale)
{
	using Constants = ThemeManager::Constants;
	const float multiplier = docked ? Constants::DOCKED_ICON_SIZE_MULTIPLIER : Constants::HEADER_BASE_ICON_MULTIPLIER;
	return ImGui::GetFontSize() * multiplier * Constants::HEADER_ICON_SCALE * uiScale;
}

float MenuHeaderRenderer::RenderDockedIcons(const std::vector<ActionIcon>& actionIcons, float uiScale)
{
	if (actionIcons.empty())
		return ImGui::GetWindowPos().x + ImGui::GetWindowSize().x;

	// Docked: Draw larger icons in the title bar using foreground draw list
	const float iconSize = GetHeaderIconSize(true, uiScale);
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
		Util::DrawAnimatedButtonHighlight(interactionRect, ImGui::GetID(it->tooltip.c_str()), isHovered, isHovered && ImGui::IsMouseDown(ImGuiMouseButton_Left), false, fgDrawList);

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
		if (it->texture) {
			// Whole-pixel rect, and the mip level nearest this size, so the PNG edges stay crisp
			const ImVec2 snappedMin = Util::IconLoader::SnapToPixel(iconMin);
			const ImVec2 snappedSize = Util::IconLoader::SnapToPixel(ImVec2(iconMax.x - iconMin.x, iconMax.y - iconMin.y));
			fgDrawList->AddImage(Util::IconLoader::ViewForSize(it->texture, snappedSize), snappedMin,
				ImVec2(snappedMin.x + snappedSize.x, snappedMin.y + snappedSize.y), ImVec2(0, 0), ImVec2(1, 1), tintColor);
		} else
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

float MenuHeaderRenderer::RenderSearchPill(bool docked, float left, float right, float top, float rowHeight, float uiScale,
	const std::string& shortcutHint)
{
	using Constants = ThemeManager::Constants;
	// Same size as the Home / issues buttons it sits beside
	const float size = GetHeaderIconSize(docked, uiScale);
	const float gap = Constants::DOCKED_ICON_SPACING * uiScale;
	if (size <= 0.0f || right - gap - left < size)
		return left;

	const std::string tooltip = shortcutHint.empty() ? std::string(T("menu.palette.pill_tooltip", "Go to...")) :
	                                                   std::format("{} ({})", T("menu.palette.pill_tooltip", "Go to..."), shortcutHint);
	RenderNavButton(docked, ImVec2(left, top + (rowHeight - size) * 0.5f), size, "##HeaderGoTo", Icons::TI(MenuIcons::kSearch), tooltip.c_str(),
		false, nullptr, []() { GoToPalette::Open(); });
	return left + size + gap;
}

void MenuHeaderRenderer::RenderUndockedIcons(const std::vector<ActionIcon>& actionIcons, float uiScale)
{
	if (actionIcons.empty())
		return;

	// Undocked: Draw icons as ImageButtons in a table column
	const float iconSize = GetHeaderIconSize(false, uiScale);
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
			const ImVec2 snappedSize = Util::IconLoader::SnapToPixel(imageSize);
			clicked = ImGui::ImageButton(buttonId.c_str(), Util::IconLoader::ViewForSize(icon.texture, snappedSize), snappedSize, ImVec2(0, 0),
				ImVec2(1, 1), ImVec4(0, 0, 0, 0), iconTint);
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

	const ImVec2 logoDrawMin = Util::IconLoader::SnapToPixel(logoMin);
	const ImVec2 logoDrawSize = Util::IconLoader::SnapToPixel(ImVec2(logoMax.x - logoMin.x, logoMax.y - logoMin.y));
	drawList->AddImage(Util::IconLoader::ViewForSize(uiIcons.logo.texture, logoDrawSize), logoDrawMin,
		ImVec2(logoDrawMin.x + logoDrawSize.x, logoDrawMin.y + logoDrawSize.y), ImVec2(0, 0), ImVec2(1, 1), watermarkColor);
}
