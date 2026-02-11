#include "InteriorOnlyPanel.h"

#include "../Globals.h"
#include "../Menu.h"
#include "../SceneSettingsManager.h"
#include "EditorWindow.h"

namespace InteriorOnlyPanel
{
	using SceneType = SceneSettingsManager::SceneType;
	using EntrySource = SceneSettingsManager::EntrySource;
	static constexpr auto kSceneType = SceneType::InteriorOnly;

	// Layout constants
	constexpr float FEATURE_DROPDOWN_WIDTH_RATIO = 0.45f;
	constexpr float SETTING_DROPDOWN_WIDTH_RATIO = 0.6f;
	constexpr float VALUE_LABEL_OFFSET_RATIO = 0.5f;
	constexpr float VALUE_INPUT_WIDTH = 240.0f;
	constexpr float ADD_BUTTON_WIDTH = 60.0f;
	constexpr float DELETE_BUTTON_WIDTH = 40.0f;

	// Persistent state for the "Add Setting" workflow
	static int selectedFeatureIdx = -1;
	static int selectedSettingIdx = -1;
	static std::vector<std::string> cachedFeatureNames;
	static std::vector<std::string> cachedSettingKeys;
	static bool showAddUI = false;

	// Confirmation popup for deleting all overwrites
	static Util::ConfirmationPopup deleteAllOverwritesPopup = []() {
		Util::ConfirmationPopup p;
		p.title = "Delete All Overwrites?";
		p.message = "Are you sure you want to delete all interior-only overwrite files?\nThis cannot be undone.";
		p.confirmLabel = "Delete All";
		p.cancelLabel = "Cancel";
		return p;
	}();

	// Confirmation popup for deleting a single overwrite
	static Util::ConfirmationPopup deleteSingleOverwritePopup = []() {
		Util::ConfirmationPopup p;
		p.title = "Delete Overwrite File?";
		p.confirmLabel = "Delete";
		p.cancelLabel = "Cancel";
		return p;
	}();
	static size_t pendingDeleteIndex = SIZE_MAX;

	// Confirmation popup for deleting all user settings
	static Util::ConfirmationPopup deleteAllUserPopup = []() {
		Util::ConfirmationPopup p;
		p.title = "Delete All User Settings?";
		p.message = "Are you sure you want to remove all user-added interior-only settings?";
		p.confirmLabel = "Delete All";
		p.cancelLabel = "Cancel";
		return p;
	}();

	void DrawAddSettingUI()
	{
		auto* manager = SceneSettingsManager::GetSingleton();

		ImGui::Spacing();

		// Feature dropdown
		if (cachedFeatureNames.empty())
			cachedFeatureNames = SceneSettingsManager::GetLoadedFeatureNames();

		const char* featurePreview = (selectedFeatureIdx >= 0 && selectedFeatureIdx < static_cast<int>(cachedFeatureNames.size()))
		                                 ? cachedFeatureNames[selectedFeatureIdx].c_str()
		                                 : "Select Feature...";

		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * FEATURE_DROPDOWN_WIDTH_RATIO);
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

			const char* settingPreview = (selectedSettingIdx >= 0 && selectedSettingIdx < static_cast<int>(cachedSettingKeys.size()))
			                                 ? cachedSettingKeys[selectedSettingIdx].c_str()
			                                 : "Select Setting...";

			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * SETTING_DROPDOWN_WIDTH_RATIO);
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
			if (ImGui::Button("Add")) {
				auto& featureName = cachedFeatureNames[selectedFeatureIdx];
				auto& settingKey = cachedSettingKeys[selectedSettingIdx];
				auto currentValue = SceneSettingsManager::GetFeatureSettingValue(featureName, settingKey);

				manager->AddSetting(kSceneType, featureName, settingKey, currentValue);
				selectedSettingIdx = -1;
				showAddUI = false;
				return;
			}
		}

		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
			showAddUI = false;
			selectedFeatureIdx = -1;
			selectedSettingIdx = -1;
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
		ImGui::SameLine(availWidth * VALUE_LABEL_OFFSET_RATIO);

		bool isOverwrite = entry.source == EntrySource::Overwrite;
		auto type = SceneSettingsManager::DetectSettingType(entry.value);

		// Overwrites are read-only; user entries overridden by an active overwrite are also disabled
		bool readOnly = isOverwrite ||
			manager->HasActiveOverwrite(kSceneType, entry.featureShortName, entry.settingKey);

		if (readOnly)
			ImGui::BeginDisabled();

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
				ImGui::SetNextItemWidth(VALUE_INPUT_WIDTH);
				if (ImGui::InputFloat("##val", &val, 0.01f, 0.1f, "%.3f"))
					manager->UpdateEntryValue(kSceneType, index, val, true);
				if (ImGui::IsItemDeactivatedAfterEdit())
					manager->SaveUserSettings(kSceneType);
			}
			break;
		case SceneSettingsManager::SettingType::Integer:
			{
				int val = entry.value.get<int>();
				ImGui::SetNextItemWidth(VALUE_INPUT_WIDTH);
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
			if (ImGui::Button("X", ImVec2(DELETE_BUTTON_WIDTH, 0))) {
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
		}
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(entry.source == EntrySource::Overwrite
				? "Delete overwrite file from disk"
				: "Remove this setting");

		ImGui::PopID();
	}

	void Draw()
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto& entries = manager->GetEntries(kSceneType);
		auto& theme = globals::menu->GetSettings().Theme;

		// Header with + button
		ImGui::Text("Interior Only Settings");
		ImGui::SameLine(ImGui::GetContentRegionAvail().x - ADD_BUTTON_WIDTH);
		if (ImGui::Button("+", ImVec2(ADD_BUTTON_WIDTH, 0))) {
			showAddUI = !showAddUI;
			if (showAddUI) {
				cachedFeatureNames = SceneSettingsManager::GetLoadedFeatureNames();
				selectedFeatureIdx = -1;
				selectedSettingIdx = -1;
			}
		}
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Add a new interior-only setting override");

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

		// Add setting UI (when + is clicked)
		if (showAddUI)
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
				"No interior-only settings configured.");
			ImGui::TextColored(theme.StatusPalette.Disable,
				"Click + to add settings that will only apply in interiors.");
			ImGui::Spacing();
			ImGui::TextWrapped(
				"Settings added here will override feature defaults when you enter an interior cell. "
				"Values revert automatically when you exit.");
			return;
		}

		// --- Overwrite Files Section ---
		if (!overwriteIndices.empty()) {
			ImGui::Spacing();
			ImGui::TextColored(theme.StatusPalette.InfoColor, "Overwrite Files");
			ImGui::SameLine();

			bool allPaused = manager->AreAllOverwritesPaused(kSceneType);
			if (ImGui::SmallButton(allPaused ? "Unpause All" : "Pause All"))
				manager->SetAllOverwritesPaused(kSceneType, !allPaused);

			ImGui::SameLine();
			if (ImGui::SmallButton("Delete All"))
				deleteAllOverwritesPopup.Request();

			ImGui::Separator();

			for (auto i : overwriteIndices)
				DrawSettingEntry(i);
		}

		// --- User Settings Section (header only shown when overwrites also present) ---
		if (!userIndices.empty()) {
			if (!overwriteIndices.empty()) {
				ImGui::Spacing();
				ImGui::TextColored(theme.FeatureHeading.ColorDefault, "User Settings");
				ImGui::SameLine();
			}

			bool allUserPaused = manager->AreAllUserPaused(kSceneType);
			if (ImGui::SmallButton(allUserPaused ? "Unpause All##user" : "Pause All##user"))
				manager->SetAllUserPaused(kSceneType, !allUserPaused);

			ImGui::SameLine();
			if (ImGui::SmallButton("Delete All##user"))
				deleteAllUserPopup.Request();

			if (!overwriteIndices.empty())
				ImGui::Separator();

			for (auto i : userIndices)
				DrawSettingEntry(i);
		}
	}
}
