#pragma once

/// UI panel for managing Time of Day scene settings within the Weather Editor.
/// Shows period tabs (Dawn, Sunrise, Day, Sunset, Dusk, Night) with
/// add/pause/delete controls under each.  Delegates to shared SceneSettingsUI utilities.
namespace TimeOfDayPanel
{
	/// Draw the full Time of Day settings panel
	void Draw();
}
