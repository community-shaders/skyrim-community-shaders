#include "TimeOfDayPanel.h"

#include <array>
#include <cmath>
#include <map>

#include "../Globals.h"
#include "../Menu.h"
#include "../Menu/ThemeManager.h"
#include "../SceneSettingsManager.h"
#include "SceneSettingsUI.h"

namespace TimeOfDayPanel
{
	using SceneType = SceneSettingsManager::SceneType;
	using EntrySource = SceneSettingsManager::EntrySource;
	using Period = SceneSettingsManager::TimeOfDayPeriod;
	using C = ThemeManager::Constants;
	static constexpr auto kSceneType = SceneType::TimeOfDay;
	static constexpr int kPeriodCount = SceneSettingsManager::kPeriodCount;

	// Per-period add-setting state
	static SceneSettingsUI::AddSettingState periodAddState[kPeriodCount];

	// Shared popups
	static SceneSettingsUI::PopupState popups{
		"Are you sure you want to delete all time-of-day overwrite files?\nThis cannot be undone.",
		"Are you sure you want to remove all user-added time-of-day settings?"
	};

	/// Collect all unique feature+setting pairs across all periods, preserving order.
	struct SettingId
	{
		std::string feature;
		std::string key;
		bool operator<(const SettingId& o) const
		{
			return (feature < o.feature) || (feature == o.feature && key < o.key);
		}
	};

	/// Draw a single value cell for a given entry index (or empty if no entry for this period).
	static void DrawValueCell(size_t entryIndex)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto& entries = manager->GetEntries(kSceneType);

		if (entryIndex == SIZE_MAX) {
			ImGui::TextDisabled("--");
			return;
		}

		const auto& entry = entries[entryIndex];
		bool isOverwrite = entry.source == EntrySource::Overwrite;
		auto settingType = SceneSettingsManager::DetectSettingType(entry.value);

		ImGui::PushID(static_cast<int>(entryIndex));

		bool readOnly = isOverwrite;
		if (readOnly)
			ImGui::BeginDisabled();

		float colWidth = ImGui::GetContentRegionAvail().x;

		switch (settingType) {
		case SceneSettingsManager::SettingType::Boolean:
			{
				bool val = entry.value.is_boolean() ? entry.value.get<bool>() : (entry.value.get<int>() != 0);
				if (ImGui::Checkbox("##val", &val))
					manager->UpdateEntryValue(kSceneType, entryIndex, entry.value.is_boolean() ? json(val) : json(val ? 1 : 0));
			}
			break;
		case SceneSettingsManager::SettingType::Float:
			{
				float val = entry.value.is_number() ? entry.value.get<float>() : 0.0f;
				if (!std::isfinite(val))
					val = 0.0f;
				ImGui::SetNextItemWidth(colWidth);
				if (ImGui::InputFloat("##val", &val, 0.0f, 0.0f, "%.3f"))
					if (std::isfinite(val))
						manager->UpdateEntryValue(kSceneType, entryIndex, val, true);
				if (ImGui::IsItemDeactivatedAfterEdit())
					manager->SaveUserSettings(kSceneType);
			}
			break;
		case SceneSettingsManager::SettingType::Integer:
			{
				int val = entry.value.get<int>();
				ImGui::SetNextItemWidth(colWidth);
				if (ImGui::InputInt("##val", &val, 0, 0))
					manager->UpdateEntryValue(kSceneType, entryIndex, val, true);
				if (ImGui::IsItemDeactivatedAfterEdit())
					manager->SaveUserSettings(kSceneType);
			}
			break;
		default:
			ImGui::TextDisabled("(?)");
			break;
		}

		if (readOnly)
			ImGui::EndDisabled();

		// Toggle + X on a second line
		bool active = !entry.paused;
		if (Util::FeatureToggle("##active", &active))
			manager->TogglePauseEntry(kSceneType, entryIndex);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(entry.paused ? "Paused" : "Active");

		ImGui::SameLine();
		{
			auto styledButton = Util::ErrorButtonStyle();
			if (ImGui::Button("X", ImVec2(C::Em(C::SCENE_DELETE_BUTTON_EM), 0))) {
				if (isOverwrite) {
					popups.pendingDeleteIndex = entryIndex;
					popups.deleteSingleOverwrite.message = std::format(
						"Delete overwrite file '{}'?\nThis will permanently remove the file from disk.",
						entry.sourceFilename);
					popups.deleteSingleOverwrite.Request();
				} else {
					manager->RemoveSetting(kSceneType, entryIndex);
				}
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(isOverwrite ? "Delete overwrite file" : "Remove this setting");

		ImGui::PopID();
	}

	/// Build a setting map for entries from a specific source.
	struct SourceGroup
	{
		std::vector<SettingId> order;
		std::map<std::string, std::map<std::string, std::array<size_t, kPeriodCount>>> map;
	};

	static SourceGroup BuildSourceGroup(const std::vector<SceneSettingsManager::SettingEntry>& entries,
		EntrySource source)
	{
		SourceGroup group;
		for (size_t idx = 0; idx < entries.size(); ++idx) {
			const auto& e = entries[idx];
			if (e.source != source)
				continue;
			int p = static_cast<int>(e.period);
			if (p < 0 || p >= kPeriodCount)
				continue;
			auto& featureMap = group.map[e.featureShortName];
			auto [it, inserted] = featureMap.try_emplace(e.settingKey);
			if (inserted) {
				it->second.fill(SIZE_MAX);
				group.order.push_back({ e.featureShortName, e.settingKey });
			}
			it->second[p] = idx;
		}
		// Sort by feature name then setting key
		std::sort(group.order.begin(), group.order.end());
		return group;
	}

	/// Draw TOD table rows for a set of entries grouped by feature.
	static void DrawSourceRows(const SourceGroup& group, const float* factors)
	{
		auto& theme = globals::menu->GetSettings().Theme;
		std::string lastFeature;
		for (const auto& sid : group.order) {
			if (sid.feature != lastFeature) {
				lastFeature = sid.feature;

				// Feature header row with highlight and smaller text
				ImGui::TableNextRow();
				ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImGuiCol_TableRowBgAlt));
				ImGui::TableSetColumnIndex(0);
				ImGui::SetWindowFontScale(C::SCENE_TOD_FEATURE_TEXT_SCALE);
				auto featureLabel = SceneSettingsManager::GetFeatureDisplayName(sid.feature);
				ImGui::TextColored(theme.FeatureHeading.ColorDefault, "%s:", featureLabel.c_str());
				ImGui::SetWindowFontScale(1.0f);
			}

			auto mapIt = group.map.find(sid.feature);
			if (mapIt == group.map.end())
				continue;
			auto keyIt = mapIt->second.find(sid.key);
			if (keyIt == mapIt->second.end())
				continue;
			const auto& perKey = keyIt->second;

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Indent(C::Em(C::SCENE_ENTRY_INDENT_EM));
			ImGui::SetWindowFontScale(C::SCENE_TOD_FEATURE_TEXT_SCALE);
			ImGui::Text("%s", sid.key.c_str());
			ImGui::SetWindowFontScale(1.0f);
			ImGui::Unindent(C::Em(C::SCENE_ENTRY_INDENT_EM));

			for (int p = 0; p < kPeriodCount; ++p) {
				ImGui::TableSetColumnIndex(1 + p);

				bool isActive = factors[p] > 0.0f;
				if (!isActive)
					ImGui::PushStyleVar(ImGuiStyleVar_Alpha, C::SCENE_TOD_INACTIVE_ALPHA);

				DrawValueCell(perKey[p]);

				if (!isActive)
					ImGui::PopStyleVar();
			}
		}
	}

	/// Draw a TOD table for a single source group.
	static void DrawSourceTable(const SourceGroup& group, const float* factors, const char* tableId)
	{
		constexpr int kTotalCols = 1 + kPeriodCount;

		if (ImGui::BeginTable(tableId, kTotalCols,
				ImGuiTableFlags_Borders |
					ImGuiTableFlags_SizingFixedFit |
					ImGuiTableFlags_NoHostExtendX)) {
			ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthFixed, C::Em(C::SCENE_TOD_PARAM_COL_EM));
			for (int i = 0; i < kPeriodCount; ++i)
				ImGui::TableSetupColumn(SceneSettingsManager::kPeriodNames[i],
					ImGuiTableColumnFlags_WidthFixed, C::Em(C::SCENE_TOD_PERIOD_COL_EM));

			ImGui::TableSetupScrollFreeze(0, 1);

			// Header row with period names + active highlighting
			ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
			ImGui::TableSetColumnIndex(0);
			ImGui::TableHeader("Setting");
			for (int i = 0; i < kPeriodCount; ++i) {
				ImGui::TableSetColumnIndex(1 + i);
				bool isActive = factors[i] > 0.01f;
				if (!isActive)
					ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
				ImGui::TableHeader(SceneSettingsManager::kPeriodNames[i]);
				if (!isActive)
					ImGui::PopStyleColor();
			}

			DrawSourceRows(group, factors);
			ImGui::EndTable();
		}
	}

	void Draw()
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto& entries = manager->GetEntries(kSceneType);
		auto& theme = globals::menu->GetSettings().Theme;

		// Header
		ImGui::Text("Time of Day Settings");
		ImGui::SameLine();
		ImGui::TextDisabled("(Exterior Only)");

		auto currentPeriod = SceneSettingsManager::GetCurrentPeriod();
		ImGui::SameLine();
		ImGui::TextColored(theme.StatusPalette.InfoColor, "[%s %.1fh]",
			SceneSettingsManager::GetPeriodName(currentPeriod),
			SceneSettingsManager::GetCurrentGameHour());

		ImGui::Separator();

		// Popups
		SceneSettingsUI::DrawPopups(kSceneType, popups);

		// Per-period add-setting dropdowns — period name inline with dropdowns
		if (ImGui::CollapsingHeader("Add Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			for (int i = 0; i < kPeriodCount; ++i) {
				auto periodLabel = std::format("{}:", SceneSettingsManager::GetPeriodName(static_cast<Period>(i)));
				ImGui::PushID(i);
				SceneSettingsUI::DrawAddSettingUI(kSceneType, periodAddState[i],
					static_cast<Period>(i), periodLabel.c_str());
				ImGui::PopID();
			}
		}

		if (entries.empty()) {
			ImGui::Spacing();
			ImGui::TextColored(theme.StatusPalette.Disable,
				"No time-of-day settings configured.");
			ImGui::TextColored(theme.StatusPalette.Disable,
				"Select a feature and setting above to add overrides for each period.");
			return;
		}

		ImGui::Spacing();

		// Build separate maps for overwrite and user entries
		auto overwriteGroup = BuildSourceGroup(entries, EntrySource::Overwrite);
		auto userGroup = BuildSourceGroup(entries, EntrySource::User);

		// Get active period factors for highlighting
		float factors[kPeriodCount];
		manager->GetTimeOfDayFactors(factors);

		if (!overwriteGroup.order.empty()) {
			SceneSettingsUI::DrawSectionHeader("Overwrite Files", theme.StatusPalette.InfoColor, "##ow",
				manager->AreAllOverwritesPaused(kSceneType),
				[&] { manager->SetAllOverwritesPaused(kSceneType, !manager->AreAllOverwritesPaused(kSceneType)); },
				[&] { popups.deleteAllOverwrites.Request(); });
			DrawSourceTable(overwriteGroup, factors, "##TODOverwriteTable");
		}

		if (!userGroup.order.empty()) {
			SceneSettingsUI::DrawSectionHeader("User Settings", theme.FeatureHeading.ColorDefault, "##usr",
				manager->AreAllUserPaused(kSceneType),
				[&] { manager->SetAllUserPaused(kSceneType, !manager->AreAllUserPaused(kSceneType)); },
				[&] { popups.deleteAllUser.Request(); });
			DrawSourceTable(userGroup, factors, "##TODUserTable");
		}
	}
}
