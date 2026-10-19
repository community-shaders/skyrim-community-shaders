#include "FeatureListPicker.h"

#include <algorithm>
#include <format>
#include <ranges>
#include <vector>

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

	/// A pinned row's accent: the tint behind it, as a share of the accent's opacity, and its edge bar.
	constexpr float kPinnedTintAlpha = 0.14f;
	constexpr float kPinnedSelectedTintAlpha = 0.26f;
	constexpr float kPinnedBarWidth = 3.0f;

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
		auto* drawList = ImGui::GetWindowDrawList();
		// Pinned rows are the ones most edits start from, so they carry an accent behind the label.
		if (pinned) {
			drawList->ChannelsSplit(2);
			drawList->ChannelsSetCurrent(1);
		}

		ImGui::PushStyleColor(ImGuiCol_Text, color);
		if (ImGui::Selectable(std::format("{}##{}", name, shortName).c_str(), shortName == selected))
			selected = shortName;
		ImGui::PopStyleColor();

		if (pinned) {
			const ImVec2 rowMin = ImGui::GetItemRectMin();
			const ImVec2 rowMax = ImGui::GetItemRectMax();
			const ImVec4 accent = Util::Colors::GetAccent();
			const float scale = Util::GetUIScale();

			drawList->ChannelsSetCurrent(0);
			drawList->AddRectFilled(rowMin, rowMax,
				ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z,
					shortName == selected ? kPinnedSelectedTintAlpha : kPinnedTintAlpha)),
				ImGui::GetStyle().FrameRounding);
			drawList->AddRectFilled(rowMin, ImVec2(rowMin.x + kPinnedBarWidth * scale, rowMax.y),
				ImGui::GetColorU32(accent), ImGui::GetStyle().FrameRounding);
			drawList->ChannelsSetCurrent(1);

			// Right-aligned so the name and its dot keep their place.
			const Icons::GlyphRef star = Icons::TI(ICON_TI_PIN);
			const ImVec2 starSize = Icons::CalcGlyphSize(star);
			Icons::DrawCenteredGlyph(drawList,
				ImVec2(rowMax.x - starSize.x - ImGui::GetStyle().FramePadding.x, rowMin.y),
				ImVec2(starSize.x, rowMax.y - rowMin.y), star, ImGui::GetColorU32(accent));
			drawList->ChannelsMerge();
		}

		const Marker marker = options.marker ? options.marker(*feature) : Marker::None;
		if (marker == Marker::None)
			return;

		// Painted rather than laid out: the selectable spans the row, so a following item would sit past its edge.
		const ImVec2 rowMin = ImGui::GetItemRectMin();
		const float lineHeight = ImGui::GetTextLineHeight();
		const float radius = lineHeight * ThemeManager::Constants::SCENE_INDICATOR_RADIUS_RATIO;
		const ImVec2 center(rowMin.x + ImGui::CalcTextSize(name.c_str()).x + ImGui::GetStyle().ItemInnerSpacing.x + radius,
			(rowMin.y + ImGui::GetItemRectMax().y) * 0.5f);
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
