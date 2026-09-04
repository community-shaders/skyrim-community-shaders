#pragma once

#include "SceneSettingsManager.h"

/// The preset export dialog: a mod list with the resolved load order, a name field, and a
/// confirmation that turns destructive when the name already owns files on disk. Export is global,
/// so the page whose toolbar opened it only decides which toolbar is responsible for drawing it.
namespace ScenePresetExport
{
	/// Whether there is anything to export: a user layer, or any installed overwrite.
	bool CanExport();

	/// Opens the dialog, owned by the page whose toolbar was clicked.
	void Open(const SceneSettingsManager::SceneContextId& context);

	/// Draws the dialog and its confirmations. Call once per frame from every toolbar; only the
	/// call whose context matches the one passed to Open() does anything.
	void Draw(const SceneSettingsManager::SceneContextId& context);
}
