#pragma once

#include <string>

/// Preset export dialog: metadata, artwork copied into Presets/<Name>/, and a load-ordered mod list.
/// Export is global (every context); open from any toolbar or the File menu.
namespace ScenePresetExport
{
	/// Whether there is anything to export: a user layer, or any installed overwrite.
	bool CanExport();

	/** @brief Translated tooltip of every Export Preset entry point: what it makes, and when to use another export. */
	const char* GetSummary();

	/// Opens the export dialog.
	void Open();

	/**
	 * @brief Opens the export dialog as a Baseline export: base settings only, with the scene options hidden.
	 * @param featureShortName Feature ticked to begin with, such as the one open in Base Settings.
	 */
	void OpenBaseline(const std::string& featureShortName = {});

	/// Draws the dialog and its confirmations. Call once per frame from the editor.
	void Draw();
}
