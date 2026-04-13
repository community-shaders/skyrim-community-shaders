#pragma once

#include <functional>

#include "SceneSettingsManager.h"
#include "Utils/UI.h"

/// Shared UI utilities for scene-settings panels.
namespace SceneSettingsUI
{
	using SceneType = SceneSettingsManager::SceneType;
	using EntrySource = SceneSettingsManager::EntrySource;
	using Period = SceneSettingsManager::TimeOfDayPeriod;
	static constexpr int kPeriodCount = SceneSettingsManager::kPeriodCount;

	/// Unique feature+key identifier for TOD table ordering.
	struct SettingId
	{
		std::string feature;
		std::string key;
		bool operator<(const SettingId& o) const { return std::tie(feature, key) < std::tie(o.feature, o.key); }
	};

	/// Period-indexed entry map built from a set of entries.
	struct SourceGroup
	{
		std::vector<SettingId> order;
		std::map<std::string, std::map<std::string, std::array<size_t, kPeriodCount>>> map;
	};

	/// Build a SourceGroup from entries, optionally filtered to a single source.
	SourceGroup BuildSourceGroup(const std::vector<SceneSettingsManager::SettingEntry>& entries,
		EntrySource sourceFilter, bool filterBySource = true);

	/// Draw a light separator between feature groups (skips the first call).
	void DrawGroupSeparator(bool& firstGroup);

	/// Split entry indices by source (Overwrite vs User).
	void SplitBySource(const std::vector<SceneSettingsManager::SettingEntry>& entries,
		std::vector<size_t>& overwriteOut, std::vector<size_t>& userOut);

	/// Remove entries by indices in reverse order.
	void RemoveIndicesReversed(const std::vector<size_t>& indices,
		std::function<void(size_t)> removeFn);

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

	/// Reset and open the add-setting dialog.
	void OpenAddDialog(SceneType type, AddSettingState& state);
	void OpenWeatherAddDialog(RE::FormID weatherId, AddSettingState& state);

	/// Draw the modal add-setting dialog. Call each frame for each active dialog state.
	void DrawAddSettingDialog(SceneType type, AddSettingState& state,
		Period period = Period::Count, bool addToAllPeriods = false);
	void DrawWeatherAddDialog(RE::FormID weatherId, AddSettingState& state,
		Period period = Period::Count, bool addToAllPeriods = false);

	/// Result from DrawFlyoutControls indicating which action the user triggered.
	struct FlyoutResult
	{
		bool toggled = false;
		bool reverted = false;
		bool deleted = false;
	};

	/// Draw flyout controls (toggle + revert + delete). Works for both single and group.
	FlyoutResult DrawFlyoutControls(bool paused, bool isGroup = false);

	void DrawValueEditor(SceneType type, size_t index, float inputWidth, bool readOnly = false);
	void DrawWeatherValueEditor(RE::FormID weatherId, size_t index, float inputWidth, bool readOnly = false);
	void DrawWeatherValueEditor(RE::FormID weatherId, const std::vector<size_t>& indices, float inputWidth, bool readOnly = false);
	bool DrawSettingEntry(SceneType type, size_t index, PopupState& popups);
	void DrawPopups(SceneType type, PopupState& popups);

	void DrawSectionHeader(const char* label, const ImVec4& color, const char* idSuffix,
		bool allPaused, std::function<void()> onTogglePause, std::function<void()> onDeleteAll);

	void DrawEntrySections(SceneType type, PopupState& popups);

	bool DrawCategoryPanel(const char* category, const std::string& selected,
		void (*drawFn)());
}
