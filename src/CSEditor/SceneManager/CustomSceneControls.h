#pragma once

struct LUT;

/// Scene authoring for settings whose feature UI the widget interceptor cannot bind.
namespace CustomSceneControls
{
	/** @brief Draws LUT Load/Clear for the armed scene context. @return false when no scene is armed, so the feature draws its own controls. */
	bool DrawLutSceneControls(LUT& lut);

	/** @brief Main-menu LUT loads bypass the widget interceptor, so report them like an intercepted edit. */
	void RecordLutBaselineEdit(const LUT& lut);
}
