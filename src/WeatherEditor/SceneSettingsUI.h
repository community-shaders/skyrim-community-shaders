#pragma once

#include "SceneSettingsManager.h"
#include "Utils/UI.h"

/// Shared UI drawing utilities for scene-settings panels (Interior Only, Time of Day).
/// Eliminates duplicate ImGui code between InteriorOnlyPanel and TimeOfDayPanel.
namespace SceneSettingsUI
{
	using SceneType = SceneSettingsManager::SceneType;
	using EntrySource = SceneSettingsManager::EntrySource;
	using Period = SceneSettingsManager::TimeOfDayPeriod;

	/// Persistent state for a single "Add Setting" dropdown row.
	struct AddSettingState
	{
		int selectedFeatureIdx = -1;
		int selectedSettingIdx = -1;
		std::vector<std::string> cachedFeatureNames;
		std::vector<std::string> cachedSettingKeys;
	};

	/// Shared confirmation popup state for a panel.
	struct PopupState
	{
		Util::ConfirmationPopup deleteAllOverwrites;
		Util::ConfirmationPopup deleteSingleOverwrite{ "Delete Overwrite File?", "", "Delete" };
		Util::ConfirmationPopup deleteAllUser;
		size_t pendingDeleteIndex = SIZE_MAX;

		PopupState(const char* overwriteMsg, const char* userMsg) :
			deleteAllOverwrites("Delete All Overwrites?", overwriteMsg, "Delete All"),
			deleteAllUser("Delete All User Settings?", userMsg, "Delete All") {}
	};

	/// Draw the feature/setting dropdown + Add button.
	/// @param type         Scene type being edited.
	/// @param state        Persistent dropdown state (selection indices, caches).
	/// @param period       For TimeOfDay entries, which period to add to. Count = none.
	void DrawAddSettingUI(SceneType type, AddSettingState& state,
		Period period = Period::Count);

	/// Draw a single setting entry row (label, value editor, pause toggle, delete).
	/// @param type         Scene type being edited.
	/// @param index        Index into the entries vector.
	/// @param popups       Shared popup state for confirmations.
	/// @return true if the entry was deleted inline (caller should stop iterating).
	bool DrawSettingEntry(SceneType type, size_t index, PopupState& popups);

	/// Process all three delete-confirmation popups relative to the given type.
	void DrawPopups(SceneType type, PopupState& popups);

	/// Draw overwrite + user entry sections with section headers.
	/// @param type         Scene type being edited.
	/// @param popups       Shared popup state.
	/// @param overwriteIndices  Entry indices for overwrite entries.
	/// @param userIndices       Entry indices for user entries.
	void DrawEntrySections(SceneType type, PopupState& popups,
		const std::vector<size_t>& overwriteIndices,
		const std::vector<size_t>& userIndices);

	/// Draw a standalone scene-settings panel that dispatches to this panel's Draw().
	/// Handles ImGui::EndChild / ImGui::EndTable / ImGui::End early-return pattern
	/// used by EditorWindow for full-panel categories.
	/// @param category     Category name to check (e.g. "Interior Only").
	/// @param selected     Currently selected category string.
	/// @param drawFn       Drawing function to call if category matches.
	/// @return true if category matched and panel was drawn (caller should return).
	bool DrawCategoryPanel(const char* category, const std::string& selected,
		void (*drawFn)());
}
