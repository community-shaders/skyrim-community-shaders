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

	using SettingId = SceneSettingsUI::SettingId;
	using SourceGroup = SceneSettingsUI::SourceGroup;

	// Shared flyout states (one per flyout context to avoid conflicts)
	static Util::FlyoutState flyoutState;       // Per-cell flyout
	static Util::FlyoutState rowFlyoutState;    // Row-level flyout (setting name column)
	static Util::FlyoutState colFlyoutState;    // Column-level flyout (period headers)

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

		if (entry.paused)
			ImGui::BeginDisabled();

		SceneSettingsUI::DrawValueEditor(kSceneType, entryIndex, ImGui::GetContentRegionAvail().x, isOverwrite);

		if (entry.paused)
			ImGui::EndDisabled();

		// Flyout on hover with toggle / revert / delete
		ImGuiID cellId = ImGui::GetItemID();
		if (Util::BeginFlyout(flyoutState, cellId)) {
			auto result = SceneSettingsUI::DrawFlyoutControls(entry.paused);

			if (result.toggled)
				manager->TogglePauseEntry(kSceneType, entryIndex);
			if (result.reverted)
				manager->RevertEntryToDefault(kSceneType, entryIndex);
			if (result.deleted) {
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

			Util::EndFlyout(flyoutState);
		}

		ImGui::PopID();
	}

	/// Draw TOD table rows for a set of entries grouped by feature.
	static void DrawSourceRows(const SourceGroup& group, EntrySource source)
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
			ImVec2 cellStart = ImGui::GetCursorScreenPos();
			float cellWidth = ImGui::GetContentRegionAvail().x;

			ImGui::PushID(sid.key.c_str());
			ImGui::PushID(sid.feature.c_str());

			ImGui::Indent(C::Em(C::SCENE_ENTRY_INDENT_EM));
			ImGui::SetWindowFontScale(C::SCENE_TOD_FEATURE_TEXT_SCALE);
			ImGui::Text("%s", sid.key.c_str());
			ImGui::SetWindowFontScale(1.0f);

			// Row-level flyout on hover over full first-column cell
			ImGuiID rowId = ImGui::GetID("##rowFlyout");
			ImVec2 cellMin = cellStart;
			float rowH = std::max(ImGui::GetTextLineHeightWithSpacing(), ImGui::GetItemRectMax().y - cellStart.y);
			ImVec2 cellMax(cellStart.x + cellWidth, cellStart.y + rowH);
			if (Util::BeginFlyout(rowFlyoutState, rowId, cellMin, cellMax)) {
				const auto& entries = manager->GetEntries(kSceneType);
				bool allPaused = std::all_of(rowIndices.begin(), rowIndices.end(),
					[&](size_t i) { return i < entries.size() && entries[i].paused; });
				auto result = SceneSettingsUI::DrawFlyoutControls(allPaused, true);

				if (result.toggled)
					for (auto idx : rowIndices)
						if (idx < entries.size() && entries[idx].paused == allPaused)
							manager->TogglePauseEntry(kSceneType, idx);
				if (result.reverted)
					for (auto idx : rowIndices)
						manager->RevertEntryToDefault(kSceneType, idx);
				if (result.deleted) {
					if (isOverwrite) {
						const auto& entries2 = manager->GetEntries(kSceneType);
						std::set<std::string> filenames;
						for (auto idx : rowIndices)
							if (idx < entries2.size())
								filenames.insert(entries2[idx].sourceFilename);
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
						SceneSettingsUI::RemoveIndicesReversed(rowIndices, [&](size_t idx) { manager->RemoveSetting(kSceneType, idx); });
					}
				}
				Util::EndFlyout(rowFlyoutState);
			}

			ImGui::Unindent(C::Em(C::SCENE_ENTRY_INDENT_EM));
			ImGui::PopID();
			ImGui::PopID();

			for (int p = 0; p < kPeriodCount; ++p) {
				ImGui::TableSetColumnIndex(1 + p);

				DrawValueCell(perKey[p]);
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
	static void DrawSourceTable(const SourceGroup& group, const char* tableId, EntrySource source)
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

				ImGui::Text("%s", SceneSettingsManager::kPeriodNames[i]);

				const auto& indices = perPeriod[i];
				if (!indices.empty()) {
					ImGui::PushID(i);

					// Column-level flyout on hover over full column header cell
					ImGuiID colId = ImGui::GetID("##colFlyout");
					if (Util::BeginFlyout(colFlyoutState, colId)) {
						bool allPaused = std::all_of(indices.begin(), indices.end(),
							[&](size_t idx) { return idx < entries.size() && entries[idx].paused; });
						auto result = SceneSettingsUI::DrawFlyoutControls(allPaused, true);

						if (result.toggled)
							for (auto idx : indices)
								if (idx < entries.size() && entries[idx].paused == allPaused)
									manager->TogglePauseEntry(kSceneType, idx);
						if (result.reverted)
							for (auto idx : indices)
								manager->RevertEntryToDefault(kSceneType, idx);
						if (result.deleted) {
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
								SceneSettingsUI::RemoveIndicesReversed(indices, [&](size_t idx) { manager->RemoveSetting(kSceneType, idx); });
							}
						}
						// Close flyout after any delete action
						if (result.deleted) {
							colFlyoutState.isOpen = false;
							colFlyoutState.activeId = 0;
						}
						Util::EndFlyout(colFlyoutState);
					}

					ImGui::PopID();
				}
			}

			DrawSourceRows(group, source);
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
		auto overwriteGroup = SceneSettingsUI::BuildSourceGroup(entries, EntrySource::Overwrite);
		auto userGroup = SceneSettingsUI::BuildSourceGroup(entries, EntrySource::User);

		if (!overwriteGroup.order.empty()) {
			SceneSettingsUI::DrawSectionHeader("Overwrite Files", theme.StatusPalette.InfoColor, "##ow", manager->AreAllOverwritesPaused(kSceneType), [&] { manager->SetAllOverwritesPaused(kSceneType, !manager->AreAllOverwritesPaused(kSceneType)); }, [&] { popups.deleteAllOverwrites.Request(); });
			DrawSourceTable(overwriteGroup, "##TODOverwriteTable", EntrySource::Overwrite);
		}

		if (!userGroup.order.empty()) {
			SceneSettingsUI::DrawSectionHeader("User Settings", theme.FeatureHeading.ColorDefault, "##usr", manager->AreAllUserPaused(kSceneType), [&] { manager->SetAllUserPaused(kSceneType, !manager->AreAllUserPaused(kSceneType)); }, [&] { popups.deleteAllUser.Request(); });
			DrawSourceTable(userGroup, "##TODUserTable", EntrySource::User);
		}
	}
}
