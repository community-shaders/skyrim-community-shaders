#include "Menu/IssuesPageRenderer.h"

#include <imgui.h>

#include <format>
#include <functional>
#include <ranges>
#include <string>
#include <vector>

#include "CSEditor/Browser/BrowserWidgets.h"
#include "FeatureConstraints.h"
#include "FeatureIssues.h"
#include "Globals.h"
#include "I18n/I18n.h"
#include "Menu.h"
#include "Menu/ThemeManager.h"
#include "Util.h"

namespace
{
	using ConstraintList = std::vector<std::pair<FeatureConstraints::SettingId, FeatureConstraints::ConstraintResult>>;

	/** @brief The active constraints, recomputed at most every ISSUES_REFRESH_SECONDS: the nav entry asks every frame. */
	const ConstraintList& GetActiveConstraints()
	{
		static ConstraintList constraints;
		static double refreshedAt = -1.0;
		const double now = ImGui::GetTime();
		if (refreshedAt < 0.0 || now - refreshedAt >= ThemeManager::Constants::ISSUES_REFRESH_SECONDS) {
			constraints = FeatureConstraints::GetAllActiveConstraints();
			refreshedAt = now;
		}
		return constraints;
	}

	/** @brief The constraints table; each "Constrained By" cell opens the first source feature. */
	void RenderConstraintsTable(const ConstraintList& constraints)
	{
		ImGui::TextWrapped("%s", T("menu.home.constraints_desc", "Some settings are constrained by other features. Hover over rows for details."));
		ImGui::Spacing();

		struct ConstraintRow
		{
			std::string setting;
			std::string forcedTo;
			std::string constrainedBy;
			std::string firstSourceShortName;  // For "navigate to feature" on click
			std::string tooltip;
		};

		std::vector<ConstraintRow> rows;
		rows.reserve(constraints.size());
		for (const auto& [settingId, result] : constraints) {
			ConstraintRow row;
			row.setting = std::format("{}.{}", settingId.featureShortName, settingId.settingPath);
			row.forcedTo = FeatureConstraints::FormatConstraintValue(result.forcedValue);
			row.constrainedBy = result.sources | std::views::transform([](const auto& source) -> const std::string& { return source.featureName; }) | std::views::join_with(std::string_view(", ")) | std::ranges::to<std::string>();
			if (!result.sources.empty())
				row.firstSourceShortName = result.sources[0].featureShortName;
			for (const auto& src : result.sources) {
				if (!row.tooltip.empty())
					row.tooltip += "\n";
				row.tooltip += std::format("{}: {}", src.featureName, src.reason);
				if (src.recommendDisableAtBoot)
					row.tooltip += std::string("\n") + T("menu.home.consider_disabling_at_boot", "Consider disabling at boot.");
			}
			rows.push_back(std::move(row));
		}

		std::vector<std::string> headers = { T("menu.home.constraint_header_setting", "Setting"),
			T("menu.home.constraint_header_forced_to", "Forced To"),
			T("menu.home.constraint_header_constrained_by", "Constrained By") };

		std::vector<std::function<bool(const ConstraintRow&, const ConstraintRow&, bool)>> customSorts = {
			[](const ConstraintRow& a, const ConstraintRow& b, bool asc) { return Util::StringSortComparator(a.setting, b.setting, asc); },
			[](const ConstraintRow& a, const ConstraintRow& b, bool asc) { return Util::StringSortComparator(a.forcedTo, b.forcedTo, asc); },
			[](const ConstraintRow& a, const ConstraintRow& b, bool asc) { return Util::StringSortComparator(a.constrainedBy, b.constrainedBy, asc); }
		};

		auto cellRender = [](int rowIdx, int colIdx, const ConstraintRow& row) {
			if (colIdx == 0) {
				Util::RenderTableCell(row.setting, "", "", nullptr, ImVec4(1, 1, 1, 1), true, Util::Colors::GetWarning());
			} else if (colIdx == 1) {
				Util::RenderTableCell(row.forcedTo, "", "", nullptr, ImVec4(1, 1, 1, 1), true);
			} else if (colIdx == 2) {
				if (!row.firstSourceShortName.empty()) {
					if (Util::Selectable(std::format("{}##nav{}", row.constrainedBy, rowIdx).c_str()))
						globals::menu->SelectFeatureMenu(row.firstSourceShortName);
					if (auto _tt = Util::HoverTooltipWrapper()) {
						ImGui::Text("%s", I18n::GetSingleton()->Format("menu.home.click_to_navigate",
																  { { "feature", row.constrainedBy } },
																  "Click to navigate to {feature}")
											  .c_str());
						if (!row.tooltip.empty()) {
							ImGui::Separator();
							ImGui::Text("%s", row.tooltip.c_str());
						}
					}
				} else {
					Util::RenderTableCell(row.constrainedBy, "", row.tooltip, nullptr, ImVec4(1, 1, 1, 1), true);
				}
			}
		};

		Util::ShowSortedStringTableCustom<ConstraintRow>(
			"ConstraintsTable",
			headers,
			rows,
			0,     // sortColumn
			true,  // ascending
			customSorts,
			cellRender);
	}
}

namespace IssuesPageRenderer
{
	size_t GetFeatureIssueCount()
	{
		return FeatureIssues::GetFeatureIssues().size();
	}

	size_t GetConstraintCount()
	{
		return GetActiveConstraints().size();
	}

	size_t GetIssueCount()
	{
		return GetFeatureIssueCount() + GetConstraintCount();
	}

	bool HasIssues()
	{
		return GetIssueCount() > 0;
	}

	void RenderConstraintsSection()
	{
		BrowserUI::SectionLabel(T("menu.home.active_constraints", "Active Setting Constraints"));
		RenderConstraintsTable(GetActiveConstraints());
	}
}
