#include "InteriorOnlyPanel.h"

#include "../Globals.h"
#include "../I18n/I18n.h"
#include "../Menu.h"
#include "../Menu/ThemeManager.h"
#include "../SceneSettingsManager.h"
#include "../Utils/UI.h"
#include "EditorWindow.h"

#define I18N_KEY_PREFIX "cs_editor."

namespace InteriorOnlyPanel
{
	using SceneType = SceneSettingsManager::SceneType;
	using EntrySource = SceneSettingsManager::EntrySource;
	static constexpr auto kSceneType = SceneType::InteriorOnly;

	// Layout constants from centralized theme
	using C = ThemeManager::Constants;

	// Persistent state for the "Add Setting" workflow
	static int selectedFeatureIdx = -1;
	static int selectedSettingIdx = -1;
	static std::vector<std::string> cachedFeatureNames;
	static std::vector<std::string> cachedSettingKeys;

	// Confirmation popups
	static Util::ConfirmationPopup deleteAllOverwritesPopup{
		"Delete All Overwrites?",
		"Are you sure you want to delete all interior-only overwrite files?\nThis cannot be undone.",
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
		"Are you sure you want to remove all user-added interior-only settings?",
		"Delete All"
	};

	void DrawAddSettingUI()
	{
		auto* manager = SceneSettingsManager::GetSingleton();

		ImGui::Spacing();

		// Feature dropdown
		if (cachedFeatureNames.empty())
			cachedFeatureNames = SceneSettingsManager::GetInteriorRelevantFeatureNames();

		const char* featurePreview = (selectedFeatureIdx >= 0 && selectedFeatureIdx < static_cast<int>(cachedFeatureNames.size())) ? cachedFeatureNames[selectedFeatureIdx].c_str() : T(TKEY("select_feature"), "Select Feature...");

		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * C::SCENE_FEATURE_DROPDOWN_RATIO);
		if (ImGui::BeginCombo("##FeatureSelect", featurePreview)) {
			for (int i = 0; i < static_cast<int>(cachedFeatureNames.size()); ++i) {
				bool selected = (i == selectedFeatureIdx);
				if (ImGui::Selectable(cachedFeatureNames[i].c_str(), selected)) {
					selectedFeatureIdx = i;
					selectedSettingIdx = -1;
					cachedSettingKeys = SceneSettingsManager::GetFeatureSettingKeys(cachedFeatureNames[i]);
				}
				if (selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		ImGui::SameLine();

		// Setting dropdown (only if feature is selected)
		{
			auto _ = Util::DisableGuard(selectedFeatureIdx < 0);

			const char* settingPreview = (selectedSettingIdx >= 0 && selectedSettingIdx < static_cast<int>(cachedSettingKeys.size())) ? cachedSettingKeys[selectedSettingIdx].c_str() : T(TKEY("select_setting"), "Select Setting...");

			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * C::SCENE_SETTING_DROPDOWN_RATIO);
			if (ImGui::BeginCombo("##SettingSelect", settingPreview)) {
				for (int i = 0; i < static_cast<int>(cachedSettingKeys.size()); ++i) {
					bool selected = (i == selectedSettingIdx);
					bool alreadyAdded = selectedFeatureIdx >= 0 &&
					                    manager->HasEntryFromSource(kSceneType, cachedFeatureNames[selectedFeatureIdx], cachedSettingKeys[i], EntrySource::User);
					if (alreadyAdded) {
						ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
						ImGui::Selectable(cachedSettingKeys[i].c_str(), false, ImGuiSelectableFlags_Disabled);
						ImGui::PopStyleColor();
					} else {
						if (ImGui::Selectable(cachedSettingKeys[i].c_str(), selected))
							selectedSettingIdx = i;
					}

					if (selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
		}

		ImGui::SameLine();

		// Add button
		bool canAdd = selectedFeatureIdx >= 0 && selectedSettingIdx >= 0;
		{
			auto _ = Util::DisableGuard(!canAdd);
			if (ImGui::Button(T(TKEY("add"), "Add"))) {
				auto& featureName = cachedFeatureNames[selectedFeatureIdx];
				auto& settingKey = cachedSettingKeys[selectedSettingIdx];
				auto currentValue = SceneSettingsManager::GetFeatureSettingValue(featureName, settingKey);

				manager->AddSetting(kSceneType, featureName, settingKey, currentValue);
				selectedSettingIdx = -1;
				return;
			}
		}
	}

	void DrawSettingEntry(size_t index)
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

		// Value display/editor on same line (right-aligned)
		ImGui::SameLine(availWidth * C::SCENE_VALUE_LABEL_OFFSET_RATIO);

		bool isOverwrite = entry.source == EntrySource::Overwrite;
		auto type = SceneSettingsManager::DetectSettingType(entry.value);

		// Overwrites are read-only; user entries overridden by an active overwrite are also disabled
		bool readOnly = isOverwrite ||
		                manager->HasActiveOverwrite(kSceneType, entry.featureShortName, entry.settingKey);

		if (readOnly)
			ImGui::BeginDisabled();

		const float scale = Util::GetUIScale();

		switch (type) {
		case SceneSettingsManager::SettingType::Boolean:
			{
				bool val = entry.value.is_boolean() ? entry.value.get<bool>() : (entry.value.get<int>() != 0);
				if (ImGui::Checkbox("##val", &val)) {
					// Preserve original JSON type (integer for GPU constant buffer settings, boolean otherwise)
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
				ImGui::SetNextItemWidth(C::SCENE_VALUE_INPUT_WIDTH * scale);
				if (ImGui::InputFloat("##val", &val, 0.01f, 0.1f, "%.3f"))
					manager->UpdateEntryValue(kSceneType, index, val, true);
				if (ImGui::IsItemDeactivatedAfterEdit())
					manager->SaveUserSettings(kSceneType);
			}
			break;
		case SceneSettingsManager::SettingType::Integer:
			{
				int val = entry.value.get<int>();
				ImGui::SetNextItemWidth(C::SCENE_VALUE_INPUT_WIDTH * scale);
				if (ImGui::InputInt("##val", &val))
					manager->UpdateEntryValue(kSceneType, index, val, true);
				if (ImGui::IsItemDeactivatedAfterEdit())
					manager->SaveUserSettings(kSceneType);
			}
			break;
		default:
			ImGui::TextDisabled("%s", T(TKEY("unsupported_type"), "(unsupported type)"));
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
			ImGui::Text("%s", entry.paused ? T(TKEY("paused_click_resume"), "Paused - click to resume") : T(TKEY("active_click_pause"), "Active - click to pause"));

		// Delete button
		ImGui::SameLine();
		if (Util::ErrorButton("X", ImVec2(C::SCENE_DELETE_BUTTON_WIDTH * scale, 0))) {
			if (entry.source == EntrySource::Overwrite) {
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
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", entry.source == EntrySource::Overwrite ? T(TKEY("delete_overwrite_file"), "Delete overwrite file from disk") : T(TKEY("remove_setting"), "Remove this setting"));

		ImGui::PopID();
	}

	void Draw()
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto& entries = manager->GetEntries(kSceneType);
		auto& theme = globals::menu->GetSettings().Theme;

		// Header
		ImGui::Text("%s", T(TKEY("interior_only_settings"), "Interior Only Settings"));

		ImGui::Separator();

		// Draw confirmation popups
		if (deleteAllOverwritesPopup.Draw())
			manager->DeleteAllOverwrites(kSceneType);

		if (deleteSingleOverwritePopup.Draw()) {
			if (pendingDeleteIndex < entries.size())
				manager->RemoveSetting(kSceneType, pendingDeleteIndex);
			pendingDeleteIndex = SIZE_MAX;
		}

		if (deleteAllUserPopup.Draw())
			manager->DeleteAllUserSettings(kSceneType);

		// Add setting UI (always visible)
		DrawAddSettingUI();

		// Collect indices by source
		std::vector<size_t> overwriteIndices, userIndices;
		for (size_t i = 0; i < entries.size(); ++i) {
			if (entries[i].source == EntrySource::Overwrite)
				overwriteIndices.push_back(i);
			else
				userIndices.push_back(i);
		}

		// Empty state
		if (entries.empty()) {
			ImGui::Spacing();
			ImGui::TextColored(theme.StatusPalette.Disable,
				"%s", T(TKEY("no_interior_settings"), "No interior-only settings configured."));
			ImGui::TextColored(theme.StatusPalette.Disable,
				"%s", T(TKEY("click_plus_add"), "Click + to add settings that will only apply in interiors."));
			ImGui::Spacing();
			ImGui::TextWrapped("%s",
				T(TKEY("interior_settings_override"),
					"Settings added here will override feature defaults when you enter an interior cell. "
					"Values revert automatically when you exit."));
			return;
		}

		// --- Overwrite Files Section ---
		if (!overwriteIndices.empty()) {
			ImGui::Spacing();
			ImGui::TextColored(theme.StatusPalette.InfoColor, "%s", T(TKEY("overwrite_files"), "Overwrite Files"));
			ImGui::SameLine();

			bool allPaused = manager->AreAllOverwritesPaused(kSceneType);
			if (ImGui::SmallButton(allPaused ? T(TKEY("unpause_all"), "Unpause All") : T(TKEY("pause_all"), "Pause All")))
				manager->SetAllOverwritesPaused(kSceneType, !allPaused);

			ImGui::SameLine();
			if (ImGui::SmallButton(T(TKEY("delete_all"), "Delete All")))
				deleteAllOverwritesPopup.Request();

			ImGui::Separator();

			for (auto i : overwriteIndices)
				DrawSettingEntry(i);
		}

		// --- User Settings Section (header only shown when overwrites also present) ---
		if (!userIndices.empty()) {
			if (!overwriteIndices.empty()) {
				ImGui::Spacing();
				ImGui::TextColored(theme.FeatureHeading.ColorDefault, "%s", T(TKEY("user_settings"), "User Settings"));
				ImGui::SameLine();
			}

			bool allUserPaused = manager->AreAllUserPaused(kSceneType);
			auto pauseAllLabel = std::format("{}##user", allUserPaused ? T(TKEY("unpause_all"), "Unpause All") : T(TKEY("pause_all"), "Pause All"));
			if (ImGui::SmallButton(pauseAllLabel.c_str()))
				manager->SetAllUserPaused(kSceneType, !allUserPaused);

			ImGui::SameLine();
			auto deleteAllLabel = std::format("{}##user", T(TKEY("delete_all"), "Delete All"));
			if (ImGui::SmallButton(deleteAllLabel.c_str()))
				deleteAllUserPopup.Request();

			if (!overwriteIndices.empty())
				ImGui::Separator();

			for (auto i : userIndices) {
				// Re-check bounds: a prior inline deletion may have shrunk the entries vector
				if (i >= manager->GetEntries(kSceneType).size())
					break;
				DrawSettingEntry(i);
			}
		}
	}
}

#undef I18N_KEY_PREFIX
