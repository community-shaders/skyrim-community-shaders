#include "InteriorOnlyPanel.h"

#include "../Globals.h"
#include "../Menu.h"
#include "../SceneSettingsManager.h"
#include "SceneSettingsUI.h"

namespace InteriorOnlyPanel
{
	using SceneType = SceneSettingsManager::SceneType;
	using EntrySource = SceneSettingsManager::EntrySource;
	static constexpr auto kSceneType = SceneType::InteriorOnly;

	// Shared UI state
	static SceneSettingsUI::AddSettingState addState;
	static SceneSettingsUI::PopupState popups{
		"Are you sure you want to delete all interior-only overwrite files?\nThis cannot be undone.",
		"Are you sure you want to remove all user-added interior-only settings?"
	};

	void Draw()
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto& entries = manager->GetEntries(kSceneType);
		auto& theme = globals::menu->GetSettings().Theme;

		ImGui::Text("Interior Only Settings");
		ImGui::Separator();

		SceneSettingsUI::DrawPopups(kSceneType, popups);
		SceneSettingsUI::DrawAddSettingUI(kSceneType, addState);

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

		// Overwrite section with controls
		if (!overwriteIndices.empty()) {
			ImGui::Spacing();
			ImGui::TextColored(theme.StatusPalette.InfoColor, "Overwrite Files");
			ImGui::SameLine();

			bool allPaused = manager->AreAllOverwritesPaused(kSceneType);
			if (ImGui::SmallButton(allPaused ? "Unpause All" : "Pause All"))
				manager->SetAllOverwritesPaused(kSceneType, !allPaused);

			ImGui::SameLine();
			if (ImGui::SmallButton("Delete All"))
				popups.deleteAllOverwrites.Request();

			ImGui::Separator();

			for (auto i : overwriteIndices)
				SceneSettingsUI::DrawSettingEntry(kSceneType, i, popups);
		}

		// User section with controls
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
				popups.deleteAllUser.Request();

			if (!overwriteIndices.empty())
				ImGui::Separator();

			for (auto i : userIndices) {
				if (i >= manager->GetEntries(kSceneType).size())
					break;
				SceneSettingsUI::DrawSettingEntry(kSceneType, i, popups);
			}
		}
	}
}
