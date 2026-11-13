#pragma once

/// The single save path for everything Community Shaders keeps on this PC. Saving flows one way: the
/// main menu's save also saves the CS editor, the editor's save never writes the user config, and CS
/// tools that write into other mods (Light Placer, ...) save through their own buttons.
namespace SaveManager
{
	enum class Trigger
	{
		/// A save button or shortcut; confirms with a toast while the CS editor is open.
		User,
		/// A save the editor runs on its own, such as Scene Manager committing an edit; silent.
		Auto,
	};

	/**
	 * @brief Saves the CS editor: widgets with unsaved changes and Scene Manager's settings.
	 * @param trigger A user save also writes the widgets and the editor's preferences; an automatic one
	 *        writes only Scene Manager's settings.
	 */
	void SaveEditor(Trigger trigger);

	/**
	 * @brief Saves the user config, then the CS editor. The main menu's Save Settings.
	 * @param trigger Whether the user asked for the save.
	 */
	void SaveAll(Trigger trigger = Trigger::User);

	/**
	 * @brief Tooltip for a button that runs SaveEditor.
	 * @param unsaved Whether whatever the button sits beside has unsaved changes.
	 */
	const char* GetEditorSaveTooltip(bool unsaved);
}
