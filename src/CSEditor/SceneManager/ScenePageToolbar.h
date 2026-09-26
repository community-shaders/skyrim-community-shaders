#pragma once

#include "SceneSettingsManager.h"

/// Page-wide actions for one scene context: pause or resume it, copy settings between it and
/// another context (in either direction), export it as a preset, and clear it. The page being
/// drawn is always one side of the copy; the other side is picked in the Copy modal.
namespace ScenePageToolbar
{
	/// Draws the actions right-aligned on the current row, along with the dialogs they open.
	/// Precede it with ImGui::SameLine to share a row with the content already on it.
	/// The context's period names the saved set the page authors: Count for a flat set.
	void Draw(const SceneSettingsManager::SceneContextId& context);
}
