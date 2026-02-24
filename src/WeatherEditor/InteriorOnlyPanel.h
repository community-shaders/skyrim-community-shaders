#pragma once

#include "Utils/UI.h"

/// UI panel for managing Interior Only scene settings within the Weather Editor.
/// Renders the list of entries with add/pause/delete controls.
namespace InteriorOnlyPanel
{
	/// Draw the full Interior Only settings panel (right column of the objects window)
	void Draw();

	/// Draw the "add new setting" UI (feature dropdown + setting dropdown + confirm)
	void DrawAddSettingUI();

	/// Draw a single setting entry row
	void DrawSettingEntry(size_t index);
}
