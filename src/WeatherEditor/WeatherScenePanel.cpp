#include "WeatherScenePanel.h"

#include <algorithm>
#include <array>
#include <map>
#include <set>

#include "../Globals.h"
#include "../Menu.h"
#include "../Menu/ThemeManager.h"
#include "../SceneSettingsManager.h"
#include "SceneSettingsUI.h"

namespace WeatherScenePanel
{
	using C = ThemeManager::Constants;
	using Period = SceneSettingsManager::TimeOfDayPeriod;
	using EntrySource = SceneSettingsManager::EntrySource;
	static constexpr int kPeriodCount = SceneSettingsManager::kPeriodCount;

	// Per-weather UI state (keyed by FormID to support multiple open widgets)
	struct PanelState
	{
		SceneSettingsUI::AddSettingState periodAddStates[SceneSettingsManager::kPeriodCount];
		SceneSettingsUI::AddSettingState allPeriodsAddState;
		Util::FlyoutState flyoutState;      // Per-cell flyout
		Util::FlyoutState rowFlyoutState;   // Row-level flyout (setting name)
		Util::FlyoutState colFlyoutState;   // Column-level flyout (period headers)
	};
	static std::map<RE::FormID, PanelState> panelStates;

	static PanelState& GetState(RE::FormID id) { return panelStates[id]; }

	// --- Flat mode (InteriorOnly style) ---

	using SettingId = SceneSettingsUI::SettingId;
	using SourceGroup = SceneSettingsUI::SourceGroup;

	// --- Flat mode (InteriorOnly style) ---

	/// Draw a single flat setting row with flyout controls.
	static void DrawFlatSettingRow(RE::FormID weatherId, const std::vector<size_t>& rowIndices,
		const std::string& key, PanelState& state)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto& entries = manager->GetWeatherConfig(weatherId).entries;
		if (rowIndices.empty())
			return;

		bool anyPaused = std::any_of(rowIndices.begin(), rowIndices.end(),
			[&](size_t i) { return i < entries.size() && entries[i].paused; });
		bool anyOverwrite = std::any_of(rowIndices.begin(), rowIndices.end(),
			[&](size_t i) { return i < entries.size() && entries[i].source == EntrySource::Overwrite; });

		ImGui::PushID(key.c_str());

		float availWidth = ImGui::GetContentRegionAvail().x;
		ImGui::Text("%s", key.c_str());

		ImGui::SameLine(availWidth * C::SCENE_VALUE_LABEL_OFFSET_RATIO);
		ImGui::PushID(static_cast<int>(rowIndices[0]));

		if (anyPaused)
			ImGui::BeginDisabled();

		SceneSettingsUI::DrawWeatherValueEditor(weatherId, rowIndices, C::Em(C::SCENE_VALUE_INPUT_EM), anyOverwrite);

		if (anyPaused)
			ImGui::EndDisabled();

		ImGuiID cellId = ImGui::GetItemID();
		if (Util::BeginFlyout(state.flyoutState, cellId)) {
			bool allPaused = std::all_of(rowIndices.begin(), rowIndices.end(),
				[&](size_t i) { return i < entries.size() && entries[i].paused; });
			auto result = SceneSettingsUI::DrawFlyoutControls(allPaused, true);

			if (result.toggled)
				for (auto idx : rowIndices)
					if (idx < entries.size() && entries[idx].paused == allPaused)
						manager->TogglePauseWeatherEntry(weatherId, idx);
			if (result.reverted)
				for (auto idx : rowIndices)
					manager->RevertWeatherEntryToDefault(weatherId, idx);
			if (result.deleted)
				SceneSettingsUI::RemoveIndicesReversed(rowIndices, [&](size_t idx) { manager->RemoveWeatherSetting(weatherId, idx); });

			Util::EndFlyout(state.flyoutState);
		}

		ImGui::PopID();
		ImGui::PopID();
	}

	/// Draw flat entries grouped by feature using collapsible tree nodes (InteriorOnly style).
	static void DrawFlatGroupedEntries(RE::FormID weatherId, const std::vector<size_t>& sourceIndices,
		PanelState& state)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto& entries = manager->GetWeatherConfig(weatherId).entries;

		// Group by feature -> key -> period indices
		std::map<std::string, std::map<std::string, std::vector<size_t>>> grouped;
		for (auto idx : sourceIndices) {
			if (idx >= entries.size())
				continue;
			grouped[entries[idx].featureShortName][entries[idx].settingKey].push_back(idx);
		}

		bool firstGroup = true;
		for (const auto& [featureName, settings] : grouped) {
			SceneSettingsUI::DrawGroupSeparator(firstGroup);

			auto label = SceneSettingsManager::GetFeatureDisplayName(featureName) + ":";
			if (ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::Indent(C::Em(C::SCENE_ENTRY_INDENT_EM));
				for (const auto& [key, indices] : settings)
					DrawFlatSettingRow(weatherId, indices, key, state);
				ImGui::Unindent(C::Em(C::SCENE_ENTRY_INDENT_EM));
				ImGui::TreePop();
			}
		}
	}

	/// Draw flat mode with Overwrite Files / User Settings section headers (InteriorOnly style).
	static void DrawFlatSections(RE::FormID weatherId, PanelState& state)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto& entries = manager->GetWeatherConfig(weatherId).entries;
		auto& theme = globals::menu->GetSettings().Theme;

		std::vector<size_t> overwriteIndices, userIndices;
		SceneSettingsUI::SplitBySource(entries, overwriteIndices, userIndices);

		if (!overwriteIndices.empty()) {
			bool allPaused = std::all_of(overwriteIndices.begin(), overwriteIndices.end(),
				[&](size_t i) { return entries[i].paused; });
			SceneSettingsUI::DrawSectionHeader("Overwrite Files", theme.StatusPalette.InfoColor, "##ow",
				allPaused,
				[&] { for (auto idx : overwriteIndices) if (entries[idx].paused == allPaused) manager->TogglePauseWeatherEntry(weatherId, idx); },
				[&] { SceneSettingsUI::RemoveIndicesReversed(overwriteIndices, [&](size_t idx) { manager->RemoveWeatherSetting(weatherId, idx); }); });
			DrawFlatGroupedEntries(weatherId, overwriteIndices, state);
		}

		if (!userIndices.empty()) {
			bool allPaused = std::all_of(userIndices.begin(), userIndices.end(),
				[&](size_t i) { return entries[i].paused; });
			SceneSettingsUI::DrawSectionHeader("User Settings", theme.FeatureHeading.ColorDefault, "##usr",
				allPaused,
				[&] { for (auto idx : userIndices) if (entries[idx].paused == allPaused) manager->TogglePauseWeatherEntry(weatherId, idx); },
				[&] { SceneSettingsUI::RemoveIndicesReversed(userIndices, [&](size_t idx) { manager->RemoveWeatherSetting(weatherId, idx); }); });
			DrawFlatGroupedEntries(weatherId, userIndices, state);
		}
	}

	// --- TOD mode helpers ---

	static void DrawTodValueCell(RE::FormID weatherId, size_t entryIndex, PanelState& state)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto& entries = manager->GetWeatherConfig(weatherId).entries;

		if (entryIndex == SIZE_MAX) {
			ImGui::TextDisabled("--");
			return;
		}

		const auto& entry = entries[entryIndex];
		bool isOverwrite = entry.source == EntrySource::Overwrite;
		ImGui::PushID(static_cast<int>(entryIndex));

		if (entry.paused)
			ImGui::BeginDisabled();

		SceneSettingsUI::DrawWeatherValueEditor(weatherId, entryIndex, ImGui::GetContentRegionAvail().x, isOverwrite);

		if (entry.paused)
			ImGui::EndDisabled();

		ImGuiID cellId = ImGui::GetItemID();
		if (Util::BeginFlyout(state.flyoutState, cellId)) {
			auto result = SceneSettingsUI::DrawFlyoutControls(entry.paused);

			if (result.toggled)
				manager->TogglePauseWeatherEntry(weatherId, entryIndex);
			if (result.reverted)
				manager->RevertWeatherEntryToDefault(weatherId, entryIndex);
			if (result.deleted)
				manager->RemoveWeatherSetting(weatherId, entryIndex);

			Util::EndFlyout(state.flyoutState);
		}

		ImGui::PopID();
	}

	static void DrawTodTableForSource(RE::FormID weatherId, PanelState& state,
		const std::vector<size_t>& sourceIndices, const char* tableId)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto& entries = manager->GetWeatherConfig(weatherId).entries;
		auto& theme = globals::menu->GetSettings().Theme;

		// Build source group filtered to only the given indices
		std::set<size_t> allowed(sourceIndices.begin(), sourceIndices.end());
		SourceGroup group;
		for (size_t idx = 0; idx < entries.size(); ++idx) {
			if (allowed.find(idx) == allowed.end())
				continue;
			const auto& e = entries[idx];
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
		std::sort(group.order.begin(), group.order.end());

		if (group.order.empty())
			return;

		constexpr int kTotalCols = 1 + kPeriodCount;

		if (!ImGui::BeginTable(tableId, kTotalCols,
				ImGuiTableFlags_Borders |
					ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX))
			return;

		ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthFixed, C::Em(C::SCENE_TOD_PARAM_COL_EM));
		for (int i = 0; i < kPeriodCount; ++i)
			ImGui::TableSetupColumn(SceneSettingsManager::kPeriodNames[i],
				ImGuiTableColumnFlags_WidthFixed, C::Em(C::SCENE_TOD_PERIOD_COL_EM));
		ImGui::TableSetupScrollFreeze(0, 1);

		// Collect per-period indices for column flyout controls (filtered)
		std::array<std::vector<size_t>, kPeriodCount> perPeriod{};
		for (auto idx : sourceIndices) {
			if (idx >= entries.size())
				continue;
			int p = static_cast<int>(entries[idx].period);
			if (p >= 0 && p < kPeriodCount)
				perPeriod[p].push_back(idx);
		}

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

				ImGuiID colId = ImGui::GetID("##colFlyout");
				if (Util::BeginFlyout(state.colFlyoutState, colId)) {
					bool allPaused = std::all_of(indices.begin(), indices.end(),
						[&](size_t idx) { return idx < entries.size() && entries[idx].paused; });
					auto result = SceneSettingsUI::DrawFlyoutControls(allPaused, true);

					if (result.toggled)
						for (auto idx : indices)
							if (idx < entries.size() && entries[idx].paused == allPaused)
								manager->TogglePauseWeatherEntry(weatherId, idx);
					if (result.reverted)
						for (auto idx : indices)
							manager->RevertWeatherEntryToDefault(weatherId, idx);
					if (result.deleted)
						SceneSettingsUI::RemoveIndicesReversed(indices, [&](size_t idx) { manager->RemoveWeatherSetting(weatherId, idx); });
					if (result.deleted) {
						state.colFlyoutState.isOpen = false;
						state.colFlyoutState.activeId = 0;
					}
					Util::EndFlyout(state.colFlyoutState);
				}

				ImGui::PopID();
			}
		}

		// Data rows
		std::string lastFeature;
		for (const auto& sid : group.order) {
			if (sid.feature != lastFeature) {
				lastFeature = sid.feature;
				ImGui::TableNextRow();
				ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImGuiCol_TableRowBgAlt));
				ImGui::TableSetColumnIndex(0);
				ImGui::SetWindowFontScale(C::SCENE_TOD_FEATURE_TEXT_SCALE);
				ImGui::TextColored(theme.FeatureHeading.ColorDefault, "%s:",
					SceneSettingsManager::GetFeatureDisplayName(sid.feature).c_str());
				ImGui::SetWindowFontScale(1.0f);
			}

			auto mapIt = group.map.find(sid.feature);
			if (mapIt == group.map.end())
				continue;
			auto keyIt = mapIt->second.find(sid.key);
			if (keyIt == mapIt->second.end())
				continue;
			const auto& perKey = keyIt->second;

			// Collect valid indices for row controls
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
			if (Util::BeginFlyout(state.rowFlyoutState, rowId, cellMin, cellMax)) {
				bool allPaused = std::all_of(rowIndices.begin(), rowIndices.end(),
					[&](size_t i) { return i < entries.size() && entries[i].paused; });
				auto result = SceneSettingsUI::DrawFlyoutControls(allPaused, true);

				if (result.toggled)
					for (auto idx : rowIndices)
						if (idx < entries.size() && entries[idx].paused == allPaused)
							manager->TogglePauseWeatherEntry(weatherId, idx);
				if (result.reverted)
					for (auto idx : rowIndices)
						manager->RevertWeatherEntryToDefault(weatherId, idx);
				if (result.deleted)
					SceneSettingsUI::RemoveIndicesReversed(rowIndices, [&](size_t idx) { manager->RemoveWeatherSetting(weatherId, idx); });
				if (result.deleted) {
					state.rowFlyoutState.isOpen = false;
					state.rowFlyoutState.activeId = 0;
				}
				Util::EndFlyout(state.rowFlyoutState);
			}

			ImGui::Unindent(C::Em(C::SCENE_ENTRY_INDENT_EM));
			ImGui::PopID();
			ImGui::PopID();

			// Period columns
			for (int p = 0; p < kPeriodCount; ++p) {
				ImGui::TableSetColumnIndex(1 + p);
				DrawTodValueCell(weatherId, perKey[p], state);
			}
		}

		ImGui::EndTable();
	}

	/// Draw TOD mode with Overwrite Files / User Settings section headers.
	static void DrawTodSections(RE::FormID weatherId, PanelState& state)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto& entries = manager->GetWeatherConfig(weatherId).entries;
		auto& theme = globals::menu->GetSettings().Theme;

		std::vector<size_t> overwriteIndices, userIndices;
		SceneSettingsUI::SplitBySource(entries, overwriteIndices, userIndices);

		if (!overwriteIndices.empty()) {
			bool allPaused = std::all_of(overwriteIndices.begin(), overwriteIndices.end(),
				[&](size_t i) { return entries[i].paused; });
			SceneSettingsUI::DrawSectionHeader("Overwrite Files", theme.StatusPalette.InfoColor, "##ow",
				allPaused,
				[&] { for (auto idx : overwriteIndices) if (entries[idx].paused == allPaused) manager->TogglePauseWeatherEntry(weatherId, idx); },
				[&] { SceneSettingsUI::RemoveIndicesReversed(overwriteIndices, [&](size_t idx) { manager->RemoveWeatherSetting(weatherId, idx); }); });
			DrawTodTableForSource(weatherId, state, overwriteIndices, "##TodOverwrite");
		}

		if (!userIndices.empty()) {
			bool allPaused = std::all_of(userIndices.begin(), userIndices.end(),
				[&](size_t i) { return entries[i].paused; });
			SceneSettingsUI::DrawSectionHeader("User Settings", theme.FeatureHeading.ColorDefault, "##usr",
				allPaused,
				[&] { for (auto idx : userIndices) if (entries[idx].paused == allPaused) manager->TogglePauseWeatherEntry(weatherId, idx); },
				[&] { SceneSettingsUI::RemoveIndicesReversed(userIndices, [&](size_t idx) { manager->RemoveWeatherSetting(weatherId, idx); }); });
			DrawTodTableForSource(weatherId, state, userIndices, "##TodUser");
		}
	}

	// --- Main Draw ---

	void Draw(RE::FormID weatherId)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		auto& state = GetState(weatherId);
		const auto& config = manager->GetWeatherConfig(weatherId);
		auto& theme = globals::menu->GetSettings().Theme;
		bool showTod = manager->IsWeatherShowTimeOfDay(weatherId);

		ImGui::Text("Scene Settings");
		ImGui::Separator();

		// TOD toggle + add buttons
		{
			bool toggled = showTod;
			if (ImGui::Checkbox("Time of Day", &toggled))
				manager->SetWeatherShowTimeOfDay(weatherId, toggled);
			showTod = toggled;
		}

		if (showTod) {
			// Per-period add buttons
			for (int i = 0; i < kPeriodCount; ++i) {
				if (i > 0)
					ImGui::SameLine();
				ImGui::PushID(i);
				auto label = std::format("Add {}", SceneSettingsManager::kPeriodNames[i]);
				if (ImGui::SmallButton(label.c_str()))
					SceneSettingsUI::OpenWeatherAddDialog(weatherId, state.periodAddStates[i]);
				ImGui::PopID();
			}
			ImGui::SameLine();
		}

		// "Add" button (Delete All moved to section headers)
		if (ImGui::SmallButton(showTod ? "Add All" : "Add Setting"))
			SceneSettingsUI::OpenWeatherAddDialog(weatherId, state.allPeriodsAddState);

		// Draw add dialogs
		SceneSettingsUI::DrawWeatherAddDialog(weatherId, state.allPeriodsAddState, Period::Count, true);
		if (showTod)
			for (int p = 0; p < kPeriodCount; ++p)
				SceneSettingsUI::DrawWeatherAddDialog(weatherId, state.periodAddStates[p], static_cast<Period>(p));

		// Empty state
		if (config.entries.empty()) {
			ImGui::Spacing();
			ImGui::TextColored(theme.StatusPalette.Disable,
				"No scene settings for this weather.");
			ImGui::TextColored(theme.StatusPalette.Disable,
				"Use the Add buttons above to add overrides.");
			return;
		}

		if (showTod)
			DrawTodSections(weatherId, state);
		else
			DrawFlatSections(weatherId, state);
	}
}
