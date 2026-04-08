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
		SceneSettingsUI::AddSettingState addState;
		SceneSettingsUI::AddSettingState periodAddStates[SceneSettingsManager::kPeriodCount];
		SceneSettingsUI::AddSettingState allPeriodsAddState;
		Util::FlyoutState flyoutState;      // Per-cell flyout
		Util::FlyoutState rowFlyoutState;   // Row-level flyout (setting name)
		Util::FlyoutState colFlyoutState;   // Column-level flyout (period headers)
		Util::ConfirmationPopup todConfirm{
			"Switch Time of Day Mode?",
			"This will clear all existing entries for this weather.\nAre you sure?",
			"Switch"
		};
		bool pendingTodValue = false;  // The value to set if confirmed
	};
	static std::map<RE::FormID, PanelState> panelStates;

	static PanelState& GetState(RE::FormID id) { return panelStates[id]; }

	// --- Value editor for weather entries (mirrors SceneSettingsUI::DrawValueEditor) ---

	static void DrawWeatherValueEditor(RE::FormID weatherId, size_t index, float inputWidth)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto& entry = manager->GetWeatherConfig(weatherId).entries[index];
		auto settingType = SceneSettingsManager::DetectSettingType(entry.value);

		switch (settingType) {
		case SceneSettingsManager::SettingType::Boolean:
			{
				bool val = entry.value.is_boolean() ? entry.value.get<bool>() : (entry.value.get<int>() != 0);
				if (ImGui::Checkbox("##val", &val))
					manager->UpdateWeatherEntryValue(weatherId, index, entry.value.is_boolean() ? json(val) : json(val ? 1 : 0));
			}
			break;
		case SceneSettingsManager::SettingType::Float:
			{
				float val = entry.value.is_number() ? entry.value.get<float>() : 0.0f;
				if (!std::isfinite(val))
					val = 0.0f;
				ImGui::SetNextItemWidth(inputWidth);
				if (ImGui::InputFloat("##val", &val, 0.0f, 0.0f, "%.3f"))
					if (std::isfinite(val))
						manager->UpdateWeatherEntryValue(weatherId, index, val, true);
				if (ImGui::IsItemDeactivatedAfterEdit())
					manager->SaveWeatherSceneSettings(weatherId);
			}
			break;
		case SceneSettingsManager::SettingType::Integer:
			{
				int val = entry.value.get<int>();
				ImGui::SetNextItemWidth(inputWidth);
				if (ImGui::InputInt("##val", &val, 0, 0))
					manager->UpdateWeatherEntryValue(weatherId, index, val, true);
				if (ImGui::IsItemDeactivatedAfterEdit())
					manager->SaveWeatherSceneSettings(weatherId);
			}
			break;
		default:
			ImGui::TextDisabled("(unsupported type)");
			break;
		}
	}

	// --- Flat mode (InteriorOnly style) ---

	static void DrawFlatValueCell(RE::FormID weatherId, size_t entryIndex, PanelState& state)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto& entries = manager->GetWeatherConfig(weatherId).entries;
		const auto& entry = entries[entryIndex];

		ImGui::PushID(static_cast<int>(entryIndex));

		DrawWeatherValueEditor(weatherId, entryIndex, ImGui::GetContentRegionAvail().x);

		// Flyout on hover
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

	static void DrawFlatEntries(RE::FormID weatherId, PanelState& state)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto& entries = manager->GetWeatherConfig(weatherId).entries;

		// Group by feature
		std::map<std::string, std::vector<size_t>> grouped;
		for (size_t i = 0; i < entries.size(); ++i)
			grouped[entries[i].featureShortName].push_back(i);

		for (auto& [_, indices] : grouped)
			std::sort(indices.begin(), indices.end(), [&entries](size_t a, size_t b) {
				return entries[a].settingKey < entries[b].settingKey;
			});

		bool first = true;
		for (const auto& [featureName, indices] : grouped) {
			if (!first) {
				auto sepColor = ImGui::GetStyleColorVec4(ImGuiCol_Separator);
				sepColor.w *= C::SCENE_GROUP_SEPARATOR_ALPHA;
				ImGui::PushStyleColor(ImGuiCol_Separator, sepColor);
				ImGui::Separator();
				ImGui::PopStyleColor();
			}
			first = false;

			auto label = SceneSettingsManager::GetFeatureDisplayName(featureName) + ":";
			if (ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::Indent(C::Em(C::SCENE_ENTRY_INDENT_EM));
				for (auto i : indices) {
					ImGui::PushID(static_cast<int>(i));
					float availWidth = ImGui::GetContentRegionAvail().x;
					ImGui::Text("%s", entries[i].settingKey.c_str());
					ImGui::SameLine(availWidth * C::SCENE_VALUE_LABEL_OFFSET_RATIO);
					DrawFlatValueCell(weatherId, i, state);
					ImGui::PopID();
				}
				ImGui::Unindent(C::Em(C::SCENE_ENTRY_INDENT_EM));
				ImGui::TreePop();
			}
		}
	}

	// --- Add setting dialog (adapted for weather entries) ---

	static void DrawWeatherAddDialog(RE::FormID weatherId, SceneSettingsUI::AddSettingState& addState,
		bool isTod, Period period = Period::Count, bool addToAllPeriods = false)
	{
		if (!addState.dialogOpen)
			return;

		auto* manager = SceneSettingsManager::GetSingleton();
		ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(C::Em(C::SCENE_ADD_DIALOG_WIDTH_EM), 0));

		if (!Util::BeginWithRoundedClose("Add Weather Setting", &addState.dialogOpen,
				ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::End();
			return;
		}

		if (addState.cachedFeatureNames.empty())
			addState.cachedFeatureNames = SceneSettingsManager::GetExteriorRelevantFeatureNames();

		auto displayName = (addState.selectedFeatureIdx >= 0 &&
							   addState.selectedFeatureIdx < static_cast<int>(addState.cachedFeatureNames.size()))
		                       ? SceneSettingsManager::GetFeatureDisplayName(addState.cachedFeatureNames[addState.selectedFeatureIdx])
		                       : std::string("Select Feature...");

		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::BeginCombo("##FeatureSelect", displayName.c_str())) {
			for (int i = 0; i < static_cast<int>(addState.cachedFeatureNames.size()); ++i) {
				auto itemLabel = SceneSettingsManager::GetFeatureDisplayName(addState.cachedFeatureNames[i]);
				if (ImGui::Selectable(itemLabel.c_str(), i == addState.selectedFeatureIdx)) {
					addState.selectedFeatureIdx = i;
					addState.cachedSettingKeys = isTod
					                                 ? SceneSettingsManager::GetTransitionableSettingKeys(addState.cachedFeatureNames[i])
					                                 : SceneSettingsManager::GetFeatureSettingKeys(addState.cachedFeatureNames[i]);
					addState.selectedSettings.assign(addState.cachedSettingKeys.size(), false);
				}
				if (i == addState.selectedFeatureIdx)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		bool hasFeature = addState.selectedFeatureIdx >= 0 && !addState.cachedSettingKeys.empty();
		if (hasFeature) {
			ImGui::Spacing();
			ImGui::Separator();
			if (ImGui::SmallButton("Select All"))
				std::fill(addState.selectedSettings.begin(), addState.selectedSettings.end(), true);
			ImGui::SameLine();
			if (ImGui::SmallButton("Select None"))
				std::fill(addState.selectedSettings.begin(), addState.selectedSettings.end(), false);
			ImGui::Spacing();

			auto& featureName = addState.cachedFeatureNames[addState.selectedFeatureIdx];
			if (ImGui::BeginChild("##SettingList", ImVec2(-FLT_MIN, C::Em(C::SCENE_ADD_LIST_HEIGHT_EM)), ImGuiChildFlags_Borders)) {
				for (int i = 0; i < static_cast<int>(addState.cachedSettingKeys.size()); ++i) {
					auto& key = addState.cachedSettingKeys[i];
					bool alreadyAdded = addToAllPeriods
					                        ? [&] { for (int p = 0; p < kPeriodCount; ++p) if (!manager->HasWeatherEntryForPeriod(weatherId, featureName, key, static_cast<Period>(p))) return false; return true; }()
					                        : isTod ? manager->HasWeatherEntryForPeriod(weatherId, featureName, key, period)
					                                : manager->HasWeatherEntry(weatherId, featureName, key);
					if (alreadyAdded) {
						auto _ = Util::DisableGuard(true);
						bool checked = true;
						ImGui::Checkbox(key.c_str(), &checked);
					} else {
						bool sel = addState.selectedSettings[i];
						if (ImGui::Checkbox(key.c_str(), &sel))
							addState.selectedSettings[i] = sel;
					}
				}
			}
			ImGui::EndChild();

			ImGui::Spacing();
			int selectedCount = 0;
			for (size_t i = 0; i < addState.selectedSettings.size(); ++i)
				if (addState.selectedSettings[i])
					++selectedCount;

			{
				auto _ = Util::DisableGuard(selectedCount == 0);
				auto label = std::format("Add ({})", selectedCount);
				if (ImGui::Button(label.c_str(), ImVec2(-FLT_MIN, 0))) {
					for (size_t i = 0; i < addState.cachedSettingKeys.size(); ++i) {
						if (!addState.selectedSettings[i])
							continue;
						auto& key = addState.cachedSettingKeys[i];
						auto currentValue = SceneSettingsManager::GetFeatureSettingValue(featureName, key);
						if (addToAllPeriods) {
							for (int p = 0; p < kPeriodCount; ++p)
								if (!manager->HasWeatherEntryForPeriod(weatherId, featureName, key, static_cast<Period>(p)))
									manager->AddWeatherSetting(weatherId, featureName, key, currentValue, static_cast<Period>(p));
						} else {
							if (isTod) {
								if (!manager->HasWeatherEntryForPeriod(weatherId, featureName, key, period))
									manager->AddWeatherSetting(weatherId, featureName, key, currentValue, period);
							} else {
								if (!manager->HasWeatherEntry(weatherId, featureName, key))
									manager->AddWeatherSetting(weatherId, featureName, key, currentValue);
							}
						}
					}
					addState.dialogOpen = false;
				}
			}
		}

		ImGui::End();
	}

	// --- TOD mode helpers ---

	struct SettingId
	{
		std::string feature;
		std::string key;
		bool operator<(const SettingId& o) const { return (feature < o.feature) || (feature == o.feature && key < o.key); }
	};

	struct SourceGroup
	{
		std::vector<SettingId> order;
		std::map<std::string, std::map<std::string, std::array<size_t, kPeriodCount>>> map;
	};

	static SourceGroup BuildSourceGroup(const std::vector<SceneSettingsManager::SettingEntry>& entries)
	{
		SourceGroup group;
		for (size_t idx = 0; idx < entries.size(); ++idx) {
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
		return group;
	}

	static void DrawTodValueCell(RE::FormID weatherId, size_t entryIndex, PanelState& state)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto& entries = manager->GetWeatherConfig(weatherId).entries;

		if (entryIndex == SIZE_MAX) {
			ImGui::TextDisabled("--");
			return;
		}

		const auto& entry = entries[entryIndex];
		ImGui::PushID(static_cast<int>(entryIndex));

		DrawWeatherValueEditor(weatherId, entryIndex, ImGui::GetContentRegionAvail().x);

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

	static void DrawTodTable(RE::FormID weatherId, PanelState& state)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto& config = manager->GetWeatherConfig(weatherId);
		const auto& entries = config.entries;
		auto& theme = globals::menu->GetSettings().Theme;

		auto group = BuildSourceGroup(entries);
		if (group.order.empty())
			return;

		constexpr int kTotalCols = 1 + kPeriodCount;

		if (!ImGui::BeginTable("##WeatherTOD", kTotalCols,
				ImGuiTableFlags_Borders |
					ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX))
			return;

		ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthFixed, C::Em(C::SCENE_TOD_PARAM_COL_EM));
		for (int i = 0; i < kPeriodCount; ++i)
			ImGui::TableSetupColumn(SceneSettingsManager::kPeriodNames[i],
				ImGuiTableColumnFlags_WidthFixed, C::Em(C::SCENE_TOD_PERIOD_COL_EM));
		ImGui::TableSetupScrollFreeze(0, 1);

		// Collect per-period indices for column flyout controls
		std::array<std::vector<size_t>, kPeriodCount> perPeriod{};
		for (size_t idx = 0; idx < entries.size(); ++idx) {
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
					auto result = SceneSettingsUI::DrawGroupFlyoutControls(allPaused);

					if (result.toggled)
						for (auto idx : indices)
							if (idx < entries.size() && entries[idx].paused == !allPaused)
								manager->TogglePauseWeatherEntry(weatherId, idx);
					if (result.reverted)
						for (auto idx : indices)
							manager->RevertWeatherEntryToDefault(weatherId, idx);
					if (result.deleted) {
						auto sorted = indices;
						std::sort(sorted.begin(), sorted.end(), std::greater<>());
						for (auto idx : sorted)
							manager->RemoveWeatherSetting(weatherId, idx);
					}
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
				auto result = SceneSettingsUI::DrawGroupFlyoutControls(allPaused);

				if (result.toggled)
					for (auto idx : rowIndices)
						if (idx < entries.size() && entries[idx].paused == !allPaused)
							manager->TogglePauseWeatherEntry(weatherId, idx);
				if (result.reverted)
					for (auto idx : rowIndices)
						manager->RevertWeatherEntryToDefault(weatherId, idx);
				if (result.deleted) {
					auto sorted = rowIndices;
					std::sort(sorted.begin(), sorted.end(), std::greater<>());
					for (auto idx : sorted)
						manager->RemoveWeatherSetting(weatherId, idx);
				}
				// Close flyout after any delete action
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

	// --- Main Draw ---

	void Draw(RE::FormID weatherId)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		auto& state = GetState(weatherId);
		const auto& config = manager->GetWeatherConfig(weatherId);
		auto& theme = globals::menu->GetSettings().Theme;
		bool isTod = config.useTimeOfDay;

		// Title + add button
		ImGui::Text("Scene Settings");
		SceneSettingsUI::RightAlignNextButton();
		if (isTod) {
			// "Add to All Periods" button
			if (ImGui::Button("+", ImVec2(C::Em(C::SCENE_ADD_BUTTON_EM), C::Em(C::SCENE_ADD_BUTTON_EM)))) {
				state.allPeriodsAddState.Reset();
				state.allPeriodsAddState.dialogOpen = true;
				state.allPeriodsAddState.cachedFeatureNames = SceneSettingsManager::GetExteriorRelevantFeatureNames();
			}
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("Add setting to all periods");
		} else {
			if (ImGui::Button("+", ImVec2(C::Em(C::SCENE_ADD_BUTTON_EM), C::Em(C::SCENE_ADD_BUTTON_EM)))) {
				state.addState.Reset();
				state.addState.dialogOpen = true;
				state.addState.cachedFeatureNames = SceneSettingsManager::GetExteriorRelevantFeatureNames();
			}
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("Add feature settings");
		}

		// Time of Day checkbox — confirm if entries would be lost
		bool useTod = isTod;
		if (ImGui::Checkbox("Time of Day", &useTod)) {
			if (!config.entries.empty()) {
				state.pendingTodValue = useTod;
				state.todConfirm.Request();
			} else {
				manager->SetWeatherTimeOfDay(weatherId, useTod);
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Enable per-period overrides for this weather");

		// Draw confirmation popup
		if (state.todConfirm.Draw())
			manager->SetWeatherTimeOfDay(weatherId, state.pendingTodValue);

		ImGui::Separator();

		// Draw add dialogs
		DrawWeatherAddDialog(weatherId, state.addState, isTod);
		DrawWeatherAddDialog(weatherId, state.allPeriodsAddState, true, Period::Count, true);
		for (int p = 0; p < kPeriodCount; ++p)
			DrawWeatherAddDialog(weatherId, state.periodAddStates[p], true, static_cast<Period>(p));

		// Empty state
		if (config.entries.empty()) {
			ImGui::Spacing();
			ImGui::TextColored(theme.StatusPalette.Disable,
				"No scene settings for this weather.");
			ImGui::TextColored(theme.StatusPalette.Disable,
				"Use the + button above to add overrides.");
			ImGui::Spacing();
			ImGui::TextWrapped(
				"Settings added here will override feature defaults when this weather is active. "
				"These layer on top of global Time of Day settings.");
			return;
		}

		// Delete All button
		if (ImGui::SmallButton("Delete All"))
			manager->DeleteAllWeatherSettings(weatherId);

		ImGui::Spacing();

		if (isTod) {
			DrawTodTable(weatherId, state);
		} else {
			DrawFlatEntries(weatherId, state);
		}
	}
}
