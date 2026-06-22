#pragma once

#include "Utils/UI.h"

/**
 * @brief UI panel for managing Interior Only scene settings within the CS Editor.
 *
 * Renders the list of entries with add/pause/delete controls.
 */
namespace InteriorOnlyPanel
{
	/** @brief Draw the full Interior Only settings panel (right column of the objects window). */
	void Draw();

	/** @brief Draw the "add new setting" UI (feature dropdown + setting dropdown + confirm). */
	void DrawAddSettingUI();

	/**
	 * @brief Draw a single setting entry row.
	 * @param index The index of the entry in the settings list.
	 */
	void DrawSettingEntry(size_t index);
}
