#pragma once

#include <functional>

#include "SceneSettingsManager.h"
#include "Utils/UI.h"

/// Shared UI drawing utilities for scene-settings panels (Interior Only, Time of Day).
/// Eliminates duplicate ImGui code between InteriorOnlyPanel and TimeOfDayPanel.
namespace SceneSettingsUI
{
	using SceneType = SceneSettingsManager::SceneType;
	using EntrySource = SceneSettingsManager::EntrySource;
	using Period = SceneSettingsManager::TimeOfDayPeriod;

	/// Persistent state for the "+" add-setting dialog.
	struct AddSettingState
	{
		bool dialogOpen = false;
		int selectedFeatureIdx = -1;
		std::vector<std::string> cachedFeatureNames;
		std::vector<std::string> cachedSettingKeys;
		std::vector<bool> selectedSettings;  // Checkbox state per setting key

		void Reset()
		{
			dialogOpen = false;
			selectedFeatureIdx = -1;
			cachedFeatureNames.clear();
			cachedSettingKeys.clear();
			selectedSettings.clear();
		}
	};

	/// Shared confirmation popup state for a panel.
	struct PopupState
	{
		Util::ConfirmationPopup deleteAllOverwrites;
		Util::ConfirmationPopup deleteSingleOverwrite{ "Delete Overwrite File?", "", "Delete" };
		Util::ConfirmationPopup deleteRowOverwrite{ "Delete Overwrite Row?", "", "Delete" };
		Util::ConfirmationPopup deleteAllUser;
		size_t pendingDeleteIndex = SIZE_MAX;
		std::vector<size_t> pendingDeleteRow;

		PopupState(const char* overwriteMsg, const char* userMsg) :
			deleteAllOverwrites("Delete All Overwrites?", overwriteMsg, "Delete All"),
			deleteAllUser("Delete All User Settings?", userMsg, "Delete All") {}
	};

	/// Draw a "+" button that opens the add-setting dialog.
	/// @param type            Scene type being edited.
	/// @param state           Persistent dialog state.
	/// @param period          For TimeOfDay entries, which period to add to. Count = none.
	/// @param labelPrefix     Optional label drawn before the button (e.g. period name).
	/// @param addToAllPeriods When true, adds the setting to every period at once.
	void DrawAddSettingButton(SceneType type, AddSettingState& state,
		Period period = Period::Count, const char* labelPrefix = nullptr,
		bool addToAllPeriods = false);

	/// Position the cursor so the next add-setting button is right-aligned on the current line.
	void RightAlignNextButton();

	/// Draw the modal dialog opened by DrawAddSettingButton.
	/// Must be called each frame for each active dialog state.
	void DrawAddSettingDialog(SceneType type, AddSettingState& state,
		Period period = Period::Count, bool addToAllPeriods = false);

	/// Draw the value editor widget (checkbox/float input/int input) for a setting entry.
	/// @param type         Scene type being edited.
	/// @param index        Index into the entries vector.
	/// @param inputWidth   Width for float/int input widgets.
	void DrawValueEditor(SceneType type, size_t index, float inputWidth);

	/// Draw a single setting entry row (label, value editor, pause toggle, delete).
	/// @param type         Scene type being edited.
	/// @param index        Index into the entries vector.
	/// @param popups       Shared popup state for confirmations.
	/// @return true if the entry was deleted inline (caller should stop iterating).
	bool DrawSettingEntry(SceneType type, size_t index, PopupState& popups);

	/// Process all three delete-confirmation popups relative to the given type.
	void DrawPopups(SceneType type, PopupState& popups);

	/// Draw a section header with Pause All / Delete All inline buttons.
	/// @param label        Section label (e.g. "Overwrite Files", "User Settings").
	/// @param color        Header text color.
	/// @param idSuffix     ImGui ID suffix for button uniqueness (e.g. "##ow").
	/// @param allPaused    Whether all entries in this section are currently paused.
	/// @param onTogglePause Callback when Pause/Unpause All is clicked.
	/// @param onDeleteAll   Callback when Delete All is clicked.
	void DrawSectionHeader(const char* label, const ImVec4& color, const char* idSuffix,
		bool allPaused, std::function<void()> onTogglePause, std::function<void()> onDeleteAll);

	/// Draw overwrite + user entry sections with section-header-inline controls,
	/// each section's entries grouped by feature name.
	/// @param type         Scene type being edited.
	/// @param popups       Shared popup state.
	void DrawEntrySections(SceneType type, PopupState& popups);

	/// Draw a standalone scene-settings panel that dispatches to this panel's Draw().
	/// @param category     Category name to check (e.g. "Interior Only").
	/// @param selected     Currently selected category string.
	/// @param drawFn       Drawing function to call if category matches.
	/// @return true if category matched and panel was drawn (caller should return).
	bool DrawCategoryPanel(const char* category, const std::string& selected,
		void (*drawFn)());
}
