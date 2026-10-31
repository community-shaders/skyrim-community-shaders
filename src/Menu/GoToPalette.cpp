#include "Menu/GoToPalette.h"

#include "Globals.h"
#include "I18n/I18n.h"
#include "IconsTabler.h"
#include "Menu.h"
#include "Menu/FeatureListRenderer.h"
#include "Menu/GoToIndex.h"
#include "Menu/Icons/helpers/IconFonts.h"
#include "Menu/ThemeManager.h"
#include "Utils/UI.h"
#include "Utils/UIAnim.h"
#include "Utils/UISurface.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <imgui_internal.h>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace GoToPalette
{
	namespace
	{
		using Constants = ThemeManager::Constants;

		constexpr const char* kPopupId = "###GoToPalette";
		constexpr const char* kSortPopupId = "##GoToSortPopup";
		constexpr std::array<Scope, 5> kScopes = { Scope::All, Scope::Pages, Scope::Features, Scope::Settings, Scope::Commands };

		bool g_openRequested = false;
		bool g_open = false;
		ImGuiID g_refocusParentId = 0;  // The window to focus again on the frame after the palette closed
		Scope g_openScope = Scope::All;

		struct State
		{
			double closeStartedAt = -1.0;  // Set while the panel fades out; it closes once the fade ends
			int openedFrame = 0;           // Frame the palette opened on, so the click that opened it does not close it
			std::array<char, 256> query{};
			std::string lastQuery;
			GoToIndex::Results results;
			Scope scope = Scope::All;
			Scope lastScope = Scope::All;
			std::uint32_t lastFilter = 0;
			Menu::FeatureSortMode lastSort = Menu::FeatureSortMode::Category;
			int selected = 0;
			bool refresh = true;
			bool resetScroll = false;
		};
		State g_state;

		std::optional<GoToIndex::Kind> ScopeKind(Scope scope)
		{
			switch (scope) {
			case Scope::Pages:
				return GoToIndex::Kind::Page;
			case Scope::Features:
				return GoToIndex::Kind::Feature;
			case Scope::Settings:
				return GoToIndex::Kind::Setting;
			case Scope::Commands:
				return GoToIndex::Kind::Command;
			case Scope::All:
			default:
				return std::nullopt;
			}
		}

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

		/** @brief Plural name of a kind: its group header on the All scope and its scope label. */
		const char* GroupLabel(GoToIndex::Kind kind)
		{
			switch (kind) {
			case GoToIndex::Kind::Page:
				return T("menu.palette.group_pages", "Pages");
			case GoToIndex::Kind::Feature:
				return T("menu.palette.group_features", "Features");
			case GoToIndex::Kind::Setting:
				return T("menu.palette.group_settings", "Settings");
			case GoToIndex::Kind::Command:
			default:
				return T("menu.palette.group_commands", "Commands");
			}
		}

		const char* ScopeLabel(Scope scope)
		{
			const auto kind = ScopeKind(scope);
			return kind ? GroupLabel(*kind) : T("menu.palette.scope_all", "All");
		}

		/** @brief Theme colour of a kind's badge: four different theme roles, so no two kinds share a colour. */
		ImVec4 KindColor(GoToIndex::Kind kind)
		{
			switch (kind) {
			case GoToIndex::Kind::Page:
				return Util::Colors::GetAccent();
			case GoToIndex::Kind::Feature:
				return Util::Colors::GetSuccess();
			case GoToIndex::Kind::Setting:
				return Util::Colors::GetSecondary();
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
			return widest + Constants::PALETTE_BADGE_PAD_X * scale * 2.0f;
		}

		void DrawBadge(ImDrawList* drawList, ImVec2 min, ImVec2 size, GoToIndex::Kind kind, bool dimmed)
		{
			ImVec4 tint = KindColor(kind);
			if (dimmed)
				tint = Util::Anim::ScaleAlpha(tint, Constants::PALETTE_DIMMED_ALPHA);
			drawList->AddRectFilled(min, ImVec2(min.x + size.x, min.y + size.y),
				ImGui::GetColorU32(Util::Anim::ScaleAlpha(tint, Constants::PALETTE_BADGE_FILL_ALPHA)),
				Constants::PALETTE_BADGE_ROUNDING * Util::GetUIScale());
			const char* label = KindLabel(kind);
			const ImVec2 textSize = ImGui::CalcTextSize(label);
			drawList->AddText(ImVec2(min.x + (size.x - textSize.x) * 0.5f, min.y + (size.y - textSize.y) * 0.5f), ImGui::GetColorU32(tint), label);
		}

		float KeycapHeight()
		{
			return ImGui::GetTextLineHeight() + ImGui::GetStyle().FramePadding.y;
		}

		float KeycapWidth(const char* text, float scale)
		{
			return ImGui::CalcTextSize(text).x + Constants::PALETTE_KEYCAP_PAD_X * scale * 2.0f;
		}

		/** @brief A small rounded key chip with an outline, for chords and the footer hints. */
		void DrawKeycap(ImDrawList* drawList, ImVec2 min, const char* text, float scale)
		{
			const ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
			const ImVec2 max(min.x + KeycapWidth(text, scale), min.y + KeycapHeight());
			const float rounding = Constants::PALETTE_KEYCAP_ROUNDING * scale;
			drawList->AddRectFilled(min, max, ImGui::GetColorU32(Util::Anim::ScaleAlpha(textColor, Constants::PALETTE_KEYCAP_FILL_ALPHA)), rounding);
			drawList->AddRect(min, max, ImGui::GetColorU32(Util::Anim::ScaleAlpha(textColor, Constants::PALETTE_KEYCAP_EDGE_ALPHA)), rounding);
			const ImVec2 textSize = ImGui::CalcTextSize(text);
			drawList->AddText(ImVec2(min.x + (max.x - min.x - textSize.x) * 0.5f, min.y + (max.y - min.y - textSize.y) * 0.5f),
				ImGui::GetColorU32(textColor), text);
		}

		/** @brief The keys of a command's first chord ("Ctrl+Shift+S" gives Ctrl, Shift, S). */
		std::vector<std::string> ChordKeys(const std::string& chord)
		{
			std::vector<std::string> keys;
			const std::string first = chord.substr(0, chord.find(" / "));
			for (size_t start = 0; start < first.size();) {
				const auto end = first.find('+', start);
				const auto key = first.substr(start, end == std::string::npos ? std::string::npos : end - start);
				if (!key.empty())
					keys.push_back(key);
				if (end == std::string::npos)
					break;
				start = end + 1;
			}
			return keys;
		}

		/** @brief Draws text with the bytes in `matched` (ascending offsets) in the highlight colour. */
		void DrawHighlighted(ImDrawList* drawList, ImVec2 pos, const std::string& text, const std::vector<size_t>& matched, ImU32 base, ImU32 highlight)
		{
			const auto isMatched = [&matched](size_t i) { return std::ranges::binary_search(matched, i); };
			for (size_t i = 0; i < text.size();) {
				const bool on = isMatched(i);
				size_t j = i + 1;
				while (j < text.size() && isMatched(j) == on)
					++j;
				drawList->AddText(pos, on ? highlight : base, text.c_str() + i, text.c_str() + j);
				pos.x += ImGui::CalcTextSize(text.c_str() + i, text.c_str() + j).x;
				i = j;
			}
		}

		/** @brief Re-ranks the results when the query, scope or the feature filters changed since the last frame. */
		void RunQuery()
		{
			const std::string query(g_state.query.data());
			const auto& menuSettings = globals::menu->GetSettings();
			const std::uint32_t filter = menuSettings.FeatureListFilter & FeatureListFilter::kKnown;
			if (!g_state.refresh && query == g_state.lastQuery && g_state.scope == g_state.lastScope && filter == g_state.lastFilter &&
				menuSettings.FeatureListSort == g_state.lastSort)
				return;
			g_state.refresh = false;
			g_state.lastQuery = query;
			g_state.lastScope = g_state.scope;
			g_state.lastFilter = filter;
			g_state.lastSort = menuSettings.FeatureListSort;
			g_state.results = GoToIndex::Search(query, ScopeKind(g_state.scope), Constants::PALETTE_MAX_RESULTS);
			g_state.selected = 0;
			g_state.resetScroll = true;
		}

		/** @brief One line of the list: a group header (entry < 0) or a result. */
		struct Item
		{
			int entry = -1;
			const char* header = nullptr;
			size_t count = 0;
			float y = 0.0f;
			float height = 0.0f;
		};

		/** @brief Lays the results out top to bottom, with a header where a group starts on the All scope. */
		std::vector<Item> BuildItems(float rowHeight, float headerHeight, float& totalHeight)
		{
			std::vector<Item> items;
			const auto& results = g_state.results;
			float y = 0.0f;
			const auto push = [&](Item item, float height) {
				item.y = y;
				item.height = height;
				y += height;
				items.push_back(item);
			};
			const bool grouped = g_state.scope == Scope::All;
			const bool emptyQuery = g_state.lastQuery.empty();
			for (size_t i = 0; i < results.entries.size(); ++i) {
				const auto kind = results.entries[i]->kind;
				if (grouped && emptyQuery) {
					if (i == 0 && results.recent > 0)
						push({ .header = T("menu.palette.group_recent", "Recent"), .count = results.recent }, headerHeight);
					if (i == results.recent)
						push({ .header = GroupLabel(GoToIndex::Kind::Page), .count = results.Count(GoToIndex::Kind::Page) }, headerHeight);
				} else if (grouped && (i == 0 || results.entries[i - 1]->kind != kind)) {
					push({ .header = GroupLabel(kind), .count = results.Count(kind) }, headerHeight);
				}
				push({ .entry = static_cast<int>(i) }, rowHeight);
			}
			totalHeight = y;
			return items;
		}

		/** @brief Draws one result row into [min, max]; matched title letters take the accent colour. */
		void DrawRow(const GoToIndex::Entry& entry, ImVec2 min, ImVec2 max, float badgeWidth, float scale)
		{
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const auto& style = ImGui::GetStyle();
			const float lineHeight = ImGui::GetTextLineHeight();
			const float rowHeight = max.y - min.y;
			const float badgeHeight = lineHeight + style.FramePadding.y;

			const float left = min.x + style.FramePadding.x * 2.0f;
			DrawBadge(drawList, ImVec2(left, min.y + (rowHeight - badgeHeight) * 0.5f), ImVec2(badgeWidth, badgeHeight), entry.kind, entry.dimmed);

			const float textX = left + badgeWidth + style.ItemSpacing.x;
			float rightEdge = max.x - style.FramePadding.x * 2.0f;
			if (!entry.chord.empty()) {
				const auto keys = ChordKeys(entry.chord);
				const float keyY = min.y + (rowHeight - KeycapHeight()) * 0.5f;
				for (auto key = keys.rbegin(); key != keys.rend(); ++key) {
					rightEdge -= KeycapWidth(key->c_str(), scale);
					DrawKeycap(drawList, ImVec2(rightEdge, keyY), key->c_str(), scale);
					rightEdge -= style.ItemInnerSpacing.x;
				}
				rightEdge -= style.ItemSpacing.x;
			}

			ImVec4 textColor = style.Colors[ImGuiCol_Text];
			ImVec4 matchColor = Util::Colors::GetAccent();
			if (entry.dimmed) {
				textColor = Util::Anim::ScaleAlpha(textColor, Constants::PALETTE_DIMMED_ALPHA);
				matchColor = Util::Anim::ScaleAlpha(matchColor, Constants::PALETTE_DIMMED_ALPHA);
			}

			const bool twoLines = !entry.breadcrumb.empty();
			const float blockHeight = twoLines ? lineHeight * 2.0f + style.ItemInnerSpacing.y : lineHeight;
			const float titleY = min.y + (rowHeight - blockHeight) * 0.5f;
			const std::vector<size_t> matched = g_state.lastQuery.empty() ? std::vector<size_t>() : GoToIndex::MatchPositions(g_state.lastQuery, entry.title);

			drawList->PushClipRect(ImVec2(textX, min.y), ImVec2(rightEdge, max.y), true);
			DrawHighlighted(drawList, ImVec2(textX, titleY), entry.title, matched, ImGui::GetColorU32(textColor), ImGui::GetColorU32(matchColor));
			if (twoLines) {
				drawList->AddText(ImVec2(textX, titleY + lineHeight + style.ItemInnerSpacing.y), ImGui::GetColorU32(ImGuiCol_TextDisabled),
					entry.breadcrumb.c_str());
			}
			drawList->PopClipRect();
		}

		/** @brief Group header: a small label, a hairline and the group's match count. */
		void DrawHeader(const Item& item, ImVec2 min, ImVec2 max)
		{
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const auto& style = ImGui::GetStyle();
			const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
			const float textY = Util::CenterTextTopY(min.y, max.y - min.y);
			const float textX = min.x + style.FramePadding.x * 2.0f;
			drawList->AddText(ImVec2(textX, textY), dim, item.header);
			const std::string count = std::to_string(item.count);
			const float countWidth = ImGui::CalcTextSize(count.c_str()).x;
			const float rightEdge = max.x - style.FramePadding.x * 2.0f;
			drawList->AddText(ImVec2(rightEdge - countWidth, textY), dim, count.c_str());
			const float lineStart = textX + ImGui::CalcTextSize(item.header).x + style.ItemSpacing.x;
			const float lineEnd = rightEdge - countWidth - style.ItemSpacing.x;
			if (lineEnd > lineStart) {
				const float y = std::floor((min.y + max.y) * 0.5f);
				drawList->AddLine(ImVec2(lineStart, y), ImVec2(lineEnd, y), ImGui::GetColorU32(ImGuiCol_Separator));
			}
		}

		/** @brief Icon disc, title and hint, centred in the list when nothing matches. */
		void DrawEmptyState(ImVec2 origin, ImVec2 avail, float scale)
		{
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const float lineHeight = ImGui::GetTextLineHeight();
			const bool prompt = g_state.lastQuery.empty() && g_state.scope == Scope::Settings;
			const bool filtered = (globals::menu->GetSettings().FeatureListFilter & FeatureListFilter::kKnown) != 0 &&
			                      (g_state.scope == Scope::Features || g_state.scope == Scope::All);
			const char* title = prompt ? T("menu.palette.settings_prompt", "Search settings") : T("menu.palette.no_results", "No matches");
			const char* hint = prompt   ? T("menu.palette.settings_prompt_hint", "Type a setting's name to find it.") :
			                   filtered ? T("menu.palette.no_results_filtered", "A feature filter is on. Open the menu button in the field to clear it.") :
			                              T("menu.palette.no_results_hint", "Try another word, or another scope.");

			const float radius = lineHeight * Constants::PALETTE_EMPTY_ICON_RATIO;
			const float centerX = origin.x + avail.x * 0.5f;
			const float discY = origin.y + avail.y * Constants::PALETTE_EMPTY_DISC_Y;
			const ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
			drawList->AddCircleFilled(ImVec2(centerX, discY), radius, ImGui::GetColorU32(Util::Anim::ScaleAlpha(textColor, Constants::PALETTE_EMPTY_DISC_ALPHA)));
			Icons::DrawCenteredGlyph(drawList, ImVec2(centerX - radius, discY - radius), ImVec2(radius * 2.0f, radius * 2.0f), Icons::TI(ICON_TI_SEARCH),
				ImGui::GetColorU32(ImGuiCol_TextDisabled));
			const float titleY = discY + radius + Constants::PALETTE_GAP * scale;
			drawList->AddText(ImVec2(centerX - ImGui::CalcTextSize(title).x * 0.5f, titleY), ImGui::GetColorU32(textColor), title);
			drawList->AddText(ImVec2(centerX - ImGui::CalcTextSize(hint).x * 0.5f, titleY + lineHeight + ImGui::GetStyle().ItemInnerSpacing.y),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), hint);
		}

		/**
		 * @brief The search field: a rounded frame with the search glyph, the text input and, at the right inside the
		 * frame, the sort and filter button. The input is narrower than the frame so text never runs under the button.
		 * @return True when Enter was pressed in the input.
		 */
		bool DrawSearchField(bool sortOpen, float scale)
		{
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const auto& style = ImGui::GetStyle();
			const float lineHeight = ImGui::GetTextLineHeight();
			const float fieldHeight = ImGui::GetFrameHeight() * Constants::PALETTE_FIELD_HEIGHT_RATIO;
			const ImVec2 fieldMin = ImGui::GetCursorScreenPos();
			const float fieldWidth = ImGui::GetContentRegionAvail().x;
			const ImVec2 fieldMax(fieldMin.x + fieldWidth, fieldMin.y + fieldHeight);
			const float rounding = Constants::PALETTE_FIELD_ROUNDING * scale;
			const float padX = Constants::PALETTE_FIELD_PAD_X * scale;
			const float inset = Constants::PALETTE_FIELD_INSET * scale;

			drawList->AddRectFilled(fieldMin, fieldMax, ImGui::GetColorU32(ImGuiCol_FrameBg), rounding);
			drawList->AddRect(fieldMin, fieldMax, ImGui::GetColorU32(Util::Anim::ScaleAlpha(Util::Colors::GetAccent(), Constants::PALETTE_FIELD_BORDER_ALPHA)),
				rounding, ImDrawFlags_None, scale);

			const float glyphWidth = lineHeight * Constants::PALETTE_SEARCH_GLYPH_RATIO;
			Icons::DrawCenteredGlyph(drawList, ImVec2(fieldMin.x + padX, fieldMin.y), ImVec2(glyphWidth, fieldHeight), Icons::TI(ICON_TI_SEARCH),
				ImGui::GetColorU32(ImGuiCol_TextDisabled));

			const float buttonSize = fieldHeight - inset * 2.0f;
			const ImVec2 buttonMin(fieldMax.x - inset - buttonSize, fieldMin.y + inset);
			const float inputX = fieldMin.x + padX + glyphWidth;

			// The input keeps the keyboard unless a result or the sort popup is being used
			ImGui::SetCursorScreenPos(ImVec2(inputX, fieldMin.y));
			if (!ImGui::IsAnyItemActive() && !sortOpen)
				ImGui::SetKeyboardFocusHere();
			ImGui::SetNextItemWidth(buttonMin.x - inset - inputX);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, (fieldHeight - lineHeight) * 0.5f));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
			ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32_BLACK_TRANS);
			ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32_BLACK_TRANS);
			ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32_BLACK_TRANS);
			const bool entered = ImGui::InputTextWithHint("##GoToInput", T("menu.palette.hint", "Search pages, features and settings..."),
				g_state.query.data(), g_state.query.size(), ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::PopStyleColor(3);
			ImGui::PopStyleVar(2);

			// Sort and filter button, inside the frame
			ImGui::SetCursorScreenPos(buttonMin);
			const bool pressed = ImGui::InvisibleButton("##GoToSort", ImVec2(buttonSize, buttonSize));
			const float hover = Util::Anim::Toggle(ImGui::GetItemID(), ImGui::IsItemHovered() || sortOpen);
			const ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
			const ImVec2 buttonMax(buttonMin.x + buttonSize, buttonMin.y + buttonSize);
			drawList->AddRectFilled(buttonMin, buttonMax,
				ImGui::GetColorU32(Util::Anim::ScaleAlpha(textColor, Constants::PALETTE_HOVER_FILL_ALPHA * (1.0f + hover))), std::max(0.0f, rounding - inset));
			Icons::DrawCenteredGlyph(drawList, buttonMin, ImVec2(buttonSize, buttonSize), Icons::TI(ICON_TI_MENU_2),
				ImGui::GetColorU32(Util::Anim::Lerp(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), textColor, hover)));
			if ((globals::menu->GetSettings().FeatureListFilter & FeatureListFilter::kKnown) != 0) {
				const float radius = ImGui::GetFrameHeight() * Constants::SORT_FILTER_DOT_RATIO;
				Util::DrawIconCircle(ImVec2(buttonMax.x - radius * 1.5f, buttonMin.y + radius * 1.5f), radius, ImGui::GetColorU32(Util::Colors::GetAccent()), true);
			}
			Util::HoverTip(T("menu.palette.sort_tooltip", "Sort and filter features"));
			if (pressed)
				ImGui::OpenPopup(kSortPopupId);
			ImGui::SetNextWindowPos(ImVec2(buttonMax.x, buttonMax.y + style.ItemSpacing.y), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
			FeatureListRenderer::RenderSortPopup(kSortPopupId);

			ImGui::SetCursorScreenPos(ImVec2(fieldMin.x, fieldMax.y + Constants::PALETTE_GAP * scale));
			ImGui::Dummy(ImVec2(fieldWidth, 0.0f));
			return entered;
		}

		/** @brief The scope row: a segmented control whose accent pill glides to the chosen scope; each scope shows its count. */
		void DrawScopeRow(float scale)
		{
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const auto& style = ImGui::GetStyle();
			const float lineHeight = ImGui::GetTextLineHeight();
			const float trackPad = Constants::PALETTE_SCOPE_PAD * scale;
			const float itemPadX = Constants::PALETTE_SCOPE_ITEM_PAD_X * scale;
			const float countPadX = Constants::PALETTE_SCOPE_COUNT_PAD_X * scale;
			const float itemHeight = lineHeight + style.FramePadding.y * 2.0f;
			const float trackRounding = Constants::PALETTE_SCOPE_ROUNDING * scale;
			const ImVec2 trackMin = ImGui::GetCursorScreenPos();

			struct Slot
			{
				Scope scope;
				const char* label;
				std::string count;
				float x;
				float width;
			};
			std::array<Slot, kScopes.size()> slots{};
			float x = trackPad;
			for (size_t i = 0; i < kScopes.size(); ++i) {
				const auto kind = ScopeKind(kScopes[i]);
				const size_t count = kind ? g_state.results.Count(*kind) : g_state.results.Total();
				auto& slot = slots[i];
				slot = { kScopes[i], ScopeLabel(kScopes[i]), std::to_string(count), x, 0.0f };
				slot.width = itemPadX * 2.0f + ImGui::CalcTextSize(slot.label).x + style.ItemInnerSpacing.x +
				             ImGui::CalcTextSize(slot.count.c_str()).x + countPadX * 2.0f;
				x += slot.width;
			}
			const float trackWidth = x + trackPad;
			const float trackHeight = itemHeight + trackPad * 2.0f;
			drawList->AddRectFilled(trackMin, ImVec2(trackMin.x + trackWidth, trackMin.y + trackHeight), ImGui::GetColorU32(ImGuiCol_FrameBg), trackRounding);

			// The pill eases to the chosen scope, in width too, as the labels differ
			const auto& active = *std::ranges::find(slots, g_state.scope, &Slot::scope);
			const ImVec4 accent = Util::Colors::GetAccent();
			const auto pill = Util::Anim::SelectionGlide(ImGui::GetID("##scopePill"), ImVec2(trackMin.x + active.x, trackMin.y + trackPad),
				ImVec2(trackMin.x + active.x + active.width, trackMin.y + trackPad + itemHeight), Constants::PALETTE_PILL_RATE);
			const ImVec2 pillMin = pill.min;
			const ImVec2 pillMax = pill.max;
			const float pillRounding = std::max(0.0f, trackRounding - trackPad);
			drawList->AddRectFilled(pillMin, pillMax, ImGui::GetColorU32(Util::Anim::ScaleAlpha(accent, Constants::SEGMENT_SELECTED_FILL_ALPHA)), pillRounding);
			drawList->AddRect(pillMin, pillMax, ImGui::GetColorU32(Util::Anim::ScaleAlpha(accent, Constants::PALETTE_PILL_BORDER_ALPHA)), pillRounding,
				ImDrawFlags_None, scale);

			const ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
			const ImVec4 dimColor = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
			for (const auto& slot : slots) {
				const ImVec2 itemMin(trackMin.x + slot.x, trackMin.y + trackPad);
				ImGui::SetCursorScreenPos(itemMin);
				ImGui::PushID(static_cast<int>(slot.scope));
				if (ImGui::InvisibleButton("##scope", ImVec2(slot.width, itemHeight)))
					g_state.scope = slot.scope;
				const float chosen = Util::Anim::Toggle(ImGui::GetItemID(), slot.scope == g_state.scope);
				const float hover = Util::Anim::Toggle(ImGui::GetID("hover"), ImGui::IsItemHovered());
				ImGui::PopID();

				const ImVec4 labelColor = Util::Anim::Lerp(dimColor, textColor, std::max(chosen, hover * Constants::PALETTE_SCOPE_HOVER_LABEL));
				const float textY = Util::CenterTextTopY(itemMin.y, itemHeight);
				drawList->AddText(ImVec2(itemMin.x + itemPadX, textY), ImGui::GetColorU32(labelColor), slot.label);

				const float countWidth = ImGui::CalcTextSize(slot.count.c_str()).x + countPadX * 2.0f;
				const ImVec2 chipMin(itemMin.x + slot.width - itemPadX - countWidth, itemMin.y + (itemHeight - lineHeight) * 0.5f);
				const ImVec4 chipFill = Util::Anim::Lerp(Util::Anim::ScaleAlpha(textColor, Constants::PALETTE_COUNT_FILL_ALPHA),
					Util::Anim::ScaleAlpha(accent, Constants::SEGMENT_SELECTED_FILL_ALPHA * 2.0f), chosen);
				drawList->AddRectFilled(chipMin, ImVec2(chipMin.x + countWidth, chipMin.y + lineHeight), ImGui::GetColorU32(chipFill),
					Constants::PALETTE_BADGE_ROUNDING * scale);
				drawList->AddText(ImVec2(chipMin.x + countPadX, chipMin.y), ImGui::GetColorU32(labelColor), slot.count.c_str());
			}

			ImGui::SetCursorScreenPos(ImVec2(trackMin.x, trackMin.y + trackHeight + Constants::PALETTE_GAP * scale));
			ImGui::Dummy(ImVec2(trackWidth, 0.0f));
		}

		/** @brief Footer: key hints as keycaps on the left, the number of results on the right. */
		void DrawFooter(float scale)
		{
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const auto& style = ImGui::GetStyle();
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			const float width = ImGui::GetContentRegionAvail().x;
			const float gap = Constants::PALETTE_GAP * scale;
			const float lineY = pos.y + gap * 0.5f;
			drawList->AddLine(ImVec2(pos.x, lineY), ImVec2(pos.x + width, lineY), ImGui::GetColorU32(ImGuiCol_Separator));

			struct Hint
			{
				const char* key;
				const char* action;
			};
			const std::array<Hint, 4> hints = { {
				{ T("menu.palette.key_tab", "Tab"), T("menu.palette.hint_scope", "scope") },
				{ T("menu.palette.key_updown", "Up/Down"), T("menu.palette.hint_move", "move") },
				{ T("menu.palette.key_enter", "Enter"), T("menu.palette.hint_open", "open") },
				{ T("menu.palette.key_esc", "Esc"), T("menu.palette.hint_close", "close") },
			} };
			// Centred between the line and the popup's bottom edge, which lies past the window's bottom padding
			const float panelBottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
			const float keyY = lineY + (panelBottom - lineY - KeycapHeight()) * 0.5f;
			const float textY = Util::CenterTextTopY(keyY, KeycapHeight());
			float x = pos.x;
			for (const auto& hint : hints) {
				DrawKeycap(drawList, ImVec2(x, keyY), hint.key, scale);
				x += KeycapWidth(hint.key, scale) + style.ItemInnerSpacing.x;
				drawList->AddText(ImVec2(x, textY), ImGui::GetColorU32(ImGuiCol_TextDisabled), hint.action);
				x += ImGui::CalcTextSize(hint.action).x + style.ItemSpacing.x * 2.0f;
			}

			const std::string count = I18n::GetSingleton()->Format("menu.palette.result_count",
				{ { "count", std::to_string(g_state.results.entries.size()) } }, "{count} results");
			drawList->AddText(ImVec2(pos.x + width - ImGui::CalcTextSize(count.c_str()).x, textY), ImGui::GetColorU32(ImGuiCol_TextDisabled), count.c_str());
		}

		/** @brief The scrolling result list with its gliding selection bar; sets `chosen` when a row is clicked. */
		void DrawResults(bool moved, float scale, const GoToIndex::Entry*& chosen)
		{
			const auto& style = ImGui::GetStyle();
			const int count = static_cast<int>(g_state.results.entries.size());
			if (std::exchange(g_state.resetScroll, false))
				ImGui::SetScrollY(0.0f);
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			const float width = ImGui::GetContentRegionAvail().x;
			if (count == 0) {
				DrawEmptyState(origin, ImGui::GetContentRegionAvail(), scale);
			} else {
				const float badgeWidth = MeasureBadgeWidth(scale);
				const float rowHeight = ImGui::GetTextLineHeight() * 2.0f + style.ItemInnerSpacing.y + Constants::PALETTE_ROW_PAD_Y * scale * 2.0f;
				const float headerHeight = ImGui::GetTextLineHeight() + Constants::PALETTE_HEADER_PAD_Y * scale * 2.0f;
				float totalHeight = 0.0f;
				const std::vector<Item> items = BuildItems(rowHeight, headerHeight, totalHeight);
				ImGui::Dummy(ImVec2(width, totalHeight));

				// Where the selected row sits, and the top of its group header when it leads one
				float selectedY = 0.0f;
				float selectedTop = 0.0f;
				for (size_t n = 0; n < items.size(); ++n) {
					if (items[n].entry != g_state.selected)
						continue;
					selectedY = items[n].y;
					selectedTop = n > 0 && items[n - 1].entry < 0 ? items[n - 1].y : selectedY;
				}
				if (moved) {
					const float viewTop = ImGui::GetScrollY();
					const float viewHeight = ImGui::GetWindowHeight();
					if (selectedTop < viewTop)
						ImGui::SetScrollY(selectedTop);
					else if (selectedY + rowHeight > viewTop + viewHeight)
						ImGui::SetScrollY(selectedY + rowHeight - viewHeight);
				}

				// The highlight is one shape that glides from row to row
				ImDrawList* drawList = ImGui::GetWindowDrawList();
				const ImVec4 accent = Util::Colors::GetAccent();
				const float rowRounding = Constants::PALETTE_ROW_ROUNDING * scale;
				const auto bar = Util::Anim::SelectionGlide(ImGui::GetID("##rowGlide"), ImVec2(origin.x, origin.y + selectedY),
					ImVec2(origin.x + width, origin.y + selectedY + rowHeight), Constants::ANIM_GLIDE_RATE);
				const ImVec2 barMin = bar.min;
				const ImVec2 barMax = bar.max;
				drawList->AddRectFilled(barMin, barMax, ImGui::GetColorU32(Util::Anim::ScaleAlpha(accent, Constants::NAV_SELECTED_FILL_ALPHA)), rowRounding);
				const float barWidth = Constants::ANIM_SELECTED_BAR_WIDTH * scale;
				drawList->AddRectFilled(ImVec2(barMin.x, barMin.y + rowHeight * Constants::NAV_SELECTED_BAR_INSET), ImVec2(barMin.x + barWidth, barMax.y - rowHeight * Constants::NAV_SELECTED_BAR_INSET),
					ImGui::GetColorU32(accent), barWidth * 0.5f);

				const bool mouseMoved = ImGui::GetIO().MouseDelta.x != 0.0f || ImGui::GetIO().MouseDelta.y != 0.0f;
				const ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
				for (const auto& item : items) {
					const ImVec2 min(origin.x, origin.y + item.y);
					const ImVec2 max(origin.x + width, min.y + item.height);
					if (!ImGui::IsRectVisible(min, max))
						continue;
					if (item.entry < 0) {
						DrawHeader(item, min, max);
						continue;
					}
					const auto& entry = *g_state.results.entries[static_cast<size_t>(item.entry)];
					ImGui::PushID(entry.id.c_str());
					ImGui::SetCursorScreenPos(min);
					if (ImGui::InvisibleButton("##row", ImVec2(width, item.height)))
						chosen = &entry;
					const bool hovered = ImGui::IsItemHovered();
					if (mouseMoved && hovered)
						g_state.selected = item.entry;
					const float hover = Util::Anim::Toggle(ImGui::GetID("hover"), hovered && item.entry != g_state.selected);
					if (hover > 0.0f)
						drawList->AddRectFilled(min, max, ImGui::GetColorU32(Util::Anim::ScaleAlpha(textColor, Constants::PALETTE_HOVER_FILL_ALPHA * hover)), rowRounding);
					DrawRow(entry, min, max, badgeWidth, scale);
					ImGui::PopID();
				}
			}
		}
	}

	void Open(Scope scope)
	{
		g_openScope = scope;
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
			g_state.scope = g_openScope;
			g_state.openedFrame = ImGui::GetFrameCount();
			ImGui::OpenPopup(kPopupId);
		}

		// A click outside the panel makes ImGui drop window focus, and closing a popup only restores focus that is
		// still on it, so the menu that opened the palette gets it back by hand once the palette is gone
		if (g_refocusParentId != 0 && !ImGui::IsPopupOpen(kPopupId)) {
			if (ImGuiWindow* parent = ImGui::FindWindowByID(g_refocusParentId))
				ImGui::FocusWindow(parent);
			g_refocusParentId = 0;
		}
		const ImGuiID parentWindowId = ImGui::GetCurrentWindow()->ID;

		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		// Fades in and grows slightly from the centre each time it opens
		const float enter = ImGui::IsPopupOpen(kPopupId) ? Util::Anim::EnterProgress(ImGui::GetID(kPopupId), 0.0f, Constants::ANIM_POPUP_SECONDS) : 1.0f;
		// Like the menu itself, it keeps drawing while it fades out and closes once the fade is done
		const bool closing = g_state.closeStartedAt >= 0.0;
		const float exitProgress = closing ? std::min(1.0f, static_cast<float>(ImGui::GetTime() - g_state.closeStartedAt) * Util::Anim::Speed() /
																Constants::ANIM_POPUP_SECONDS) :
		                                     0.0f;
		// Never exactly zero: a window drawn at zero alpha may not be submitted, and the close below has to run inside it
		const float visibility = std::max(Constants::ANIM_POPUP_MIN_VISIBILITY, enter * (1.0f - Util::Anim::EaseOutCubic(exitProgress)));
		const float grow = Constants::ANIM_POPUP_SCALE_FROM + (1.0f - Constants::ANIM_POPUP_SCALE_FROM) * visibility;
		ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(viewport->Size.x * Constants::PALETTE_WIDTH_RATIO * grow, viewport->Size.y * Constants::PALETTE_HEIGHT_RATIO * grow),
			ImGuiCond_Always);
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * visibility);
		ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32_BLACK_TRANS);

		// No nav inputs: Tab and the arrows belong to the palette, not to ImGui's focus movement
		constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
		                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNavInputs;
		g_open = ImGui::BeginPopupModal(kPopupId, nullptr, flags);
		ImGui::PopStyleColor();
		if (!g_open) {
			ImGui::PopStyleVar();
			return;
		}
		// Surface behind the content, with a soft shadow on the dimmed backdrop
		Util::Surface::Options surface;
		surface.rounding = ImGui::GetStyle().WindowRounding;
		surface.minFillAlpha = Constants::PALETTE_MIN_FILL_ALPHA;
		const ImVec2 panelMin = ImGui::GetWindowPos();
		const ImVec2 panelMax(panelMin.x + ImGui::GetWindowSize().x, panelMin.y + ImGui::GetWindowSize().y);
		Util::Surface::Draw(ImGui::GetWindowDrawList(), panelMin, panelMax, Util::Surface::Role::Palette, surface);

		const float scale = Util::GetUIScale();
		const bool sortOpen = ImGui::IsPopupOpen(kSortPopupId);

		// Escape closes the sort popup first, then the palette, never the menu
		bool close = false;
		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
			globals::menu->ConsumeEscape();
			close = !sortOpen;
		}
		// A click outside the panel closes it; while the sort popup is open that click belongs to the popup
		if (!sortOpen && ImGui::GetFrameCount() > g_state.openedFrame && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
			!ImGui::IsMouseHoveringRect(panelMin, panelMax, false))
			close = true;
		if (!sortOpen && ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
			const int step = ImGui::GetIO().KeyShift ? -1 : 1;
			const int scopeCount = static_cast<int>(kScopes.size());
			const int at = static_cast<int>(std::ranges::find(kScopes, g_state.scope) - kScopes.begin());
			g_state.scope = kScopes[static_cast<size_t>((at + step + scopeCount) % scopeCount)];
		}

		const bool entered = DrawSearchField(sortOpen, scale);
		RunQuery();
		DrawScopeRow(scale);

		const int count = static_cast<int>(g_state.results.entries.size());
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
			chosen = g_state.results.entries[static_cast<size_t>(g_state.selected)];

		const float footerHeight = KeycapHeight() + Constants::PALETTE_GAP * scale * 2.0f;
		if (ImGui::BeginChild("##GoToResults", ImVec2(0.0f, -footerHeight), ImGuiChildFlags_None))
			DrawResults(moved, scale, chosen);
		ImGui::EndChild();

		DrawFooter(scale);

		if ((chosen || close) && !closing) {
			if (Util::Anim::ReducedMotion()) {
				ImGui::CloseCurrentPopup();
				g_refocusParentId = parentWindowId;
			} else
				g_state.closeStartedAt = ImGui::GetTime();
			if (chosen) {
				GoToIndex::RecordPick(*chosen);
				GoToIndex::Execute(*chosen, shortcuts);
			}
		}
		if (closing && exitProgress >= 1.0f) {
			ImGui::CloseCurrentPopup();
			g_refocusParentId = parentWindowId;
		}
		ImGui::EndPopup();
		ImGui::PopStyleVar();
	}
}
