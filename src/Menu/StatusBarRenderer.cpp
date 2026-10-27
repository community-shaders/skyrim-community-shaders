#include "Menu/StatusBarRenderer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <string_view>

#include "CSEditor/Browser/BrowserWidgets.h"
#include "FeatureIssues.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "IconsTabler.h"
#include "Menu.h"
#include "Menu/FeatureListRenderer.h"
#include "Menu/HomePageRenderer.h"
#include "Menu/MenuHeaderRenderer.h"
#include "Menu/MenuIcons.h"
#include "Menu/ThemeManager.h"
#include "SaveManager.h"
#include "ShaderCache.h"
#include "State.h"
#include "Util.h"
#include "Utils/UI.h"
#include "Utils/UIAnim.h"

namespace
{
	using Constants = ThemeManager::Constants;

	/** @brief Edge of the Settings / Profiling icon buttons. */
	float NavButtonSize()
	{
		return ImGui::GetFontSize() * Constants::STATUS_BAR_NAV_BUTTON_FONT_RATIO;
	}

	/** @brief Height of the bar's row: the tallest item in it, so every item can be centred on the same line. */
	float RowHeight()
	{
		return std::max(ImGui::GetFrameHeight(), NavButtonSize());
	}

	/** @brief Where the row's items are laid out: its top-left and height, plus the x the next item starts at. */
	struct Row
	{
		float x;
		float top;
		float height;

		/** @brief Puts the cursor where an item of the given height starts, centred on the row. */
		void Place(float itemHeight) const { ImGui::SetCursorScreenPos(ImVec2(x, top + (height - itemHeight) * 0.5f)); }
		/** @brief Puts the cursor where a line of text starts so its ink is centred on the row. */
		void PlaceText() const { ImGui::SetCursorScreenPos(ImVec2(x, Util::CenterTextTopY(top, height))); }
		/** @brief Continues after the item just submitted. */
		void Advance(float gap) { x = ImGui::GetItemRectMax().x + gap; }
	};

	/** @brief A clickable status part: a glyph and a label in one colour, both centred on the row. */
	bool Segment(Row& row, const char* id, Icons::GlyphRef icon, const char* label, const ImVec4& color, const char* tooltip)
	{
		const ImGuiStyle& style = ImGui::GetStyle();
		const ImVec2 iconSize = Icons::CalcGlyphSize(icon);
		const float labelWidth = ImGui::CalcTextSize(label).x;
		const float gap = Icons::LabeledButtonGap(icon, label);
		const float height = ImGui::GetFrameHeight();
		row.Place(height);
		ImGui::PushID(id);
		const bool clicked = ImGui::InvisibleButton("##segment", ImVec2(iconSize.x + gap + labelWidth + style.FramePadding.x * 2.0f, height));
		const ImVec2 min = ImGui::GetItemRectMin();
		auto* drawList = ImGui::GetWindowDrawList();
		if (ImGui::IsItemHovered() || ImGui::IsItemActive())
			drawList->AddRectFilled(min, ImGui::GetItemRectMax(),
				ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered), style.FrameRounding);
		const ImU32 col = ImGui::GetColorU32(color);
		float x = min.x + style.FramePadding.x;
		Icons::DrawCenteredGlyph(drawList, ImVec2(x, min.y), ImVec2(iconSize.x, height), icon, col);
		drawList->AddText(ImVec2(x + iconSize.x + gap, Util::CenterTextTopY(min.y, height)), col, label);
		ImGui::PopID();
		if (tooltip)
			Util::AddTooltip(tooltip);
		row.Advance(0.0f);
		return clicked;
	}

	/** @brief Icon button of the bar's left group: eased hover / press / selected fill, glyph larger than the bar's text glyphs. */
	bool NavButton(Row& row, const char* id, Icons::GlyphRef glyph, const char* tooltip, bool selected)
	{
		const float size = NavButtonSize();
		row.Place(size);
		const bool clicked = ImGui::InvisibleButton(id, ImVec2(size, size));
		const auto blend = Util::Anim::ItemBlend(ImGui::GetItemID(), ImGui::IsItemHovered(), ImGui::IsItemActive(), selected);
		auto* drawList = ImGui::GetWindowDrawList();
		const ImVec2 min = ImGui::GetItemRectMin();
		const ImVec2 max = ImGui::GetItemRectMax();
		const float rounding = ImGui::GetStyle().FrameRounding;
		if (blend.hover > 0.0f)
			drawList->AddRectFilled(min, max, Util::Anim::ScaleAlphaU32(ImGui::GetColorU32(ImGuiCol_FrameBgHovered), blend.hover), rounding);
		if (blend.press > 0.0f)
			drawList->AddRectFilled(min, max, Util::Anim::ScaleAlphaU32(ImGui::GetColorU32(ImGuiCol_ButtonActive), blend.press), rounding);
		if (blend.selected > 0.0f)
			drawList->AddRectFilled(min, max, Util::Anim::ScaleAlphaU32(ImGui::GetColorU32(ImGuiCol_Header), blend.selected), rounding);
		const ImVec4 resting = Util::Anim::Lerp(Util::Colors::GetSecondary(), ImGui::GetStyleColorVec4(ImGuiCol_Text), blend.hover);
		const ImU32 color = ImGui::GetColorU32(Util::Anim::Lerp(resting, Util::Colors::GetAccent(), blend.selected));
		if (ImFont* font = Icons::GetFont(glyph); font && glyph) {
			// PushFont takes a size before the style and window font scales; undo them so the ratio is in font sizes
			const ImGuiContext& context = *ImGui::GetCurrentContext();
			const float fontScale = context.FontSizeBase > 0.0f ? context.FontSize / context.FontSizeBase : 1.0f;
			ImGui::PushFont(font, ImGui::GetFontSize() * Constants::STATUS_BAR_NAV_GLYPH_FONT_RATIO / std::max(fontScale, 0.01f));
			drawList->AddText(Icons::GetCenteredGlyphPos(min, ImVec2(size, size), glyph.utf8), color, glyph.utf8);
			ImGui::PopFont();
		}
		if (tooltip)
			Util::AddTooltip(tooltip, Util::kTooltipWhenDisabled);
		row.Advance(0.0f);
		return clicked;
	}

	/** @brief A thin vertical line of partial height, in the theme's separator colour, centred on the row. */
	void Divider(Row& row)
	{
		const float gap = ImGui::GetStyle().ItemSpacing.x;
		const float height = ImGui::GetFontSize() * Constants::STATUS_BAR_DIVIDER_FONT_RATIO;
		const float x = std::floor(row.x + gap) + 0.5f;
		const float y = row.top + (row.height - height) * 0.5f;
		ImGui::GetWindowDrawList()->AddLine(ImVec2(x, y), ImVec2(x, y + height), ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
		row.x = x + 0.5f + gap;
	}

	/** @brief Settings and Profiling, then a divider; the left end of the bar. */
	void NavGroup(Row& row)
	{
		const float gap = ImGui::GetStyle().ItemSpacing.x;
		const auto page = [&](std::string_view id, const char* widgetId, const char* glyph, const char* tooltip) {
			if (NavButton(row, widgetId, Icons::TI(glyph), tooltip, FeatureListRenderer::IsPageOpen(id)))
				FeatureListRenderer::RequestPage(id);
			row.x += gap;
		};
		page(MenuPageIds::kSettings, "##StatusSettings", MenuIcons::kSettings, T("menu.features.settings", "Settings"));
		Divider(row);
		page(MenuPageIds::kProfiling, "##StatusProfiling", MenuIcons::kProfiling, T("menu.features.profiling", "Profiling"));
		Divider(row);
	}
}

namespace StatusBarRenderer
{
	float GetHeight()
	{
		// The gap before the separator (left by the content above it), the separator, then equal gaps above and below the row
		const float gap = ImGui::GetStyle().ItemSpacing.y;
		return gap * 3.0f + Constants::SEPARATOR_THICKNESS + RowHeight();
	}

	void Render(const std::vector<MenuShortcuts::Shortcut>& shortcuts, bool showStatus)
	{
		const float gap = ImGui::GetStyle().ItemSpacing.y;
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal, Constants::SEPARATOR_THICKNESS);

		ImGui::PushID("##StatusBar");
		Row row{ origin.x, origin.y + Constants::SEPARATOR_THICKNESS + gap, RowHeight() };
		const float left = row.x;
		const float right = left + ImGui::GetContentRegionAvail().x;

		NavGroup(row);
		if (!showStatus) {
			ImGui::PopID();
			return;
		}

		auto* i18n = I18n::GetSingleton();
		const float segmentGap = ImGui::GetStyle().ItemSpacing.x * Constants::STATUS_BAR_SEGMENT_GAP_SCALE;
		bool first = true;
		const auto nextSegment = [&]() {
			if (!first)
				row.x += segmentGap;
			first = false;
		};

		// Feature issues come first, in the error colour; clicking opens Needs Attention on Home
		if (FeatureIssues::HasFeatureIssues()) {
			nextSegment();
			const auto label = i18n->Format("menu.status.feature_issues",
				{ { "count", std::to_string(FeatureIssues::GetFeatureIssues().size()) } }, "Feature issues: {count}");
			if (Segment(row, "##StatusIssues", Icons::TI(MenuIcons::kIssues), label.c_str(), Util::Colors::GetError(),
					T("menu.status.issues_tooltip", "Show the issues on Home")))
				HomePageRenderer::FocusNeedsAttention();
		}

		// Unsaved changes, with a Save link
		if (globals::state->HasUnsavedChanges()) {
			nextSegment();
			const ImVec4 unsavedColor = Util::Colors::GetUnsaved();
			const float spacing = ImGui::GetStyle().ItemSpacing.x;
			row.Place(ImGui::GetTextLineHeight());
			Util::DrawInlineIndicatorDot(ImGui::GetColorU32(unsavedColor), true);
			row.Advance(spacing);
			row.PlaceText();
			ImGui::TextColored(unsavedColor, "%s", T("menu.status.unsaved", "Unsaved changes"));
			row.Advance(spacing);
			row.PlaceText();
			if (BrowserUI::Link("##StatusSave", T("menu.status.save", "Save")))
				SaveManager::SaveAll();
			Util::AddTooltip(i18n->Format("menu.header.save_tooltip",
									 { { "shortcut", MenuShortcuts::GetHint(shortcuts, MenuShortcuts::Ids::kSave) } },
									 "Save settings ({shortcut})")
					.c_str());
			row.Advance(0.0f);
		}

		auto* shaderCache = globals::shaderCache;

		// Shader compile progress while it runs
		if (shaderCache->IsCompiling()) {
			nextSegment();
			const ImVec4 infoColor = Util::Colors::GetInfo();
			const float spacing = ImGui::GetStyle().ItemSpacing.x;
			row.Place(ImGui::GetTextLineHeight());
			{
				ImGui::PushStyleColor(ImGuiCol_Text, infoColor);
				Icons::Text(Icons::TI(ICON_TI_LOADER_2));
				ImGui::PopStyleColor();
			}
			row.Advance(spacing);
			row.PlaceText();
			ImGui::TextColored(infoColor, "%s",
				i18n->Format("menu.status.compiling",
						{ { "done", std::to_string(shaderCache->GetCompletedTasks()) }, { "total", std::to_string(shaderCache->GetTotalTasks()) } },
						"Compiling shaders {done}/{total}")
					.c_str());
			row.Advance(0.0f);
		}

		// Failed shaders; clicking shows or hides the in-game error message
		if (const auto failed = shaderCache->GetFailedTasks()) {
			nextSegment();
			const auto label = i18n->Format("menu.status.failed_shaders", { { "count", std::to_string(failed) } }, "Failed shaders: {count}");
			if (Segment(row, "##StatusFailed", Icons::TI(ICON_TI_CIRCLE_X), label.c_str(), Util::Colors::GetError(),
					T("menu.toggle_error_message_tooltip",
						"Hide or show the shader failure message. "
						"Your installation is broken and will likely see errors in game. "
						"Please double check you have updated all features and that your load order is correct. "
						"See CommunityShaders.log for details and check the Nexus Mods page or Discord server.")))
				shaderCache->ToggleErrorMessages();
		}

		// Nothing needs attention: say so, so the left side is never empty
		if (first) {
			nextSegment();
			row.Place(ImGui::GetTextLineHeight());
			Icons::Text(Icons::TI(MenuIcons::kSave));
			row.Advance(ImGui::GetStyle().ItemSpacing.x);
			row.PlaceText();
			ImGui::TextDisabled("%s", T("menu.status.all_saved", "All changes saved"));
			row.Advance(0.0f);
		}

		// GPU, runtime and swap chain on the right, in whatever room the left side leaves
		std::string info;
		const auto addPart = [&info](const std::string& part) {
			if (part.empty())
				return;
			if (!info.empty())
				info += "  ·  ";
			info += part;
		};
		addPart(globals::state->adapterDescription);
		addPart(std::format("{} {}", magic_enum::enum_name(REL::Module::GetRuntime()),
			Util::GetFormattedVersion(REL::Module::get().version())));
		addPart(MenuHeaderRenderer::GetSwapChainText());

		// Keep clear of the window's bottom-right resize grip
		const float gripClearance = ImGui::GetFontSize() * Constants::STATUS_BAR_GRIP_CLEARANCE_SCALE;
		const float helpWidth = HomePageRenderer::GetHelpButtonWidth();
		const float helpRight = right - gripClearance;
		const float available = helpRight - helpWidth - ImGui::GetStyle().ItemSpacing.x - (row.x + segmentGap);
		const float width = std::min(ImGui::CalcTextSize(info.c_str()).x, available);
		if (width >= ImGui::GetFontSize()) {
			row.x = helpRight - helpWidth - ImGui::GetStyle().ItemSpacing.x - width;
			row.PlaceText();
			BrowserUI::EllipsizedText(info.c_str(), width, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		}

		// Help sits at the right end, clear of the resize grip
		row.x = helpRight - helpWidth;
		row.Place(ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f);
		HomePageRenderer::RenderHelpButton();

		ImGui::PopID();
	}
}
