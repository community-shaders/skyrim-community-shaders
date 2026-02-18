#include "TimeOfDayPanel.h"

#include "../Globals.h"
#include "../Menu.h"
#include "../Menu/ThemeManager.h"
#include "../SceneSettingsManager.h"
#include "EditorWindow.h"

namespace TimeOfDayPanel
{
	using SceneType = SceneSettingsManager::SceneType;
	using EntrySource = SceneSettingsManager::EntrySource;
	using Period = SceneSettingsManager::TimeOfDayPeriod;
	static constexpr auto kSceneType = SceneType::TimeOfDay;
	static constexpr int kPeriodCount = static_cast<int>(Period::Count);

	// Layout constants from centralized theme
	using C = ThemeManager::Constants;

	// Per-period persistent state for the "Add Setting" workflow
	struct PeriodUIState
	{
		int selectedFeatureIdx = -1;
		int selectedSettingIdx = -1;
		std::vector<std::string> cachedFeatureNames;
		std::vector<std::string> cachedSettingKeys;
	};
	static PeriodUIState periodState[kPeriodCount];

	// Confirmation popups (shared across tabs)
	static Util::ConfirmationPopup deleteAllOverwritesPopup{
		"Delete All Overwrites?",
		"Are you sure you want to delete all time-of-day overwrite files?\nThis cannot be undone.",
		"Delete All"
	};

	static Util::ConfirmationPopup deleteSingleOverwritePopup{
		"Delete Overwrite File?",
		"",
		"Delete"
	};
	static size_t pendingDeleteIndex = SIZE_MAX;

	static Util::ConfirmationPopup deleteAllUserPopup{
		"Delete All User Settings?",
		"Are you sure you want to remove all user-added time-of-day settings?",
		"Delete All"
	};

	// --- Helpers to filter entries by period ---

	static void CollectPeriodIndices(Period period, const std::vector<SceneSettingsManager::SettingEntry>& entries,
		std::vector<size_t>& overwriteOut, std::vector<size_t>& userOut)
	{
		for (size_t i = 0; i < entries.size(); ++i) {
			if (entries[i].period != period)
				continue;
			if (entries[i].source == EntrySource::Overwrite)
				overwriteOut.push_back(i);
			else
				userOut.push_back(i);
		}
	}

	static void DrawAddSettingUI(Period period)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		auto& state = periodState[static_cast<int>(period)];

		ImGui::Spacing();

		// Feature dropdown
		if (state.cachedFeatureNames.empty())
			state.cachedFeatureNames = SceneSettingsManager::GetExteriorRelevantFeatureNames();

		const char* featurePreview = (state.selectedFeatureIdx >= 0 &&
										 state.selectedFeatureIdx < static_cast<int>(state.cachedFeatureNames.size())) ?
		                                 state.cachedFeatureNames[state.selectedFeatureIdx].c_str() :
		                                 "Select Feature...";

		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * C::SCENE_FEATURE_DROPDOWN_RATIO);
		if (ImGui::BeginCombo("##FeatureSelect", featurePreview)) {
			for (int i = 0; i < static_cast<int>(state.cachedFeatureNames.size()); ++i) {
				bool selected = (i == state.selectedFeatureIdx);
				if (ImGui::Selectable(state.cachedFeatureNames[i].c_str(), selected)) {
					state.selectedFeatureIdx = i;
					state.selectedSettingIdx = -1;
					state.cachedSettingKeys = SceneSettingsManager::GetFeatureSettingKeys(state.cachedFeatureNames[i]);
				}
				if (selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		ImGui::SameLine();

		// Setting dropdown
		{
			auto _ = Util::DisableGuard(state.selectedFeatureIdx < 0);

			const char* settingPreview = (state.selectedSettingIdx >= 0 &&
											 state.selectedSettingIdx < static_cast<int>(state.cachedSettingKeys.size())) ?
			                                 state.cachedSettingKeys[state.selectedSettingIdx].c_str() :
			                                 "Select Setting...";

			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * C::SCENE_SETTING_DROPDOWN_RATIO);
			if (ImGui::BeginCombo("##SettingSelect", settingPreview)) {
				for (int i = 0; i < static_cast<int>(state.cachedSettingKeys.size()); ++i) {
					bool selected = (i == state.selectedSettingIdx);
					bool alreadyAdded = state.selectedFeatureIdx >= 0 &&
					                    manager->HasEntryForPeriod(
											state.cachedFeatureNames[state.selectedFeatureIdx],
											state.cachedSettingKeys[i], period, EntrySource::User);
					if (alreadyAdded) {
						ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
						ImGui::Selectable(state.cachedSettingKeys[i].c_str(), false, ImGuiSelectableFlags_Disabled);
						ImGui::PopStyleColor();
					} else {
						if (ImGui::Selectable(state.cachedSettingKeys[i].c_str(), selected))
							state.selectedSettingIdx = i;
					}
					if (selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
		}

		ImGui::SameLine();

		// Add button
		bool canAdd = state.selectedFeatureIdx >= 0 && state.selectedSettingIdx >= 0;
		{
			auto _ = Util::DisableGuard(!canAdd);
			if (ImGui::Button("Add")) {
				auto& featureName = state.cachedFeatureNames[state.selectedFeatureIdx];
				auto& settingKey = state.cachedSettingKeys[state.selectedSettingIdx];
				auto currentValue = SceneSettingsManager::GetFeatureSettingValue(featureName, settingKey);
				manager->AddSetting(kSceneType, featureName, settingKey, currentValue, period);
				state.selectedSettingIdx = -1;
				return;
			}
		}
	}

	static void DrawSettingEntry(size_t index)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto& entries = manager->GetEntries(kSceneType);
		if (index >= entries.size())
			return;

		const auto& entry = entries[index];

		ImGui::PushID(static_cast<int>(index));

		// Feature.Setting label
		float availWidth = ImGui::GetContentRegionAvail().x;
		ImGui::Text("%s.%s", entry.featureShortName.c_str(), entry.settingKey.c_str());

		// Value editor (right-aligned)
		ImGui::SameLine(availWidth * C::SCENE_VALUE_LABEL_OFFSET_RATIO);

		bool isOverwrite = entry.source == EntrySource::Overwrite;
		auto type = SceneSettingsManager::DetectSettingType(entry.value);
		bool readOnly = isOverwrite;

		if (readOnly)
			ImGui::BeginDisabled();

		switch (type) {
		case SceneSettingsManager::SettingType::Boolean:
			{
				bool val = entry.value.is_boolean() ? entry.value.get<bool>() : (entry.value.get<int>() != 0);
				if (ImGui::Checkbox("##val", &val)) {
					if (entry.value.is_boolean())
						manager->UpdateEntryValue(kSceneType, index, val);
					else
						manager->UpdateEntryValue(kSceneType, index, val ? 1 : 0);
				}
			}
			break;
		case SceneSettingsManager::SettingType::Float:
			{
				float val = entry.value.get<float>();
				ImGui::SetNextItemWidth(C::SCENE_VALUE_INPUT_WIDTH);
				if (ImGui::InputFloat("##val", &val, 0.01f, 0.1f, "%.3f"))
					manager->UpdateEntryValue(kSceneType, index, val, true);
				if (ImGui::IsItemDeactivatedAfterEdit())
					manager->SaveUserSettings(kSceneType);
			}
			break;
		case SceneSettingsManager::SettingType::Integer:
			{
				int val = entry.value.get<int>();
				ImGui::SetNextItemWidth(C::SCENE_VALUE_INPUT_WIDTH);
				if (ImGui::InputInt("##val", &val))
					manager->UpdateEntryValue(kSceneType, index, val, true);
				if (ImGui::IsItemDeactivatedAfterEdit())
					manager->SaveUserSettings(kSceneType);
			}
			break;
		default:
			ImGui::TextDisabled("(unsupported type)");
			break;
		}

		if (readOnly)
			ImGui::EndDisabled();

		// Active/Pause toggle
		ImGui::SameLine();
		bool active = !entry.paused;
		if (Util::FeatureToggle("##active", &active))
			manager->TogglePauseEntry(kSceneType, index);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(entry.paused ? "Paused - click to resume" : "Active - click to pause");

		// Delete button
		ImGui::SameLine();
		{
			auto styledButton = Util::ErrorButtonStyle();
			if (ImGui::Button("X", ImVec2(C::SCENE_DELETE_BUTTON_WIDTH, 0))) {
				if (isOverwrite) {
					pendingDeleteIndex = index;
					deleteSingleOverwritePopup.message = std::format(
						"Delete overwrite file '{}'?\nThis will permanently remove the file from disk.",
						entry.sourceFilename);
					deleteSingleOverwritePopup.Request();
				} else {
					manager->RemoveSetting(kSceneType, index);
					ImGui::PopID();
					return;
				}
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(isOverwrite ? "Delete overwrite file from disk" : "Remove this setting");

		ImGui::PopID();
	}

	static void DrawPeriodTab(Period period)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto& entries = manager->GetEntries(kSceneType);
		auto& theme = globals::menu->GetSettings().Theme;

		// Draw confirmation popups
		if (deleteSingleOverwritePopup.Draw()) {
			if (pendingDeleteIndex < entries.size())
				manager->RemoveSetting(kSceneType, pendingDeleteIndex);
			pendingDeleteIndex = SIZE_MAX;
		}

		DrawAddSettingUI(period);

		// Collect indices for this period
		std::vector<size_t> overwriteIndices, userIndices;
		CollectPeriodIndices(period, entries, overwriteIndices, userIndices);

		if (overwriteIndices.empty() && userIndices.empty()) {
			ImGui::Spacing();
			ImGui::TextColored(theme.StatusPalette.Disable,
				"No settings for %s.", SceneSettingsManager::GetPeriodName(period));
			return;
		}

		// Overwrite section
		if (!overwriteIndices.empty()) {
			ImGui::Spacing();
			ImGui::TextColored(theme.StatusPalette.InfoColor, "Overwrite Files");
			ImGui::Separator();
			for (auto i : overwriteIndices)
				DrawSettingEntry(i);
		}

		// User section
		if (!userIndices.empty()) {
			if (!overwriteIndices.empty()) {
				ImGui::Spacing();
				ImGui::TextColored(theme.FeatureHeading.ColorDefault, "User Settings");
				ImGui::Separator();
			}
			for (auto i : userIndices)
				DrawSettingEntry(i);
		}
	}

	void Draw()
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto& entries = manager->GetEntries(kSceneType);
		auto& theme = globals::menu->GetSettings().Theme;

		ImGui::Text("Time of Day Settings");
		ImGui::SameLine();
		ImGui::TextDisabled("(Exterior Only)");

		// Show current period indicator
		auto dominant = manager->GetDominantPeriod();
		ImGui::SameLine();
		ImGui::TextColored(theme.StatusPalette.InfoColor, "[%s %.1fh]",
			SceneSettingsManager::GetPeriodName(dominant),
			SceneSettingsManager::GetCurrentGameHour());

		ImGui::Separator();

		// Global confirmation popups
		if (deleteAllOverwritesPopup.Draw())
			manager->DeleteAllOverwrites(kSceneType);
		if (deleteAllUserPopup.Draw())
			manager->DeleteAllUserSettings(kSceneType);

		// Global controls
		if (!entries.empty()) {
			if (manager->HasOverwriteEntries(kSceneType)) {
				bool allPaused = manager->AreAllOverwritesPaused(kSceneType);
				if (ImGui::SmallButton(allPaused ? "Unpause All Overwrite" : "Pause All Overwrite"))
					manager->SetAllOverwritesPaused(kSceneType, !allPaused);
				ImGui::SameLine();
				if (ImGui::SmallButton("Delete All Overwrite"))
					deleteAllOverwritesPopup.Request();
				ImGui::SameLine();
			}

			bool allUserPaused = manager->AreAllUserPaused(kSceneType);
			if (ImGui::SmallButton(allUserPaused ? "Unpause All User" : "Pause All User"))
				manager->SetAllUserPaused(kSceneType, !allUserPaused);
			ImGui::SameLine();
			if (ImGui::SmallButton("Delete All User"))
				deleteAllUserPopup.Request();
		}

		// Period tabs
		if (ImGui::BeginTabBar("##TODPeriods")) {
			for (int i = 0; i < kPeriodCount; ++i) {
				auto period = static_cast<Period>(i);
				if (ImGui::BeginTabItem(SceneSettingsManager::GetPeriodName(period))) {
					DrawPeriodTab(period);
					ImGui::EndTabItem();
				}
			}
			ImGui::EndTabBar();
		}
	}
}
