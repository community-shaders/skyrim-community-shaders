#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

struct Setting;
class Effect;

namespace UITree
{
	struct ViewOptions;
}

/**
 * @brief Standalone ENB-style editor for Effects 11 presets.
 *
 * Draws two panels at the screen edges so the scene stays visible while tuning:
 * "Effects 11" for enbseries.ini and weather file settings, and "Shader Parameters"
 * for the preset's .fx files. It is opened from the Effects 11 feature page or the
 * editor hotkey. The Community Shaders menu and the editor are never shown together.
 */
class Effects11Editor
{
public:
	static Effects11Editor& GetSingleton();

	/**
	 * @brief Opens the editor and closes the Community Shaders menu.
	 * @param a_returnToMenu Reopen the Community Shaders menu when the editor closes.
	 */
	void Open(bool a_returnToMenu);

	/**
	 * @brief Closes the editor.
	 * @param a_restoreMenu Reopen the Community Shaders menu if the editor was opened from it.
	 */
	void Close(bool a_restoreMenu = true);

	/** @brief Hotkey entry point: opens the editor (returning to the menu if it was open), or closes it. */
	void Toggle();

	bool IsOpen() const { return open.load(std::memory_order_relaxed); }

	/** @brief Draws the editor windows. Called once per frame by the overlay renderer while open. */
	void Draw();

	/** @brief Draws the status summary and "Open editor" launcher on the Effects 11 feature page. */
	void DrawLauncher();

	/** @brief True if ESC should close the editor (no popup or text field consumed it). */
	bool ShouldHandleEscapeKey();

	/** @brief Moves both panels back to their default positions on the next frame. */
	void RequestLayoutReset() { resetLayout = true; }

private:
	Effects11Editor() = default;

	/** Time-of-day focus: which period values are shown. 0-7 select a single TimeOfDayValue index. */
	static constexpr int kFocusCurrent = -2;
	static constexpr int kFocusAll = -1;

	enum class PendingAction
	{
		None,
		Revert,
		ReloadShaders
	};

	// Windows
	void DrawSettingsWindow();
	void DrawShaderWindow();
	void DrawNoPreset();

	// Settings window parts
	void DrawToolbar();
	void DrawStatus();
	void DrawSearchBar(std::string& a_filter, const char* a_hint, bool& a_focusRequest);
	void DrawPeriodFocusCombo();
	void DrawSections();
	std::vector<const Setting*> CollectRows(const std::string& a_category, const char* a_name) const;
	void DrawCategory(const std::string& a_category, const char* a_name, const char* a_description, const std::vector<const Setting*>& a_rows);
	void DrawCategoryWeatherToggle(const std::string& a_category);
	void DrawWeatherFileList();
	void DrawSettingRow(const Setting& a_setting, bool a_categoryActive);
	void DrawTimeOfDayRows(const Setting& a_setting, const char* a_label, bool a_editable, bool& a_labelHovered);
	void DrawColorTimeOfDayRow(const Setting& a_setting, const char* a_label, bool a_editable, bool& a_labelHovered);
	void DrawSettingTooltip(const Setting& a_setting, const std::string& a_name, bool a_dependencyMet);

	// Shader window parts
	void DrawEffectSection(Effect& a_effect, uint32_t a_enableSettingID, const char* a_friendlyName, UITree::ViewOptions& a_totals);
	int CountHiddenPeriodParameters() const;

	// Actions
	void RefreshPresetPaths();
	void Save();
	void Revert();
	void ReloadShaders();
	void RequestAction(PendingAction a_action);
	void DrawPendingActionPopup();
	void HandleShortcuts(bool& a_focusSearch);

	// Time of day
	float PeriodWeight(int a_period) const;
	bool IsPeriodActive(int a_period) const;
	bool IsPeriodFocused(int a_period) const;
	bool IsEffectPeriodShown(const std::string& a_period) const;
	std::vector<int> FocusedPeriods(bool a_exteriorOnly) const;
	std::string ActivePeriodSummary(bool a_withWeights) const;
	bool IsInterior() const;

	/** @brief File that edits to this category's weather-aware settings are written to. */
	std::string EditTargetFile(const std::string& a_category) const;
	/** @brief Weather whose values SettingManager edits: the one that dominates the current blend. */
	uint32_t EditWeatherID() const;

	std::atomic<bool> open{ false };
	bool returnToMenu = false;
	bool showShaderPanel = true;
	bool resetLayout = false;
	bool dirty = false;
	bool shaderReloadNeeded = false;
	bool suppressNextEscape = false;
	bool popupOpenLastFrame = false;
	bool focusSettingsSearch = false;
	bool focusShaderSearch = false;
	int periodFocus = kFocusCurrent;
	int hiddenPeriodParameters = 0;
	PendingAction pendingAction = PendingAction::None;
	std::string settingsFilter;
	std::string shaderFilter;

	/** Preset file locations, cached because resolving them touches the disk. */
	struct PresetPaths
	{
		std::string iniFull;
		std::string iniDisplay;
		std::string mainEffect;
	} presetPaths;
};
