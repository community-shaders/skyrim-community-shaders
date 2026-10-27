#include "Menu/GoToPalette.h"

#include "Globals.h"
#include "I18n/I18n.h"
#include "Menu.h"
#include "Menu/GoToIndex.h"
#include "Menu/ThemeManager.h"
#include "Utils/UI.h"
#include "Utils/UIAnim.h"
#include "Utils/UIGlass.h"

#include <algorithm>
#include <array>
#include <imgui_internal.h>
#include <string>

namespace GoToPalette
{
	namespace
	{
		constexpr const char* kPopupId = "###GoToPalette";

		bool g_openRequested = false;
		bool g_open = false;

		struct State
		{
			std::array<char, 256> query{};
			std::string lastQuery;
			std::vector<const GoToIndex::Entry*> results;
			int selected = 0;
			bool refresh = true;
		};
		State g_state;

		const char* KindLabel(GoToIndex::Kind kind)
		{
			switch (kind) {
			case GoToIndex::Kind::Page:
				return T("menu.palette.badge_page", "Page");
			case GoToIndex::Kind::Feature:
				return T("menu.palette.badge_feature", "Feature");
			case GoToIndex::Kind::Setting:
				return T("menu.palette.badge_setting", "Setting");
			case GoToIndex::Kind::Command:
			default:
				return T("menu.palette.badge_command", "Command");
			}
		}

		ImVec4 KindColor(GoToIndex::Kind kind)
		{
			switch (kind) {
			case GoToIndex::Kind::Page:
				return Util::Colors::GetInfo();
			case GoToIndex::Kind::Feature:
				return Util::Colors::GetSuccess();
			case GoToIndex::Kind::Setting:
				return Util::Colors::GetAccent();
			case GoToIndex::Kind::Command:
			default:
				return Util::Colors::GetWarning();
			}
		}

		/** @brief Widest badge label plus padding, so every row's title starts at the same x. */
		float MeasureBadgeWidth(float scale)
		{
			float widest = 0.0f;
			for (const auto kind : { GoToIndex::Kind::Page, GoToIndex::Kind::Feature, GoToIndex::Kind::Setting, GoToIndex::Kind::Command })
				widest = std::max(widest, ImGui::CalcTextSize(KindLabel(kind)).x);
			return widest + ThemeManager::Constants::PALETTE_BADGE_PAD_X * scale * 2.0f;
		}

		void DrawBadge(ImDrawList* drawList, ImVec2 min, ImVec2 size, GoToIndex::Kind kind, bool dimmed)
		{
			ImVec4 tint = KindColor(kind);
			if (dimmed)
				tint.w *= 0.5f;
			ImVec4 fill = tint;
			fill.w *= ThemeManager::Constants::PALETTE_BADGE_FILL_ALPHA;
			drawList->AddRectFilled(min, ImVec2(min.x + size.x, min.y + size.y), ImGui::GetColorU32(fill),
				ThemeManager::Constants::PALETTE_BADGE_ROUNDING * Util::GetUIScale());
			const char* label = KindLabel(kind);
			const ImVec2 textSize = ImGui::CalcTextSize(label);
			drawList->AddText(ImVec2(min.x + (size.x - textSize.x) * 0.5f, min.y + (size.y - textSize.y) * 0.5f), ImGui::GetColorU32(tint), label);
		}

		/** @brief Re-ranks the results when the query changed since the last frame. */
		void RunQuery()
		{
			const std::string query(g_state.query.data());
			if (!g_state.refresh && query == g_state.lastQuery)
				return;
			g_state.refresh = false;
			g_state.lastQuery = query;
			g_state.results = GoToIndex::Search(query, ThemeManager::Constants::PALETTE_MAX_RESULTS);
			g_state.selected = 0;
		}

		/** @brief Draws one result row into the rect the Selectable took; the chord sits at the right edge. */
		void DrawRow(const GoToIndex::Entry& entry, ImVec2 min, ImVec2 max, float badgeWidth, float scale)
		{
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const auto& style = ImGui::GetStyle();
			const float lineHeight = ImGui::GetTextLineHeight();
			const float padY = ThemeManager::Constants::PALETTE_ROW_PAD_Y * scale;
			const float badgeHeight = lineHeight + style.FramePadding.y;

			DrawBadge(drawList, ImVec2(min.x + style.FramePadding.x, min.y + padY), ImVec2(badgeWidth, badgeHeight), entry.kind, entry.dimmed);

			const float textX = min.x + style.FramePadding.x * 2.0f + badgeWidth;
			float rightEdge = max.x - style.FramePadding.x;
			if (!entry.chord.empty()) {
				const float chordWidth = ImGui::CalcTextSize(entry.chord.c_str()).x;
				drawList->AddText(ImVec2(rightEdge - chordWidth, min.y + padY), ImGui::GetColorU32(ImGuiCol_TextDisabled), entry.chord.c_str());
				rightEdge -= chordWidth + style.ItemSpacing.x;
			}

			ImVec4 textColor = style.Colors[ImGuiCol_Text];
			if (entry.dimmed)
				textColor.w *= 0.5f;
			const ImVec4 clip(textX, min.y, rightEdge, max.y);
			drawList->AddText(nullptr, 0.0f, ImVec2(textX, min.y + padY), ImGui::GetColorU32(textColor), entry.title.c_str(), nullptr, 0.0f, &clip);
			if (!entry.breadcrumb.empty()) {
				drawList->AddText(nullptr, 0.0f, ImVec2(textX, min.y + padY + lineHeight + style.ItemInnerSpacing.y),
					ImGui::GetColorU32(ImGuiCol_TextDisabled), entry.breadcrumb.c_str(), nullptr, 0.0f, &clip);
			}
		}
	}

	void Open()
	{
		g_openRequested = true;
	}

	bool IsOpen()
	{
		return g_open || g_openRequested;
	}

	void Draw(const std::vector<MenuShortcuts::Shortcut>& shortcuts)
	{
		if (g_openRequested) {
			g_openRequested = false;
			GoToIndex::Refresh(shortcuts);
			g_state = {};
			ImGui::OpenPopup(kPopupId);
		}

		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		// Fades in and grows slightly from the centre each time it opens
		const float enter = ImGui::IsPopupOpen(kPopupId) ? Util::Anim::EnterProgress(ImGui::GetID(kPopupId), 0.0f, ThemeManager::Constants::ANIM_POPUP_SECONDS) : 1.0f;
		const float grow = ThemeManager::Constants::ANIM_POPUP_SCALE_FROM + (1.0f - ThemeManager::Constants::ANIM_POPUP_SCALE_FROM) * enter;
		ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(viewport->Size.x * ThemeManager::Constants::PALETTE_WIDTH_RATIO * grow,
									 viewport->Size.y * ThemeManager::Constants::PALETTE_HEIGHT_RATIO * grow),
			ImGuiCond_Always);
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * enter);
		ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32_BLACK_TRANS);

		constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
		                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar;
		g_open = ImGui::BeginPopupModal(kPopupId, nullptr, flags);
		ImGui::PopStyleColor();
		if (!g_open) {
			ImGui::PopStyleVar();
			return;
		}
		{
			// Glass panel behind the content, with a soft shadow on the dimmed backdrop
			Util::Glass::Style glass;
			glass.shadow = true;
			glass.rounding = ImGui::GetStyle().WindowRounding;
			Util::Glass::Draw(ImGui::GetWindowDrawList(), ImGui::GetWindowPos(),
				ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y), glass);
		}

		const float scale = Util::GetUIScale();

		// Escape closes the palette, not the menu
		bool close = false;
		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
			globals::menu->ConsumeEscape();
			close = true;
		}

		// The input keeps the keyboard unless a result is being pressed, so typing and the arrows always reach it
		if (!ImGui::IsAnyItemActive())
			ImGui::SetKeyboardFocusHere();
		ImGui::SetNextItemWidth(-FLT_MIN);
		const bool entered = ImGui::InputTextWithHint("##GoToInput", T("menu.palette.hint", "Search pages, features and settings..."),
			g_state.query.data(), g_state.query.size(), ImGuiInputTextFlags_EnterReturnsTrue);

		RunQuery();

		const int count = static_cast<int>(g_state.results.size());
		bool moved = false;
		if (count > 0) {
			if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
				g_state.selected = (g_state.selected + 1) % count;
				moved = true;
			} else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
				g_state.selected = (g_state.selected + count - 1) % count;
				moved = true;
			}
		}
		const GoToIndex::Entry* chosen = nullptr;
		if (entered && count > 0)
			chosen = g_state.results[static_cast<size_t>(g_state.selected)];

		ImGui::Spacing();
		const float footerHeight = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
		if (ImGui::BeginChild("##GoToResults", ImVec2(0.0f, -footerHeight), ImGuiChildFlags_None)) {
			if (count == 0) {
				ImGui::TextDisabled("%s", T("menu.palette.no_results", "No matches"));
			} else {
				if (g_state.lastQuery.empty())
					ImGui::TextDisabled("%s", T("menu.palette.recent_and_pages", "Recent and pages"));

				const float badgeWidth = MeasureBadgeWidth(scale);
				const float rowHeight = ImGui::GetTextLineHeight() * 2.0f + ImGui::GetStyle().ItemInnerSpacing.y +
				                        ThemeManager::Constants::PALETTE_ROW_PAD_Y * scale * 2.0f;
				const bool mouseMoved = ImGui::GetIO().MouseDelta.x != 0.0f || ImGui::GetIO().MouseDelta.y != 0.0f;

				// Rows are click targets only; the arrows move the selection, so nav must not wander onto them
				ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
				ImGuiListClipper clipper;
				clipper.Begin(count, rowHeight + ImGui::GetStyle().ItemSpacing.y);
				if (moved)
					clipper.IncludeItemByIndex(g_state.selected);
				while (clipper.Step()) {
					for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
						const auto& entry = *g_state.results[static_cast<size_t>(i)];
						ImGui::PushID(entry.id.c_str());
						if (ImGui::Selectable("##row", i == g_state.selected, ImGuiSelectableFlags_NoAutoClosePopups, ImVec2(0.0f, rowHeight)))
							chosen = &entry;
						if (mouseMoved && ImGui::IsItemHovered())
							g_state.selected = i;
						if (moved && i == g_state.selected)
							ImGui::SetScrollHereY();
						DrawRow(entry, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), badgeWidth, scale);
						ImGui::PopID();
					}
				}
				ImGui::PopItemFlag();
			}
		}
		ImGui::EndChild();

		ImGui::TextDisabled("%s", T("menu.palette.footer", "Up/Down to move, Enter to open, Esc to close"));

		if (chosen || close) {
			ImGui::CloseCurrentPopup();
			if (chosen) {
				GoToIndex::RecordPick(*chosen);
				GoToIndex::Execute(*chosen, shortcuts);
			}
		}
		ImGui::EndPopup();
		ImGui::PopStyleVar();
	}
}
