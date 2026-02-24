#include "SceneSettingsUI.h"

#include <cmath>

#include "../Globals.h"
#include "../Menu.h"
#include "../Menu/ThemeManager.h"
#include "../SceneSettingsManager.h"

namespace SceneSettingsUI
{
	using C = ThemeManager::Constants;

	// --- Feature name resolution by scene type ---

	static std::vector<std::string> GetFeatureNamesForType(SceneType type)
	{
		return (type == SceneType::InteriorOnly)
		           ? SceneSettingsManager::GetInteriorRelevantFeatureNames()
		           : SceneSettingsManager::GetExteriorRelevantFeatureNames();
	}

	// --- Duplicate checking by scene type ---

	static bool IsAlreadyAdded(SceneType type, const std::string& feature, const std::string& key, Period period)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		return (type == SceneType::TimeOfDay)
		           ? manager->HasEntryForPeriod(feature, key, period, EntrySource::User)
		           : manager->HasEntryFromSource(type, feature, key, EntrySource::User);
	}

	// --- Shared Drawing ---

	void DrawAddSettingUI(SceneType type, AddSettingState& state, Period period)
	{
		auto* manager = SceneSettingsManager::GetSingleton();

		ImGui::Spacing();

		// Feature dropdown
		if (state.cachedFeatureNames.empty())
			state.cachedFeatureNames = GetFeatureNamesForType(type);

		const char* featurePreview = (state.selectedFeatureIdx >= 0 &&
										 state.selectedFeatureIdx < static_cast<int>(state.cachedFeatureNames.size()))
		                                 ? state.cachedFeatureNames[state.selectedFeatureIdx].c_str()
		                                 : "Select Feature...";

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
											 state.selectedSettingIdx < static_cast<int>(state.cachedSettingKeys.size()))
			                                 ? state.cachedSettingKeys[state.selectedSettingIdx].c_str()
			                                 : "Select Setting...";

			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * C::SCENE_SETTING_DROPDOWN_RATIO);
			if (ImGui::BeginCombo("##SettingSelect", settingPreview)) {
				for (int i = 0; i < static_cast<int>(state.cachedSettingKeys.size()); ++i) {
					bool selected = (i == state.selectedSettingIdx);
					bool alreadyAdded = state.selectedFeatureIdx >= 0 &&
					                    IsAlreadyAdded(type, state.cachedFeatureNames[state.selectedFeatureIdx],
											state.cachedSettingKeys[i], period);
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
				manager->AddSetting(type, featureName, settingKey, currentValue, period);
				state.selectedSettingIdx = -1;
				return;
			}
		}
	}

	bool DrawSettingEntry(SceneType type, size_t index, PopupState& popups)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto& entries = manager->GetEntries(type);
		if (index >= entries.size())
			return false;

		const auto& entry = entries[index];

		ImGui::PushID(static_cast<int>(index));

		// Feature.Setting label
		float availWidth = ImGui::GetContentRegionAvail().x;
		ImGui::Text("%s.%s", entry.featureShortName.c_str(), entry.settingKey.c_str());

		// Value editor (right-aligned)
		ImGui::SameLine(availWidth * C::SCENE_VALUE_LABEL_OFFSET_RATIO);

		bool isOverwrite = entry.source == EntrySource::Overwrite;
		auto settingType = SceneSettingsManager::DetectSettingType(entry.value);

		// Overwrites are always read-only; for non-TOD types, user entries overridden
		// by an active overwrite are also disabled.
		bool readOnly = isOverwrite ||
		                (type != SceneType::TimeOfDay &&
							manager->HasActiveOverwrite(type, entry.featureShortName, entry.settingKey));

		if (readOnly)
			ImGui::BeginDisabled();

		switch (settingType) {
		case SceneSettingsManager::SettingType::Boolean:
			{
				bool val = entry.value.is_boolean() ? entry.value.get<bool>() : (entry.value.get<int>() != 0);
				if (ImGui::Checkbox("##val", &val)) {
					if (entry.value.is_boolean())
						manager->UpdateEntryValue(type, index, val);
					else
						manager->UpdateEntryValue(type, index, val ? 1 : 0);
				}
			}
			break;
		case SceneSettingsManager::SettingType::Float:
			{
				float val = entry.value.is_number() ? entry.value.get<float>() : 0.0f;
				if (!std::isfinite(val))
					val = 0.0f;
				ImGui::SetNextItemWidth(C::SCENE_VALUE_INPUT_WIDTH);
				if (ImGui::InputFloat("##val", &val, 0.01f, 0.1f, "%.3f")) {
					if (std::isfinite(val))
						manager->UpdateEntryValue(type, index, val, true);
				}
				if (ImGui::IsItemDeactivatedAfterEdit())
					manager->SaveUserSettings(type);
			}
			break;
		case SceneSettingsManager::SettingType::Integer:
			{
				int val = entry.value.get<int>();
				ImGui::SetNextItemWidth(C::SCENE_VALUE_INPUT_WIDTH);
				if (ImGui::InputInt("##val", &val))
					manager->UpdateEntryValue(type, index, val, true);
				if (ImGui::IsItemDeactivatedAfterEdit())
					manager->SaveUserSettings(type);
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
			manager->TogglePauseEntry(type, index);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(entry.paused ? "Paused - click to resume" : "Active - click to pause");

		// Delete button
		ImGui::SameLine();
		{
			auto styledButton = Util::ErrorButtonStyle();
			if (ImGui::Button("X", ImVec2(C::SCENE_DELETE_BUTTON_WIDTH, 0))) {
				if (isOverwrite) {
					popups.pendingDeleteIndex = index;
					popups.deleteSingleOverwrite.message = std::format(
						"Delete overwrite file '{}'?\nThis will permanently remove the file from disk.",
						entry.sourceFilename);
					popups.deleteSingleOverwrite.Request();
				} else {
					manager->RemoveSetting(type, index);
					ImGui::PopID();
					return true;
				}
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(isOverwrite ? "Delete overwrite file from disk" : "Remove this setting");

		ImGui::PopID();
		return false;
	}

	void DrawPopups(SceneType type, PopupState& popups)
	{
		auto* manager = SceneSettingsManager::GetSingleton();

		if (popups.deleteAllOverwrites.Draw())
			manager->DeleteAllOverwrites(type);

		if (popups.deleteSingleOverwrite.Draw()) {
			if (popups.pendingDeleteIndex < manager->GetEntries(type).size())
				manager->RemoveSetting(type, popups.pendingDeleteIndex);
			popups.pendingDeleteIndex = SIZE_MAX;
		}

		if (popups.deleteAllUser.Draw())
			manager->DeleteAllUserSettings(type);
	}

	void DrawEntrySections(SceneType type, PopupState& popups,
		const std::vector<size_t>& overwriteIndices,
		const std::vector<size_t>& userIndices)
	{
		auto& theme = globals::menu->GetSettings().Theme;

		if (!overwriteIndices.empty()) {
			ImGui::Spacing();
			ImGui::TextColored(theme.StatusPalette.InfoColor, "Overwrite Files");
			ImGui::Separator();
			for (auto i : overwriteIndices)
				DrawSettingEntry(type, i, popups);
		}

		if (!userIndices.empty()) {
			if (!overwriteIndices.empty()) {
				ImGui::Spacing();
				ImGui::TextColored(theme.FeatureHeading.ColorDefault, "User Settings");
				ImGui::Separator();
			}
			for (auto i : userIndices) {
				if (i >= SceneSettingsManager::GetSingleton()->GetEntries(type).size())
					break;
				DrawSettingEntry(type, i, popups);
			}
		}
	}

	bool DrawCategoryPanel(const char* category, const std::string& selected, void (*drawFn)())
	{
		if (selected != category)
			return false;
		drawFn();
		ImGui::EndChild();
		ImGui::EndTable();
		ImGui::End();
		return true;
	}
}
