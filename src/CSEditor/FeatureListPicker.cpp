#include "FeatureListPicker.h"

#include <algorithm>
#include <format>
#include <imgui_internal.h>
#include <ranges>
#include <vector>

#include "Browser/BrowserWidgets.h"
#include "Feature.h"
#include "FeatureCategories.h"
#include "Globals.h"
#include "IconsTabler.h"
#include "Menu.h"
#include "Menu/FeatureListRenderer.h"
#include "Menu/Fonts.h"
#include "Menu/Icons/helpers/IconFonts.h"
#include "Menu/ThemeManager.h"
#include "State.h"
#include "Utils/UI.h"

namespace
{
	using Marker = FeatureListPicker::Marker;
	using Options = FeatureListPicker::Options;

	bool IsPinned(const Options& options, const Feature* feature)
	{
		return std::ranges::find(options.pinned, feature) != options.pinned.end();
	}

	bool MatchesSearch(const Options& options, Feature* feature)
	{
		return !options.search || Util::FeatureMatchesSearch(feature, *options.search);
	}

	/** @brief Name tinted by boot state, with the marker dot painted just past it. */
	void DrawRow(Feature* feature, std::string& selected, const Options& options)
	{
		const auto shortName = feature->GetShortName();
		const auto name = feature->GetDisplayName();
		const auto& palette = globals::menu->GetSettings().Theme.StatusPalette;
		ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
		if (globals::state->IsFeatureDisabled(shortName))
			color = palette.Disable;
		else if (!feature->loaded)
			color = feature->installed ? palette.RestartNeeded : palette.Disable;

		const bool pinned = IsPinned(options, feature);
		const bool isSelected = shortName == selected;
		const auto& style = ImGui::GetStyle();
		auto* drawList = ImGui::GetWindowDrawList();

		if (Util::Selectable(std::format("##{}", shortName).c_str(), isSelected))
			selected = shortName;

		ImVec2 rowMin, rowMax;
		BrowserUI::InsetRowRect(rowMin, rowMax);

		const ImVec4 accent = Util::Colors::GetAccent();
		const float barWidth = ThemeManager::Constants::ANIM_SELECTED_BAR_WIDTH * Util::GetUIScale();
		const float edgeInset = std::max(style.FramePadding.x, barWidth) + style.ItemInnerSpacing.x;
		float textMaxX = rowMax.x - edgeInset;
		if (pinned) {
			const float tintAlpha = isSelected ? ThemeManager::Constants::CSE_PINNED_SELECTED_TINT_ALPHA : ThemeManager::Constants::CSE_PINNED_TINT_ALPHA;
			drawList->AddRectFilled(rowMin, rowMax, ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, tintAlpha)), style.FrameRounding);
			drawList->PushClipRect(rowMin, ImVec2(rowMin.x + barWidth, rowMax.y), true);
			drawList->AddRectFilled(rowMin, rowMax, ImGui::GetColorU32(accent), style.FrameRounding);
			drawList->PopClipRect();

			const Icons::GlyphRef star = Icons::TI(ICON_TI_PIN);
			const ImVec2 starSize = Icons::CalcGlyphSize(star);
			const float starX = rowMax.x - starSize.x - edgeInset;
			Icons::DrawCenteredGlyph(drawList, ImVec2(starX, rowMin.y), ImVec2(starSize.x, rowMax.y - rowMin.y), star,
				ImGui::GetColorU32(accent));
			textMaxX = starX - style.ItemSpacing.x;
		}

		const float lineHeight = ImGui::GetTextLineHeight();
		const ImVec2 textPos(rowMin.x + edgeInset, rowMin.y + (rowMax.y - rowMin.y - lineHeight) * 0.5f);
		const ImVec2 nameSize = ImGui::CalcTextSize(name.c_str());
		ImGui::PushStyleColor(ImGuiCol_Text, color);
		ImGui::RenderTextEllipsis(drawList, textPos, ImVec2(textMaxX, textPos.y + nameSize.y), textMaxX, name.c_str(), nullptr, &nameSize);
		ImGui::PopStyleColor();

		const Marker marker = options.marker ? options.marker(*feature) : Marker::None;
		if (marker == Marker::None)
			return;

		const float radius = lineHeight * ThemeManager::Constants::SCENE_INDICATOR_RADIUS_RATIO;
		const ImVec2 center(std::min(textPos.x + nameSize.x, textMaxX) + style.ItemSpacing.x + radius, (rowMin.y + rowMax.y) * 0.5f);
		Util::DrawIconCircle(center, radius, ImGui::GetColorU32(Util::Colors::GetInfo()), marker == Marker::Filled);

		if (options.markerTooltip && ImGui::IsItemHovered() &&
			ImGui::IsMouseHoveringRect(ImVec2(center.x - lineHeight * 0.5f, center.y - lineHeight * 0.5f),
				ImVec2(center.x + lineHeight * 0.5f, center.y + lineHeight * 0.5f))) {
			if (const char* tooltip = options.markerTooltip(*feature, marker))
				ImGui::SetTooltip("%s", tooltip);
		}
	}
}

void FeatureListPicker::Draw(std::span<Feature* const> features, std::string& selected, const Options& options)
{
	if (options.search)
		Util::DrawFeatureSearchBar(*options.search);

	for (auto* feature : options.pinned) {
		if (feature && MatchesSearch(options, feature))
			DrawRow(feature, selected, options);
	}

	const auto listed = [&](Feature* feature) {
		return feature && !IsPinned(options, feature) && MatchesSearch(options, feature);
	};

	if (!options.categories) {
		for (auto* feature : features) {
			if (listed(feature))
				DrawRow(feature, selected, options);
		}
		return;
	}

	for (const auto category : FeatureCategories::kMenuOrder) {
		auto grouped = features | std::views::filter([&](Feature* feature) {
			return listed(feature) && feature->GetCategory() == category;
		}) | std::ranges::to<std::vector>();
		if (grouped.empty())
			continue;
		std::ranges::sort(grouped, {}, &Feature::GetDisplayName);

		const std::string categoryKey(category);
		auto& expanded = options.categories->try_emplace(categoryKey, true).first->second;
		{
			MenuFonts::FontRoleGuard fontGuard(Menu::FontRole::Heading);
			Util::DrawCategoryHeader(categoryKey.c_str(), FeatureListRenderer::TranslateCategory(category).c_str(), expanded,
				static_cast<int>(grouped.size()));
		}
		if (!expanded)
			continue;

		MenuFonts::FontRoleGuard fontGuard(Menu::FontRole::Subheading);
		for (auto* feature : grouped)
			DrawRow(feature, selected, options);
	}
}
