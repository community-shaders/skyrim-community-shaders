#include "TimeOfDayPanel.h"

#include "../Globals.h"
#include "../Menu.h"
#include "../SceneSettingsManager.h"
#include "SceneSettingsUI.h"

namespace TimeOfDayPanel
{
	using SceneType = SceneSettingsManager::SceneType;
	using EntrySource = SceneSettingsManager::EntrySource;
	using Period = SceneSettingsManager::TimeOfDayPeriod;
	static constexpr auto kSceneType = SceneType::TimeOfDay;

	// Per-period add-setting state
	static SceneSettingsUI::AddSettingState periodAddState[SceneSettingsManager::kPeriodCount];

	// Shared popups
	static SceneSettingsUI::PopupState popups{
		"Are you sure you want to delete all time-of-day overwrite files?\nThis cannot be undone.",
		"Are you sure you want to remove all user-added time-of-day settings?"
	};

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

	static void DrawPeriodTab(Period period)
	{
		auto* manager = SceneSettingsManager::GetSingleton();
		const auto& entries = manager->GetEntries(kSceneType);
		auto& theme = globals::menu->GetSettings().Theme;

		// Draw single-overwrite delete popup (shared across tabs)
		if (popups.deleteSingleOverwrite.Draw()) {
			if (popups.pendingDeleteIndex < entries.size())
				manager->RemoveSetting(kSceneType, popups.pendingDeleteIndex);
			popups.pendingDeleteIndex = SIZE_MAX;
		}

		SceneSettingsUI::DrawAddSettingUI(kSceneType, periodAddState[static_cast<int>(period)], period);

		// Collect indices for this period
		std::vector<size_t> overwriteIndices, userIndices;
		CollectPeriodIndices(period, entries, overwriteIndices, userIndices);

		if (overwriteIndices.empty() && userIndices.empty()) {
			ImGui::Spacing();
			ImGui::TextColored(theme.StatusPalette.Disable,
				"No settings for %s.", SceneSettingsManager::GetPeriodName(period));
			return;
		}

		SceneSettingsUI::DrawEntrySections(kSceneType, popups, overwriteIndices, userIndices);
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
		if (popups.deleteAllOverwrites.Draw())
			manager->DeleteAllOverwrites(kSceneType);
		if (popups.deleteAllUser.Draw())
			manager->DeleteAllUserSettings(kSceneType);

		// Global controls
		if (!entries.empty()) {
			if (manager->HasOverwriteEntries(kSceneType)) {
				bool allPaused = manager->AreAllOverwritesPaused(kSceneType);
				if (ImGui::SmallButton(allPaused ? "Unpause All Overwrite" : "Pause All Overwrite"))
					manager->SetAllOverwritesPaused(kSceneType, !allPaused);
				ImGui::SameLine();
				if (ImGui::SmallButton("Delete All Overwrite"))
					popups.deleteAllOverwrites.Request();
				ImGui::SameLine();
			}

			bool allUserPaused = manager->AreAllUserPaused(kSceneType);
			if (ImGui::SmallButton(allUserPaused ? "Unpause All User" : "Pause All User"))
				manager->SetAllUserPaused(kSceneType, !allUserPaused);
			ImGui::SameLine();
			if (ImGui::SmallButton("Delete All User"))
				popups.deleteAllUser.Request();
		}

		// Period tabs
		if (ImGui::BeginTabBar("##TODPeriods")) {
			for (int i = 0; i < SceneSettingsManager::kPeriodCount; ++i) {
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
