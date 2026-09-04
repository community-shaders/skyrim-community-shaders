#pragma once

#include "SceneSettingsManager.h"

/// Page-wide actions for one scene context: pause or resume it, copy settings between it and
/// another context (in either direction), export it as a preset, and clear it. The page being
/// drawn is always one side of the copy; the other side is picked from the From/To submenus.
namespace ScenePageToolbar
{
	/// Draws the actions right-aligned on the current row, along with the dialogs they open.
	/// Precede it with ImGui::SameLine to share a row with the content already on it.
	/// The period scope has to match what the page authors: a page with time of day off owns every period.
	void Draw(const SceneSettingsManager::SceneContextId& context,
		SceneSettingsManager::PeriodScope periodScope);
}
