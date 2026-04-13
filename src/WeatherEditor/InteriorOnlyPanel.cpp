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

		if (ImGui::SmallButton("Add Setting"))
			SceneSettingsUI::OpenAddDialog(kSceneType, addState);

		SceneSettingsUI::DrawPopups(kSceneType, popups);
		SceneSettingsUI::DrawAddSettingDialog(kSceneType, addState);

		// Empty state
		if (entries.empty()) {
			ImGui::Spacing();
			ImGui::TextColored(theme.StatusPalette.Disable,
				"No interior-only settings configured.");
			ImGui::TextColored(theme.StatusPalette.Disable,
				"Use the Add Setting button above to add overrides.");
			ImGui::Spacing();
			ImGui::TextWrapped(
				"Settings added here will override feature defaults when you enter an interior cell. "
				"Values revert automatically when you exit.");
			return;
		}

		SceneSettingsUI::DrawEntrySections(kSceneType, popups);
	}
}
