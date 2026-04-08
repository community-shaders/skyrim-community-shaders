#include "TimeOfDayPanel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>

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
	static SceneSettingsUI::AddSettingState allPeriodsAddState;

	/// Reset and open the add-setting dialog for a given state.
	static void OpenAddDialog(SceneSettingsUI::AddSettingState& state)
	{
		state.Reset();
		state.dialogOpen = true;
	}

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

	// Shared flyout state (one per panel)
	static Util::FlyoutState flyoutState;

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

		ImGui::PushID(static_cast<int>(entryIndex));

		bool readOnly = isOverwrite;
		if (readOnly)
			ImGui::BeginDisabled();

		SceneSettingsUI::DrawValueEditor(kSceneType, entryIndex, ImGui::GetContentRegionAvail().x);

		if (readOnly)
			ImGui::EndDisabled();

		// Flyout on hover with toggle / revert / delete
		ImGuiID cellId = ImGui::GetItemID();
		if (Util::BeginFlyout(flyoutState, cellId)) {
			bool active = !entry.paused;
			if (Util::SmallFeatureToggle("##active", &active))
				manager->TogglePauseEntry(kSceneType, entryIndex);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text(entry.paused ? "Paused" : "Active");

			ImGui::SameLine();
			auto* menu = globals::menu;
			float iconH = ImGui::GetFrameHeight() * 0.7f;
			if (menu && Util::IconButton("##revert", menu->uiIcons.undo.texture, ImVec2(iconH, iconH)))
				manager->RevertEntryToDefault(kSceneType, entryIndex);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("Revert to default");

			ImGui::SameLine();
			if (Util::ThemedDeleteButton("X")) {
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
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text(isOverwrite ? "Delete overwrite file" : "Remove this setting");

			Util::EndFlyout(flyoutState);
		}

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
	static void DrawSourceRows(const SourceGroup& group, const float* factors, EntrySource source)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		auto& theme = globals::menu->GetSettings().Theme;
		bool isOverwrite = source == EntrySource::Overwrite;
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

			// Collect valid indices for this row
			std::vector<size_t> rowIndices;
			for (int p = 0; p < kPeriodCount; ++p)
				if (perKey[p] != SIZE_MAX)
					rowIndices.push_back(perKey[p]);

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::PushID(sid.key.c_str());
			ImGui::PushID(sid.feature.c_str());

			ImGui::Indent(C::Em(C::SCENE_ENTRY_INDENT_EM));
			ImGui::SetWindowFontScale(C::SCENE_TOD_FEATURE_TEXT_SCALE);
			ImGui::Text("%s", sid.key.c_str());
			ImGui::SetWindowFontScale(1.0f);

			// Row-level toggle + delete
			{
				const auto& entries = manager->GetEntries(kSceneType);
				bool allPaused = std::all_of(rowIndices.begin(), rowIndices.end(),
					[&](size_t i) { return i < entries.size() && entries[i].paused; });
				bool active = !allPaused;
				if (Util::FeatureToggle("##rowActive", &active))
					for (auto idx : rowIndices)
						if (idx < entries.size() && entries[idx].paused == active)
							manager->TogglePauseEntry(kSceneType, idx);
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text(allPaused ? "Unpause all periods" : "Pause all periods");

				ImGui::SameLine();
				{
					auto styledButton = Util::ErrorButtonStyle();
					if (ImGui::Button("X", ImVec2(C::Em(C::SCENE_DELETE_BUTTON_EM), 0))) {
						if (isOverwrite) {
							// Collect unique filenames for the confirmation message
							std::set<std::string> filenames;
							for (auto idx : rowIndices)
								if (idx < entries.size())
									filenames.insert(entries[idx].sourceFilename);
							std::string fileList;
							for (const auto& f : filenames) {
								if (!fileList.empty())
									fileList += ", ";
								fileList += "'" + f + "'";
							}
							popups.pendingDeleteRow = rowIndices;
							popups.deleteRowOverwrite.message = std::format(
								"Delete overwrite entries from {}?\nThis will permanently remove the file(s) from disk.",
								fileList);
							popups.deleteRowOverwrite.Request();
						} else {
							// Delete user entries in reverse order so indices stay valid
							std::sort(rowIndices.begin(), rowIndices.end(), std::greater<>());
							for (auto idx : rowIndices)
								manager->RemoveSetting(kSceneType, idx);
						}
					}
				}
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text(isOverwrite ? "Delete row from disk" : "Remove all periods");
			}

			ImGui::Unindent(C::Em(C::SCENE_ENTRY_INDENT_EM));
			ImGui::PopID();
			ImGui::PopID();

			for (int p = 0; p < kPeriodCount; ++p) {
				ImGui::TableSetColumnIndex(1 + p);

				bool isActive = factors[p] > C::SCENE_TOD_ACTIVE_THRESHOLD;
				if (!isActive)
					ImGui::PushStyleVar(ImGuiStyleVar_Alpha, C::SCENE_TOD_INACTIVE_ALPHA);

				DrawValueCell(perKey[p]);

				if (!isActive)
					ImGui::PopStyleVar();
			}
		}
	}

	/// Collect all entry indices per period from a source group.
	static void CollectPerPeriodIndices(const SourceGroup& group, std::array<std::vector<size_t>, kPeriodCount>& out)
	{
		for (const auto& [_, featureMap] : group.map)
			for (const auto& [__, perKey] : featureMap)
				for (int p = 0; p < kPeriodCount; ++p)
					if (perKey[p] != SIZE_MAX)
						out[p].push_back(perKey[p]);
	}

	/// Draw a TOD table for a single source group.
	static void DrawSourceTable(const SourceGroup& group, const float* factors, const char* tableId, EntrySource source)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto& entries = manager->GetEntries(kSceneType);
		bool isOverwrite = source == EntrySource::Overwrite;
		constexpr int kTotalCols = 1 + kPeriodCount;

		// Pre-collect per-period indices for header controls
		std::array<std::vector<size_t>, kPeriodCount> perPeriod{};
		CollectPerPeriodIndices(group, perPeriod);

		if (ImGui::BeginTable(tableId, kTotalCols,
				ImGuiTableFlags_Borders |
					ImGuiTableFlags_SizingFixedFit |
					ImGuiTableFlags_NoHostExtendX)) {
			ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthFixed, C::Em(C::SCENE_TOD_PARAM_COL_EM));
			for (int i = 0; i < kPeriodCount; ++i)
				ImGui::TableSetupColumn(SceneSettingsManager::kPeriodNames[i],
					ImGuiTableColumnFlags_WidthFixed, C::Em(C::SCENE_TOD_PERIOD_COL_EM));

			ImGui::TableSetupScrollFreeze(0, 1);

			// Header row with period names + integrated per-column controls
			ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
			ImGui::TableSetColumnIndex(0);
			ImGui::TableHeader("Setting");
			for (int i = 0; i < kPeriodCount; ++i) {
				ImGui::TableSetColumnIndex(1 + i);
				bool isActive = factors[i] > C::SCENE_TOD_ACTIVE_THRESHOLD;
				if (!isActive)
					ImGui::PushStyleVar(ImGuiStyleVar_Alpha, C::SCENE_TOD_INACTIVE_ALPHA);

				ImGui::Text("%s", SceneSettingsManager::kPeriodNames[i]);

				const auto& indices = perPeriod[i];
				if (!indices.empty()) {
					ImGui::PushID(i);

					bool allPaused = std::all_of(indices.begin(), indices.end(),
						[&](size_t idx) { return idx < entries.size() && entries[idx].paused; });
					bool active = !allPaused;
					if (Util::FeatureToggle("##colActive", &active))
						for (auto idx : indices)
							if (idx < entries.size() && entries[idx].paused == active)
								manager->TogglePauseEntry(kSceneType, idx);
					if (auto _tt = Util::HoverTooltipWrapper())
						ImGui::Text(allPaused ? "Unpause all in this period" : "Pause all in this period");

					ImGui::SameLine();
					{
						auto styledButton = Util::ErrorButtonStyle();
						if (ImGui::Button("X", ImVec2(C::Em(C::SCENE_DELETE_BUTTON_EM), 0))) {
							if (isOverwrite) {
								std::set<std::string> filenames;
								for (auto idx : indices)
									if (idx < entries.size())
										filenames.insert(entries[idx].sourceFilename);
								std::string fileList;
								for (const auto& f : filenames) {
									if (!fileList.empty())
										fileList += ", ";
									fileList += "'" + f + "'";
								}
								popups.pendingDeleteRow = indices;
								popups.deleteRowOverwrite.message = std::format(
									"Delete all {} overwrite entries?\nThis will permanently remove file(s) {} from disk.",
									SceneSettingsManager::kPeriodNames[i], fileList);
								popups.deleteRowOverwrite.Request();
							} else {
								auto sorted = indices;
								std::sort(sorted.begin(), sorted.end(), std::greater<>());
								for (auto idx : sorted)
									manager->RemoveSetting(kSceneType, idx);
							}
						}
					}
					if (auto _tt = Util::HoverTooltipWrapper())
						ImGui::Text(isOverwrite ? "Delete all in this period" : "Remove all in this period");

					ImGui::PopID();
				}

				if (!isActive)
					ImGui::PopStyleVar();
			}

			DrawSourceRows(group, factors, source);
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

		// Add buttons: inline row of named buttons matching section header style
		for (int i = 0; i < kPeriodCount; ++i) {
			if (i > 0)
				ImGui::SameLine();
			ImGui::PushID(i);
			auto label = std::format("Add {}", SceneSettingsManager::kPeriodNames[i]);
			if (ImGui::SmallButton(label.c_str()))
				OpenAddDialog(periodAddState[i]);
			ImGui::PopID();
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Add All"))
			OpenAddDialog(allPeriodsAddState);

		// Draw all add-setting dialogs (no-op when not open)
		for (int i = 0; i < kPeriodCount; ++i)
			SceneSettingsUI::DrawAddSettingDialog(kSceneType, periodAddState[i], static_cast<Period>(i));
		SceneSettingsUI::DrawAddSettingDialog(kSceneType, allPeriodsAddState, Period::Count, true);

		ImGui::Separator();

		// Popups
		SceneSettingsUI::DrawPopups(kSceneType, popups);

		if (entries.empty()) {
			ImGui::Spacing();
			ImGui::TextColored(theme.StatusPalette.Disable,
				"No time-of-day settings configured.");
			ImGui::TextColored(theme.StatusPalette.Disable,
				"Use the Add buttons above to add overrides for each period.");
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
			SceneSettingsUI::DrawSectionHeader("Overwrite Files", theme.StatusPalette.InfoColor, "##ow", manager->AreAllOverwritesPaused(kSceneType), [&] { manager->SetAllOverwritesPaused(kSceneType, !manager->AreAllOverwritesPaused(kSceneType)); }, [&] { popups.deleteAllOverwrites.Request(); });
			DrawSourceTable(overwriteGroup, factors, "##TODOverwriteTable", EntrySource::Overwrite);
		}

		if (!userGroup.order.empty()) {
			SceneSettingsUI::DrawSectionHeader("User Settings", theme.FeatureHeading.ColorDefault, "##usr", manager->AreAllUserPaused(kSceneType), [&] { manager->SetAllUserPaused(kSceneType, !manager->AreAllUserPaused(kSceneType)); }, [&] { popups.deleteAllUser.Request(); });
			DrawSourceTable(userGroup, factors, "##TODUserTable", EntrySource::User);
		}
	}
}
